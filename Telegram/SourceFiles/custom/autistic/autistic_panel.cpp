/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/autistic/autistic_panel.h"

#include "core/enhanced_settings.h"
#include "custom/autistic/autistic_mode.h"
#include "lang/lang_keys.h"
#include "styles/palette.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "window/window_main.h"
#include "window/window_session_controller.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>

namespace Autistic {
namespace {

constexpr auto kPanelWidth = 300;
constexpr auto kMargin = 12;
constexpr auto kHeaderHeight = 38;

class Panel final : public Ui::RpWidget {
public:
	explicit Panel(not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	[[nodiscard]] QRect cardGeometry() const;
	void updatePosition();

	not_null<Window::SessionController*> _controller;

};

Panel::Panel(not_null<Window::SessionController*> controller)
: Ui::RpWidget(controller->widget().get())
, _controller(controller) {
	namespace Option = EnhancedSettings::Option;
	const auto apply = [=](EnhancedSettings::Key<bool> key, bool value) {
		EnhancedSettings::ApplyOption(_controller, key, value);
	};

	const auto body = Ui::CreateChild<Ui::RpWidget>(this);
	body->paintRequest(
	) | rpl::on_next([=](const QRect &r) {
		QPainter p(body.get());
		p.fillRect(r, st::boxBg);
	}, body->lifetime());

	const auto rowHeight = st::settingsButtonNoIcon.height;
	const auto rowCount = 10;
	const auto shadow = st::boxRoundShadow.extend;
	const auto panelWidth = style::ConvertScale(kPanelWidth);
	const auto rowsHeight = rowCount * rowHeight;
	resize(
		panelWidth + shadow.left() + shadow.right(),
		style::ConvertScale(kHeaderHeight)
			+ rowsHeight
			+ shadow.top()
			+ shadow.bottom());
	body->setGeometry(QRect(
		shadow.left(),
		shadow.top() + style::ConvertScale(kHeaderHeight),
		panelWidth,
		rowsHeight));

	auto rowTop = 0;
	const auto addToggle = [&](rpl::producer<QString> title,
			EnhancedSettings::Key<bool> key) {
		const auto button = Ui::CreateChild<Ui::SettingsButton>(
			body,
			std::move(title),
			st::settingsButtonNoIcon);
		button->toggleOn(EnhancedSettings::Watch(key));
		button->toggledValue(
		) | rpl::filter([=](bool value) {
			return value != EnhancedSettings::Get(key);
		}) | rpl::on_next([=](bool value) {
			apply(key, value);
		}, button->lifetime());
		button->setGeometry(0, rowTop, panelWidth, rowHeight);
		rowTop += rowHeight;
	};
	const auto masterClick = [&](bool value) {
		EnhancedSettings::ApplyOption(
			_controller,
			Option::AutisticTelegram,
			value);
	};

	const auto master = Ui::CreateChild<Ui::SettingsButton>(
		body,
		tr::lng_extremegram_autistic_telegram(),
		st::settingsButtonNoIcon);
	master->toggleOn(EnhancedSettings::Watch(Option::AutisticTelegram));
	master->toggledValue(
	) | rpl::filter([=](bool value) {
		return value != EnhancedSettings::Get(Option::AutisticTelegram);
	}) | rpl::on_next(masterClick, master->lifetime());
	master->setGeometry(0, rowTop, panelWidth, rowHeight);
	rowTop += rowHeight;

	addToggle(
		tr::lng_extremegram_autistic_commands(),
		Option::AutisticCommands);
	addToggle(
		tr::lng_extremegram_autistic_ignore(),
		Option::AutisticIgnore);
	addToggle(
		tr::lng_extremegram_autistic_anti_ping(),
		Option::AutisticAntiPing);
	addToggle(
		tr::lng_extremegram_autistic_read_invisible(),
		Option::AutisticReadInvisible);
	addToggle(
		tr::lng_extremegram_autistic_ghosts(),
		Option::AutisticGhosts);
	addToggle(
		tr::lng_extremegram_autistic_show_orig(),
		Option::AutisticShowOrig);
	addToggle(
		tr::lng_extremegram_autistic_layout_fix(),
		Option::AutisticLayoutFix);
	addToggle(
		tr::lng_extremegram_autistic_heart(),
		Option::AutisticHeart);

	const auto cycle = Ui::CreateChild<Ui::SettingsButton>(
		body,
		tr::lng_extremegram_autistic_hide_foreign(),
		st::settingsButtonNoIcon);
	cycle->setClickedCallback([=] {
		CycleHideForeign();
		_controller->showToast(
			QString::fromUtf8("Скрытие чужих: ")
			+ HideForeignLabel(hideForeign()));
	});
	cycle->setGeometry(0, rowTop, panelWidth, rowHeight);

	setAttribute(Qt::WA_ShowWithoutActivating);
	updatePosition();
	parentWidget()->installEventFilter(this);
}

QRect Panel::cardGeometry() const {
	const auto shadow = st::boxRoundShadow.extend;
	return QRect(
		shadow.left(),
		shadow.top(),
		width() - shadow.left() - shadow.right(),
		height() - shadow.top() - shadow.bottom());
}

void Panel::updatePosition() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	move(
		parent->width() - width() - style::ConvertScale(kMargin),
		style::ConvertScale(60));
}

void Panel::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	const auto card = cardGeometry();
	const auto radius = style::ConvertScale(10);

	Ui::Shadow::paint(p, card, width(), st::boxRoundShadow);

	auto border = st::windowActiveTextFg->c;
	border.setAlphaF(0.3);
	p.setPen(QPen(border, 1.));
	p.setBrush(st::boxBg);
	p.drawRoundedRect(card, radius, radius);

	p.setPen(st::windowActiveTextFg);
	p.setFont(st::semiboldFont);
	p.drawText(
		QRect(
			card.left() + style::ConvertScale(14),
			card.top() + style::ConvertScale(8),
			card.width() - style::ConvertScale(28),
			st::semiboldFont->height),
		Qt::AlignVCenter | Qt::AlignLeft,
		QString::fromUtf8("Autistic Telegram"));
}

bool Panel::eventFilter(QObject *object, QEvent *event) {
	if (object == parentWidget()) {
		if (event->type() == QEvent::Resize
			|| event->type() == QEvent::Move) {
			updatePosition();
		} else if (event->type() == QEvent::WindowDeactivate) {
			hide();
		}
	}
	return Ui::RpWidget::eventFilter(object, event);
}

} // namespace

void TogglePanel(not_null<Window::SessionController*> controller) {
	static QPointer<Panel> panel;
	if (panel) {
		panel->hide();
		panel->deleteLater();
		panel = nullptr;
		return;
	}
	panel = new Panel(controller);
	panel->show();
	panel->raise();
}

} // namespace Autistic
