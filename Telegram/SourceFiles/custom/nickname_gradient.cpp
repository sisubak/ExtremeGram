/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/nickname_gradient.h"

#include "apiwrap.h"
#include "base/flat_map.h"
#include "base/unixtime.h"
#include "core/enhanced_settings.h"
#include "custom/client_badge_detector.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtGui/QMouseEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace Custom {
namespace {

constexpr auto kMarker = "\xE2\x80\x8B" "EG";
constexpr int kMarkerEncodedLength = 13;
constexpr int kMinColors = 2;
constexpr int kMaxColors = 6;
constexpr std::array<float64, 3> kSpeedPeriods = { 14.0, 8.0, 4.0 };

[[nodiscard]] bool IsHexChar(QChar ch) {
	return ch.isDigit()
		|| (ch >= QChar('a') && ch <= QChar('f'))
		|| (ch >= QChar('A') && ch <= QChar('F'));
}

[[nodiscard]] std::optional<QColor> HexToColor(const QString &hex) {
	if (hex.size() != 6) {
		return std::nullopt;
	}
	for (const auto ch : hex) {
		if (!IsHexChar(ch)) {
			return std::nullopt;
		}
	}
	auto ok = false;
	const auto value = hex.toUInt(&ok, 16);
	if (!ok) {
		return std::nullopt;
	}
	return QColor::fromRgb(value & 0xFFFFFFu);
}

[[nodiscard]] QString ColorToHex(const QColor &color) {
	return QString::number(
		(static_cast<uint>(color.red()) << 16)
		| (static_cast<uint>(color.green()) << 8)
		| static_cast<uint>(color.blue()),
		16).rightJustified(6, '0');
}

[[nodiscard]] const std::vector<QColor> &PresetColors() {
	static const auto value = std::vector<QColor>{
		QColor(0xFF, 0x3B, 0x30),
		QColor(0xFF, 0x95, 0x00),
		QColor(0xFF, 0xCC, 0x00),
		QColor(0x34, 0xC7, 0x59),
		QColor(0x00, 0xC7, 0xBE),
		QColor(0x0A, 0x84, 0xFF),
		QColor(0x5E, 0x5C, 0xE6),
		QColor(0xBF, 0x5A, 0xF2),
		QColor(0xFF, 0x2D, 0x55),
		QColor(0x8E, 0x8E, 0x93),
	};
	return value;
}

[[nodiscard]] QLinearGradient BuildGradient(
		const std::vector<QColor> &colors,
		int width) {
	const auto w = std::max(1, width);
	const auto phase = std::fmod(crl::now() / 4000.0, 1.0);
	auto gradient = QLinearGradient(
		QPointF(-phase * w, 0.0),
		QPointF((2.0 - phase) * w, 0.0));
	gradient.setSpread(QGradient::RepeatSpread);
	if (colors.empty()) {
		gradient.setColorAt(0.0, QColor(0x44, 0x44, 0x44));
		gradient.setColorAt(1.0, QColor(0x44, 0x44, 0x44));
		return gradient;
	}
	const auto count = int(colors.size());
	for (auto i = 0; i != count; ++i) {
		gradient.setColorAt(double(i) / count, colors[i]);
	}
	gradient.setColorAt(1.0, colors[0]);
	return gradient;
}

class Swatch final : public Ui::RpWidget {
public:
	Swatch(
		QWidget *parent,
		QColor color,
		int size,
		Fn<void()> clicked)
	: Ui::RpWidget(parent)
	, _color(std::move(color))
	, _clicked(std::move(clicked)) {
		resize(size, size);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(this);
			p.setPen(Qt::NoPen);
			p.setBrush(_color);
			p.drawRoundedRect(rect(), 5, 5);
		}, lifetime());
	}

	void setColor(const QColor &color) {
		if (_color != color) {
			_color = color;
			update();
		}
	}

	[[nodiscard]] const QColor &color() const {
		return _color;
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_clicked();
		}
	}

private:
	QColor _color;
	Fn<void()> _clicked;
};

