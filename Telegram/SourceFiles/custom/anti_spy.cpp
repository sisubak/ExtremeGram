/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/anti_spy.h"

#include "base/event_filter.h"
#include "base/flat_map.h"
#include "core/enhanced_settings.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <algorithm>
#include <array>

#if defined Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#elif defined Q_OS_MAC
#include <libproc.h>
#endif

namespace Custom::AntiSpy {
namespace {

constexpr auto kPollInterval = 900;

[[nodiscard]] const std::vector<AppInfo> &AppsData() {
	static const auto result = std::vector<AppInfo>{
		{ u"OBS Studio"_q, { u"obs64.exe"_q, u"obs32.exe"_q, u"obs"_q } },
		{ u"Discord"_q, {
			u"discord.exe"_q,
			u"discord"_q,
			u"discordcanary"_q,
			u"discordptb"_q } },
		{ u"Lightshot"_q, { u"lightshot.exe"_q, u"lightshot"_q } },
		{ u"ShareX"_q, { u"sharex.exe"_q, u"sharex"_q } },
		{ u"Bandicam"_q, { u"bdcam.exe"_q, u"bandicam"_q } },
		{ u"Fraps"_q, { u"fraps.exe"_q, u"fraps"_q } },
		{ u"Ножницы Windows"_q, {
			u"snippingtool.exe"_q,
			u"snippingtool"_q,
			u"screenclippinghost.exe"_q } },
		{ u"Xbox Game Bar"_q, {
			u"gamebar.exe"_q,
			u"gamebarftserver.exe"_q,
			u"xboxgamebar"_q } },
		{ u"TeamViewer"_q, { u"teamviewer.exe"_q, u"teamviewer"_q } },
		{ u"AnyDesk"_q, { u"anydesk.exe"_q, u"anydesk"_q } },
		{ u"Zoom"_q, { u"zoom.exe"_q, u"zoom"_q } },
		{ u"NVIDIA Share"_q, {
			u"nvspcaps64.exe"_q,
			u"nvsphelper64.exe"_q } },
	};
	return result;
}

[[nodiscard]] QStringList RunningProcessNames() {
	auto result = QStringList();
#if defined Q_OS_WIN
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return result;
	}
	auto entry = PROCESSENTRY32{};
	entry.dwSize = sizeof(entry);
	if (Process32First(snapshot, &entry)) {
		do {
			result.push_back(
				QString::fromWCharArray(entry.szExeFile).toLower());
		} while (Process32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);
#elif defined Q_OS_MAC
	auto pids = std::vector<pid_t>(8192);
	const auto bytes = proc_listpids(
		PROC_ALL_PIDS,
		0,
		pids.data(),
		int(pids.size() * sizeof(pid_t)));
	if (bytes > 0) {
		pids.resize(bytes / sizeof(pid_t));
		auto name = std::array<char, PROC_PIDPATHINFO_MAXSIZE>();
		for (const auto pid : pids) {
			if (pid <= 0) {
				continue;
			}
			if (proc_name(pid, name.data(), int(name.size())) > 0) {
				result.push_back(
					QFileInfo(QString::fromUtf8(name.data()))
						.fileName()
						.toLower());
			}
		}
	}
#else
	const auto proc = QDir(u"/proc"_q);
	const auto entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &entry : entries) {
		auto ok = false;
		entry.toInt(&ok);
		if (!ok) {
			continue;
		}
		auto file = QFile(u"/proc/"_q + entry + u"/comm"_q);
		if (file.open(QIODevice::ReadOnly)) {
			result.push_back(
				QString::fromUtf8(file.readAll()).trimmed().toLower());
		}
	}
#endif
	return result;
}

class Manager final {
public:
	Manager();
	~Manager();

	void registerWindow(not_null<QWidget*> window);
	void refresh();

private:
	[[nodiscard]] bool shouldProtect() const;
	[[nodiscard]] bool anySelectedRunning() const;
	void ensureTimer();

	rpl::lifetime _lifetime;
	base::flat_map<QWidget*, bool> _applied;
	std::vector<QPointer<QWidget>> _windows;
	QTimer _timer;
};

Manager::Manager() {
	_timer.setInterval(kPollInterval);
	QObject::connect(&_timer, &QTimer::timeout, [=] {
		refresh();
	});
	using namespace EnhancedSettings;
	Watch(Option::AntiSpy) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	Watch(Option::AntiSpyOtherApps) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	Watch(Option::AntiSpyApps) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
}

Manager::~Manager() = default;

void Manager::registerWindow(not_null<QWidget*> window) {
	_windows.push_back(window.get());
	base::install_event_filter(window, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::WinIdChange) {
			_applied.erase(window.get());
			refresh();
		} else if (event->type() == QEvent::Destroy) {
			_applied.erase(window.get());
			_windows.erase(
				std::remove_if(
					_windows.begin(),
					_windows.end(),
					[&](const QPointer<QWidget> &value) {
						return value.data() == window.get();
					}),
				_windows.end());
		}
		return base::EventFilterResult::Continue;
	}, _lifetime);
	refresh();
}

