/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/nickname_gradient.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/enhanced_settings.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtGui/QMouseEvent>
#include <algorithm>
#include <array>
#include <cmath>

namespace Custom {
namespace {

constexpr auto kMarker = "\xE2\x80\x8B" "EG";
constexpr int kMarkerEncodedLength = 13;
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
	if (speedChar == u"0"_q) {
		return NicknameGradientConfig{ *c1, *c2, 0 };
	} else if (speedChar == u"1"_q) {
		return NicknameGradientConfig{ *c1, *c2, 1 };
	} else if (speedChar == u"2"_q) {
		return NicknameGradientConfig{ *c1, *c2, 2 };
	}
	return std::nullopt;
}

QString EncodeBioGradient(const NicknameGradientConfig &config) {
	return QString::fromUtf8(kMarker)
		+ ColorToHex(config.c1)
		+ ColorToHex(config.c2)
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
	_session->changes().peerUpdates(
		Data::PeerUpdate::Flag::About
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		const auto user = update.peer->asUser();
		if (!user) {
			return;
		}
		auto &entry = _cache[user->id];
		if (entry.about != user->about()) {
			entry.about = user->about();
			entry.config = ParseBioGradient(entry.about);
			if (entry.config) {
				ensureTimer();
			}
		}
		repaintPeer(user->id);
	}, _lifetime);
}

std::optional<NicknameGradientConfig> NicknameGradient::configFor(
		not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	if (!user || user->isBot() || user->isServiceUser()) {
		return std::nullopt;
	}
	auto &entry = _cache[user->id];
	if (entry.about != user->about()) {
		entry.about = user->about();
		entry.config = ParseBioGradient(entry.about);
		if (entry.config) {
			ensureTimer();
		}
	}
	if (!entry.config
		&& entry.about.isEmpty()
		&& crl::now() - entry.lastFullRequest > 30 * crl::time(1000)) {
		entry.lastFullRequest = crl::now();
		user->updateFull();
	}
	return entry.config;
}

QPen NicknameGradient::penFor(
		const NicknameGradientConfig &config,
		float64 width) const {
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
	gradient.setColorAt(0.0, config.c1);
	gradient.setColorAt(0.5, config.c2);
	gradient.setColorAt(1.0, config.c1);
	auto pen = QPen(QBrush(gradient), 1.0);
	pen.setCosmetic(true);
	return pen;
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
	auto animated = false;
	for (const auto &[id, entry] : _cache) {
		if (entry.config) {
			animated = true;
			repaintPeer(id);
		}
	}
	if (!animated) {
		_tickTimer.cancel();
	}
}

void ApplyNicknameGradient(
		not_null<Main::Session*> session,
		const NicknameGradientConfig &config) {
	auto about = StripBioGradient(session->user()->about());
	const auto encoded = EncodeBioGradient(config);
	about = about.isEmpty() ? encoded : (about + ' ' + encoded);
	session->api().saveSelfBio(about);
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

} // namespace Custom

namespace {

class Swatch final : public Ui::RpWidget {
public:
	Swatch(QWidget *parent, QColor color, Fn<void(QColor)> chosen)
	: Ui::RpWidget(parent)
	, _color(std::move(color))
	, _chosen(std::move(chosen)) {
		resize(38, 24);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(this);
			p.setPen(Qt::NoPen);
			p.setBrush(_color);
			p.drawRoundedRect(rect(), 4, 4);
			if (_selected) {
				auto pen = QPen(QColor(255, 255, 255, 220));
				pen.setWidth(2);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				p.drawRoundedRect(
					rect().marginsRemoved(QMargins(1, 1, 1, 1)),
					4,
					4);
			}
		}, lifetime());
	}

	[[nodiscard]] const QColor &color() const {
		return _color;
	}

	void setSelected(bool value) {
		if (_selected != value) {
			_selected = value;
			update();
		}
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		_chosen(_color);
	}