class RemoveButton final : public Ui::RpWidget {
public:
	RemoveButton(QWidget *parent, Fn<void()> clicked)
	: Ui::RpWidget(parent)
	, _clicked(std::move(clicked)) {
		resize(24, 24);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(this);
			auto pen = QPen(QColor(0x88, 0x88, 0x88));
			pen.setWidthF(1.6);
			p.setPen(pen);
			p.setRenderHint(QPainter::Antialiasing, true);
			p.drawLine(8, 8, 16, 16);
			p.drawLine(16, 8, 8, 16);
		}, lifetime());
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_clicked();
		}
	}

private:
	Fn<void()> _clicked;
};

} // namespace

std::optional<NicknameGradientConfig> ParseBioGradient(
		const QString &about) {
	const auto marker = QString::fromUtf8(kMarker);
	const auto index = about.indexOf(marker);
	if (index < 0) {
		return std::nullopt;
	}
	const auto rest = about.mid(index + marker.size());
	if (rest.size() < kMarkerEncodedLength) {
		return std::nullopt;
	}
	const auto hexPart = rest.left(12);
	const auto speedChar = rest.mid(12, 1);
	const auto c1 = HexToColor(hexPart.left(6));
	const auto c2 = HexToColor(hexPart.mid(6, 6));
	if (!c1 || !c2) {
		return std::nullopt;
	}
	auto speed = -1;
	if (speedChar == u"0"_q) {
		speed = 0;
	} else if (speedChar == u"1"_q) {
		speed = 1;
	} else if (speedChar == u"2"_q) {
		speed = 2;
	}
	if (speed < 0) {
		return std::nullopt;
	}
	return NicknameGradientConfig{ { *c1, *c2 }, speed };
}

QString EncodeBioGradient(const NicknameGradientConfig &config) {
	if (config.colors.size() != 2) {
		return {};
	}
	return QString::fromUtf8(kMarker)
		+ ColorToHex(config.colors[0])
		+ ColorToHex(config.colors[1])
		+ QString::number(std::clamp(config.speed, 0, 2));
}

QString StripBioGradient(const QString &about) {
	const auto marker = QString::fromUtf8(kMarker);
	const auto index = about.lastIndexOf(marker);
	if (index < 0) {
		return about;
	}
	auto stripped = about.left(index);
	while (stripped.endsWith(' ')) {
		stripped.chop(1);
	}
	return stripped;
}

NicknameGradient::NicknameGradient(not_null<Main::Session*> session)
: _session(session)
, _tickTimer([=] { onTick(); }) {
	ClientBadgeFor(_session).gradientChanges(
	) | rpl::on_next([=](PeerId peerId) {
		repaintPeer(peerId);
		fireChanged();
	}, _lifetime);

	_session->changes().peerUpdates(
		Data::PeerUpdate::Flag::About
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		repaintPeer(update.peer->id);
	}, _lifetime);
}

std::optional<NicknameGradientConfig> NicknameGradient::configFor(
		not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	if (!user || user->isBot() || user->isServiceUser()) {
		return std::nullopt;
	}
	if (auto gradient = ClientBadgeFor(_session).nicknameGradientFor(peer)) {
		_animated.emplace(user->id);
		ensureTimer();
		return gradient;
	}
	if (const auto bio = ParseBioGradient(user->about())) {
		_animated.emplace(user->id);
		ensureTimer();
		return bio;
	}
	return std::nullopt;
}

QPen NicknameGradient::penFor(
		const NicknameGradientConfig &config,
		float64 width) const {
	if (config.colors.empty()) {
		return QPen();
	}
	const auto period = kSpeedPeriods[std::clamp(config.speed, 0, 2)];
	const auto ms = int(period * 1000.0);
	const auto phase = std::fmod(
		(crl::now() % ms) / float64(ms),
		1.0);
	const auto offset = phase * width * 2.0;
	auto gradient = QLinearGradient(
		QPointF(-offset, 0.0),
		QPointF(width * 2.0 - offset, 0.0));
	gradient.setSpread(QGradient::RepeatSpread);
	const auto count = int(config.colors.size());
	for (auto i = 0; i != count; ++i) {
		gradient.setColorAt(double(i) / count, config.colors[i]);
	}
	gradient.setColorAt(1.0, config.colors[0]);
	auto pen = QPen(QBrush(gradient), 1.0);
	pen.setCosmetic(true);
	return pen;
}