bool Manager::shouldProtect() const {
	using namespace EnhancedSettings;
	if (!Get(Option::AntiSpy)) {
		return false;
	}
	if (!Platform::ScreenshotProtectionSupported()) {
		return false;
	}
	if (Get(Option::AntiSpyOtherApps)) {
		return true;
	}
	return anySelectedRunning();
}

bool Manager::anySelectedRunning() const {
	using namespace EnhancedSettings;
	const auto selected = Get(Option::AntiSpyApps);
	if (selected.isEmpty()) {
		return false;
	}
	const auto &apps = AppsData();
	auto wanted = QStringList();
	for (const auto index : selected) {
		if (index >= 0 && index < int(apps.size())) {
			wanted += apps[index].processes;
		}
	}
	if (wanted.isEmpty()) {
		return false;
	}
	const auto running = RunningProcessNames();
	for (const auto &process : wanted) {
		if (running.contains(process)) {
			return true;
		}
	}
	return false;
}

void Manager::ensureTimer() {
	using namespace EnhancedSettings;
	const auto needed = Get(Option::AntiSpy)
		&& !Get(Option::AntiSpyOtherApps)
		&& !Get(Option::AntiSpyApps).isEmpty()
		&& Platform::ScreenshotProtectionSupported();
	if (needed && !_timer.isActive()) {
		_timer.start();
	} else if (!needed && _timer.isActive()) {
		_timer.stop();
	}
}

void Manager::refresh() {
	_windows.erase(
		std::remove_if(
			_windows.begin(),
			_windows.end(),
			[](const QPointer<QWidget> &value) { return value.isNull(); }),
		_windows.end());
	const auto active = shouldProtect();
	for (const auto &window : _windows) {
		const auto raw = window.data();
		if (!raw) {
			continue;
		}
		const auto i = _applied.find(raw);
		if (i == _applied.end() || i->second != active) {
			Platform::SetWindowScreenshotProtection(
				not_null<QWidget*>(raw),
				active);
			_applied[raw] = active;
		}
	}
	ensureTimer();
}

Manager &ManagerInstance() {
	static auto instance = Manager();
	return instance;
}

} // namespace

const std::vector<AppInfo> &KnownApps() {
	return AppsData();
}

bool PlatformSupported() {
	return Platform::ScreenshotProtectionSupported();
}

void RegisterWindow(not_null<QWidget*> window) {
	ManagerInstance().registerWindow(window);
}

void ShowSettingsBox(not_null<Window::SessionController*> controller) {
	using namespace EnhancedSettings;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_anti_spy());
		const auto layout = box->verticalLayout();

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				tr::lng_extremegram_anti_spy_about(tr::now),
				st::boxLabel),
			QMargins(
				st::boxPadding.left(),
				0,
				st::boxPadding.right(),
				8));

		if (!PlatformSupported()) {
			layout->add(
				object_ptr<Ui::FlatLabel>(
					layout,
					tr::lng_extremegram_anti_spy_unsupported(tr::now),
					st::boxLabel),
				QMargins(
					st::boxPadding.left(),
					0,
					st::boxPadding.right(),
					8));
		}

		const auto &apps = KnownApps();
		for (auto i = 0; i != int(apps.size()); ++i) {
			const auto checked = Get(Option::AntiSpyApps).contains(i);
			const auto row = layout->add(
				object_ptr<Ui::Checkbox>(
					layout,
					apps[i].name,
					checked,
					st::defaultBoxCheckbox),
				QMargins(
					st::boxPadding.left() + st::boxOptionListPadding.left(),
					0,
					st::boxPadding.right(),
					0));
			row->checkedValue() | rpl::on_next([=](bool value) {
				auto list = Get(Option::AntiSpyApps);
				if (value) {
					if (!list.contains(i)) {
						list.push_back(i);
					}
				} else {
					list.removeAll(i);
				}
				std::sort(list.begin(), list.end());
				ApplyOption(controller, Option::AntiSpyApps, list);
			}, row->lifetime());
		}

		const auto other = layout->add(
			object_ptr<Ui::Checkbox>(
				layout,
				tr::lng_extremegram_anti_spy_other(tr::now),
				Get(Option::AntiSpyOtherApps),
				st::defaultBoxCheckbox),
			QMargins(
				st::boxPadding.left() + st::boxOptionListPadding.left(),
				12,
				st::boxPadding.right(),
				0));
		other->checkedValue() | rpl::on_next([=](bool value) {
			ApplyOption(controller, Option::AntiSpyOtherApps, value);
		}, other->lifetime());

		const auto off = layout->add(
			object_ptr<Ui::SettingsButton>(
				layout,
				tr::lng_extremegram_anti_spy_off(),
				st::settingsButtonNoIcon),
			QMargins(
				st::boxPadding.left(),
				12,
				st::boxPadding.right(),
				4));
		off->setClickedCallback([=] {
			ApplyOption(controller, Option::AntiSpy, false);
			box->closeBox();
		});

		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Custom::AntiSpy