private:
	QColor _color;
	bool _selected = false;
	Fn<void(QColor)> _chosen;
};

} // namespace

namespace Custom {

void ShowNicknameGradientBox(
		not_null<Window::SessionController*> controller) {
	const auto current = ParseBioGradient(
		controller->session().user()->about());
	const auto c1 = std::make_shared<QColor>(
		current ? current->c1 : QColor(0x00, 0xC7, 0xBE));
	const auto c2 = std::make_shared<QColor>(
		current ? current->c2 : QColor(0xFF, 0x2D, 0x55));
	const auto speed = std::make_shared<int>(
		current ? current->speed : 1);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_nickname_gradient());
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
		const auto layout = box->verticalLayout();

		const auto apply = [=] {
			ApplyNicknameGradient(
				&controller->session(),
				{ *c1, *c2, *speed });
		};

		const auto preview = layout->add(
			object_ptr<Ui::RpWidget>(layout),
			QMargins(
				st::boxPadding.left(),
				8,
				st::boxPadding.right(),
				0));
		preview->setFixedHeight(14);
		preview->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(preview);
			auto gradient = QLinearGradient(
				0.0,
				0.0,
				preview->width() * 2.0,
				0.0);
			gradient.setColorAt(0.0, *c1);
			gradient.setColorAt(0.5, *c2);
			gradient.setColorAt(1.0, *c1);
			p.fillRect(preview->rect(), QBrush(gradient));
		}, preview->lifetime());
		const auto refreshPreview = [=] { preview->update(); };

		const auto addColorRow = [&](
				const std::vector<QColor> &colors,
				std::shared_ptr<QColor> target) {
			const auto row = Ui::CreateChild<Ui::RpWidget>(layout.get());
			row->resize(colors.size() * 38 + 8 * (colors.size() - 1), 24);
			const auto swatches = std::make_shared<std::vector<Swatch*>>();
			auto x = 0;
			for (const auto &color : colors) {
				const auto swatch = Ui::CreateChild<Swatch>(row, color, [=](QColor chosen) {
					*target = chosen;
					for (const auto &s : *swatches) {
						s->setSelected(s->color() == chosen);
					}
					refreshPreview();
					apply();
				});
				swatch->moveToLeft(x, 0);
				swatch->setSelected(color == *target);
				swatches->push_back(swatch);
				x += 46;
			}
			layout->add(
				object_ptr<Ui::RpWidget>::fromRaw(row),
				QMargins(
					st::boxPadding.left(),
					2,
					st::boxPadding.right(),
					2));
		};

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				tr::lng_extremegram_nickname_gradient_color1(),
				st::defaultFlatLabel),
			QMargins(st::boxPadding.left(), 4, 0, 0));
		addColorRow(
			{
				QColor(0xFF, 0x3B, 0x30),
				QColor(0xFF, 0x95, 0x00),
				QColor(0xFF, 0xCC, 0x00),
				QColor(0x34, 0xC7, 0x59),
				QColor(0x00, 0xC7, 0xBE),
				QColor(0x0A, 0x84, 0xFF),
			},
			c1);
		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				tr::lng_extremegram_nickname_gradient_color2(),
				st::defaultFlatLabel),
			QMargins(st::boxPadding.left(), 4, 0, 0));
		addColorRow(
			{
				QColor(0xFF, 0x2D, 0x55),
				QColor(0xBF, 0x5A, 0xF2),
				QColor(0xFF, 0x37, 0x5F),
				QColor(0xFF, 0xD6, 0x0A),
				QColor(0x30, 0xD1, 0x58),
				QColor(0x5E, 0x5C, 0xE6),
			},
			c2);

		const auto group = std::make_shared<Ui::RadiobuttonGroup>(*speed);
		group->setChangedCallback([=](int value) {
			*speed = value;
			apply();
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
			controller->session().api().saveSelfBio(
				StripBioGradient(controller->session().user()->about()));
			box->closeBox();
		});
	}));
}

} // namespace Custom