rpl::producer<> NicknameGradient::ticks() const {
	return _tickEvents.events();
}

void NicknameGradient::repaintPeer(PeerId peerId) {
	const auto history = _session->data().historyLoaded(peerId);
	if (!history) {
		return;
	}
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			_session->data().requestItemRepaint(view->data());
		}
	}
}

void NicknameGradient::ensureTimer() {
	if (!_tickTimer.isActive()) {
		_tickTimer.callEach(120);
	}
}

void NicknameGradient::onTick() {
	const auto ids = _animated;
	for (const auto peerId : ids) {
		const auto peer = _session->data().peerLoaded(peerId);
		const auto user = peer ? peer->asUser() : nullptr;
		const auto has = user
			&& (ClientBadgeFor(_session).nicknameGradientFor(peer)
				|| ParseBioGradient(user->about()));
		if (has) {
			repaintPeer(peerId);
		} else {
			_animated.erase(peerId);
		}
	}
	fireChanged();
	if (_animated.empty()) {
		_tickTimer.cancel();
	}
}

void NicknameGradient::fireChanged() {
	if (_firing) {
		return;
	}
	_firing = true;
	_tickEvents.fire({});
	_firing = false;
}

void ApplyNicknameGradient(
		not_null<Main::Session*> session,
		std::optional<NicknameGradientConfig> config,
		Fn<void(bool ok, QString message)> done) {
	ClientBadgeFor(session).applySelfAvatarGradient(
		std::move(config),
		[=](bool ok, QString message) {
			if (ok) {
				const auto about = session->user()->about();
				const auto stripped = StripBioGradient(about);
				if (stripped != about) {
					session->api().saveSelfBio(stripped);
				}
			}
			if (done) {
				done(ok, std::move(message));
			}
		});
}

