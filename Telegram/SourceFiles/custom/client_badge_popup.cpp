/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/client_badge_popup.h"

#include "custom/client_badge_detector.h"
#include "custom/smooth_animations.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "styles/palette.h"
#include "styles/style_basic.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Custom {
namespace {

constexpr auto kCardRadius = 14;
constexpr auto kIconSize = 36;
constexpr auto kPadding = 14;
constexpr auto kShadow = 10;
constexpr auto kTextSkip = 12;
constexpr auto kShowDuration = crl::time(240);
constexpr auto kHideDuration = crl::time(170);
constexpr auto kHoldDuration = crl::time(2600);

class BadgePopup final : public Ui::RpWidget {
public:
	BadgePopup(QWidget *parent, QImage icon);
	~BadgePopup();

	void startShow();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void startHide();
	void updatePosition();

	QImage _icon;
	QString _title;
	QString _subtitle;
	Ui::Animations::Simple _animation;
	QTimer _holdTimer;
	float64 _progress = 0.;
	bool _hiding = false;

};

BadgePopup::BadgePopup(QWidget *parent, QImage icon)
: Ui::RpWidget(parent)
, _icon(std::move(icon)) {
	setCursor(style::cur_pointer);
	setAttribute(Qt::WA_ShowWithoutActivating);

	_title = u"ExtremeGram"_q;
	_subtitle = tr::lng_extremegram_badge_clicked(tr::now);

	const auto iconSize = style::ConvertScale(kIconSize);
	const auto padding = style::ConvertScale(kPadding);
	const auto textSkip = style::ConvertScale(kTextSkip);
	const auto shadow = style::ConvertScale(kShadow);
	const auto textWidth = std::max(
		st::semiboldFont->width(_title),
		st::normalFont->width(_subtitle));
	const auto maxCard = std::max(
		style::ConvertScale(280),
		std::min(
			style::ConvertScale(430),
			parent->width() - 2 * shadow - style::ConvertScale(24)));
	const auto cardWidth = std::clamp(
		padding + iconSize + textSkip + textWidth + padding,
		style::ConvertScale(280),
		maxCard);
	const auto cardHeight = padding + iconSize + padding;
	resize(cardWidth + 2 * shadow, cardHeight + 2 * shadow);
	updatePosition();

	parent->installEventFilter(this);

	_holdTimer.setSingleShot(true);
	QObject::connect(&_holdTimer, &QTimer::timeout, [this] {
		startHide();
	});
}

BadgePopup::~BadgePopup() {
	if (const auto parent = parentWidget()) {
		parent->removeEventFilter(this);
	}
}

void BadgePopup::startShow() {
	show();
	raise();
	_animation.start([this](float64 value) {
		_progress = value;
		update();
	}, 0., 1., kShowDuration, anim::transition(
		Custom::Animations::CubicSmoothOut));
	_holdTimer.start(int(kHoldDuration + kShowDuration));
}

void BadgePopup::startHide() {
	if (_hiding) {
		return;
	}
	_hiding = true;
	_holdTimer.stop();
	_animation.start([this](float64 value) {
		_progress = value;
		update();
		if (value <= 0.) {
			deleteLater();
		}
	}, _progress, 0., kHideDuration, anim::transition(
		Custom::Animations::CubicSmoothOut));
}

void BadgePopup::updatePosition() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	const auto x = (parent->width() - width()) / 2;
	move(std::max(0, x), style::ConvertScale(4));
}

void BadgePopup::paintEvent(QPaintEvent *) {
	const auto progress = std::clamp(_progress, 0., 1.);
	if (progress <= 0.) {
		return;
	}

	auto p = QPainter(this);
	p.setOpacity(progress);

	const auto shadow = style::ConvertScale(kShadow);
	const auto radius = style::ConvertScale(kCardRadius);
	auto card = QRectF(
		shadow,
		shadow,
		width() - 2 * shadow,
		height() - 2 * shadow);

	const auto scale = 0.96 + 0.04 * progress;
	p.translate(width() / 2., height() / 2.);
	p.scale(scale, scale);
	p.translate(-width() / 2., -height() / 2.);
	p.translate(0., -style::ConvertScale(10) * (1. - progress));

	const auto shadowColor = st::shadowFg->c;
	for (auto i = 3; i >= 1; --i) {
		auto color = shadowColor;
		color.setAlphaF(std::min(1., color.alphaF() * 0.5 * i));
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(
			card.adjusted(-i, -i + 2., i, i + 2.),
			radius + i,
			radius + i);
	}

	auto border = st::windowActiveTextFg->c;
	border.setAlphaF(0.35);
	p.setPen(QPen(border, 1.));
	p.setBrush(st::boxBg);
	p.drawRoundedRect(card, radius, radius);

	const auto padding = style::ConvertScale(kPadding);
	const auto iconSize = style::ConvertScale(kIconSize);
	const auto iconTarget = QRectF(
		card.left() + padding,
		card.top() + (card.height() - iconSize) / 2.,
		iconSize,
		iconSize);
	if (!_icon.isNull()) {
		p.drawImage(iconTarget, _icon);
	}

	const auto textLeft = iconTarget.right() + style::ConvertScale(kTextSkip);
	const auto textWidth = card.right() - padding - textLeft;
	const auto titleHeight = st::semiboldFont->height;
	const auto subtitleHeight = st::normalFont->height;
	const auto textSkip = style::ConvertScale(3);
	const auto total = titleHeight + subtitleHeight + textSkip;
	auto textTop = card.top() + (card.height() - total) / 2.;

	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawText(
		QRectF(textLeft, textTop, textWidth, titleHeight),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::semiboldFont->elided(_title, int(textWidth)));
	textTop += titleHeight + textSkip;
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRectF(textLeft, textTop, textWidth, subtitleHeight),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::normalFont->elided(_subtitle, int(textWidth)));
}

void BadgePopup::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		startHide();
		e->accept();
		return;
	}
	Ui::RpWidget::mouseReleaseEvent(e);
}

bool BadgePopup::eventFilter(QObject *object, QEvent *event) {
	if (object == parentWidget() && event->type() == QEvent::Resize) {
		updatePosition();
	}
	return Ui::RpWidget::eventFilter(object, event);
}

QPointer<BadgePopup> CurrentPopup;

} // namespace

void ShowClientBadgePopup(QWidget *parent, not_null<PeerData*> peer) {
	if (!parent) {
		return;
	}
	const auto window = parent->window();
	if (!window) {
		return;
	}
	const auto dpr = std::max(1, int(std::ceil(window->devicePixelRatioF())));
	const auto icon = ClientBadgeFor(&peer->session()).badgePreview(
		st::boxBg->c,
		style::ConvertScale(kIconSize),
		dpr);
	if (icon.isNull()) {
		return;
	}
	if (CurrentPopup) {
		CurrentPopup->deleteLater();
		CurrentPopup = nullptr;
	}
	const auto popup = new BadgePopup(window, icon);
	CurrentPopup = popup;
	popup->startShow();
}

} // namespace Custom