NicknameGradient &NicknameGradientFor(not_null<Main::Session*> session) {
	static base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<NicknameGradient>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<NicknameGradient>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

void ShowNicknameGradientBox(
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	auto current = NicknameGradientFor(session).configFor(session->user());
	if (!current) {
		current = ParseBioGradient(session->user()->about());
	}
	const auto colors = std::make_shared<std::vector<QColor>>(
		(current && current->colors.size() >= kMinColors)
			? current->colors
			: std::vector<QColor>{
				QColor(0x00, 0xC7, 0xBE),
				QColor(0xFF, 0x2D, 0x55) });
	const auto speed = std::make_shared<int>(
		current ? current->speed : 1);

	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_nickname_gradient());
		const auto layout = box->verticalLayout();

		const auto preview = layout->add(
			object_ptr<Ui::RpWidget>(layout),
			QMargins(
				st::boxPadding.left(),
				8,
				st::boxPadding.right(),
				0));
		preview->setFixedHeight(18);
		preview->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(preview);
			p.setPen(Qt::NoPen);
			p.setBrush(BuildGradient(*colors, preview->width()));
			p.drawRoundedRect(preview->rect(), 6, 6);
		}, preview->lifetime());

		const auto previewTimer = std::make_shared<base::Timer>([=] {
			preview->update();
		});
		previewTimer->callEach(60);
		preview->lifetime().add([previewTimer] {
			previewTimer->cancel();
		});

		const auto refreshPreview = [=] {
			preview->update();
		};

		auto colorsWrap = object_ptr<Ui::VerticalLayout>(layout);
		const auto colorsList = colorsWrap.data();
		layout->add(std::move(colorsWrap), QMargins(0, 0, 0, 0));

		const auto add = layout->add(
			object_ptr<Ui::SettingsButton>(
				layout,
				tr::lng_extremegram_nickname_gradient_add_color(),
				st::settingsButtonNoIcon),
			QMargins(
				st::boxPadding.left(),
				4,
				st::boxPadding.right(),
				4));

		const auto rebuild = std::make_shared<Fn<void()>>();
		const auto openEditor = [=](int index) {
			controller->show(Box([=](not_null<Ui::GenericBox*> editorBox) {
				editorBox->setTitle(
					tr::lng_extremegram_nickname_gradient_custom_color());
				const auto editor = editorBox->addRow(object_ptr<ColorEditor>(
					editorBox,
					ColorEditor::Mode::HSL,
					colors->at(index)));
				editorBox->addButton(tr::lng_box_ok(), [=] {
					(*colors)[index] = editor->color();
					refreshPreview();
					(*rebuild)();
					editorBox->closeBox();
				});
				editorBox->addButton(tr::lng_cancel(), [=] {
					editorBox->closeBox();
				});
				editorBox->setFocusCallback([=] {
					editor->setInnerFocus();
				});
			}));
		};

		*rebuild = [=] {
			colorsList->clear();
			for (auto i = 0; i != int(colors->size()); ++i) {
				const auto row = Ui::CreateChild<Ui::RpWidget>(colorsList);
				row->resize(360, 44);
				const auto currentSwatch = Ui::CreateChild<Swatch>(
					row,
					(*colors)[i],
					44,
					[=] { openEditor(i); });
				currentSwatch->moveToLeft(0, 0);
				auto x = 52;
				for (const auto &preset : PresetColors()) {
					const auto swatch = Ui::CreateChild<Swatch>(
						row,
						preset,
						24,
						[=, color = preset] {
							(*colors)[i] = color;
							currentSwatch->setColor(color);
							refreshPreview();
						});
					swatch->moveToLeft(x, 10);
					x += 28;
				}
				if (colors->size() > kMinColors) {
					const auto remove = Ui::CreateChild<RemoveButton>(
						row,
						[=] {
							colors->erase(colors->begin() + i);
							refreshPreview();
							(*rebuild)();
						});
					remove->moveToLeft(x + 4, 10);
				}
				colorsList->add(
					object_ptr<Ui::RpWidget>::fromRaw(row),
					QMargins(
						st::boxPadding.left(),
						2,
						st::boxPadding.right(),
						2));
			}
			add->setVisible(colors->size() < kMaxColors);
		};
		(*rebuild)();

		add->setClickedCallback([=] {
			if (colors->size() >= kMaxColors) {
				return;
			}
			colors->push_back(
				PresetColors()[colors->size() % PresetColors().size()]);
			refreshPreview();
			(*rebuild)();
		});

		const auto group = std::make_shared<Ui::RadiobuttonGroup>(*speed);
		group->setChangedCallback([=](int value) {
			*speed = value;
		});
		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				tr::lng_extremegram_nickname_gradient_speed(),
				st::defaultFlatLabel),
			QMargins(st::boxPadding.left(), 4, 0, 0));
		for (auto i = 0; i != 3; ++i) {
			const auto text = (i == 0)
				? tr::lng_extremegram_nickname_gradient_slow(tr::now)
				: (i == 1)
				? tr::lng_extremegram_nickname_gradient_medium(tr::now)
				: tr::lng_extremegram_nickname_gradient_fast(tr::now);
			layout->add(
				object_ptr<Ui::Radiobutton>(
					layout,
					group,
					i,
					text,
					st::defaultBoxCheckbox,
					st::defaultRadio),
				QMargins(
					st::boxPadding.left() + st::boxOptionListPadding.left(),
					0,
					st::boxPadding.right(),
					st::boxOptionListSkip));
		}

		const auto off = layout->add(
			object_ptr<Ui::SettingsButton>(
				layout,
				tr::lng_extremegram_nickname_gradient_off(),
				st::settingsButtonNoIcon),
			QMargins(
				st::boxPadding.left(),
				4,
				st::boxPadding.right(),
				4));
		off->setClickedCallback([=] {
			controller->showToast(
				tr::lng_extremegram_nickname_gradient_applying(tr::now));
			ApplyNicknameGradient(
				session,
				std::nullopt,
				[=](bool ok, QString message) {
					controller->showToast(std::move(message));
				});
			box->closeBox();
		});

		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
		box->addButton(tr::lng_box_ok(), [=] {
			if (colors->size() < kMinColors) {
				return;
			}
			controller->showToast(
				tr::lng_extremegram_nickname_gradient_applying(tr::now));
			ApplyNicknameGradient(
				session,
				NicknameGradientConfig{ *colors, *speed },
				[=](bool ok, QString message) {
					controller->showToast(std::move(message));
				});
			box->closeBox();
		});
	}));
}

} // namespace Custom
