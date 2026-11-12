/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/client_badge_popup.h"

#include "base/timer.h"
#include "custom/client_badge_detector.h"
#include "custom/redesign.h"
#include "custom/smooth_animations.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "styles/palette.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/shadow.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Custom {
namespace {

constexpr auto kIconSize = 36;
constexpr auto kPadding = 14;
constexpr auto kTextSkip = 12;
constexpr auto kGap = 10;
constexpr auto kShowDuration = crl::time(220);
constexpr auto kHideDuration = crl::time(160);
constexpr auto kHoldDuration = crl::time(2600);
constexpr auto kIconDelay = crl::time(70);
constexpr auto kIconDuration = crl::time(300);
constexpr auto kPulseDuration = crl::time(480);

class BadgePopup final : public Ui::RpWidget {
public:
	BadgePopup(QWidget *parent, QImage icon, QRect anchor);
	~BadgePopup();

	void startShow();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void startIcon();
	void startHide();
	void updatePosition();
	[[nodiscard]] QRect cardGeometry() const;
	[[nodiscard]] QColor accentColor() const;

	QImage _icon;
	QString _title;
	QString _subtitle;
	QRect _anchor;
	Ui::Animations::Simple _animation;
	Ui::Animations::Simple _iconAnimation;
	Ui::Animations::Simple _pulseAnimation;
	base::Timer _iconDelay;
	base::Timer _holdTimer;
	float64 _progress = 0.;
	float64 _iconProgress = 0.;
	float64 _pulseProgress = 0.;
	bool _hiding = false;

};

BadgePopup::BadgePopup(QWidget *parent, QImage icon, QRect anchor)
: Ui::RpWidget(parent)
, _icon(std::move(icon))
, _anchor(anchor) {
	setCursor(style::cur_pointer);
	setAttribute(Qt::WA_ShowWithoutActivating);

	_title = u"ExtremeGram"_q;
	_subtitle = tr::lng_extremegram_badge_clicked(tr::now);

	const auto iconSize = style::ConvertScale(kIconSize);
	const auto padding = style::ConvertScale(kPadding);
	const auto textSkip = style::ConvertScale(kTextSkip);
	const auto shadow = st::boxRoundShadow.extend;
	const auto textWidth = std::max(
		st::semiboldFont->width(_title),
		st::normalFont->width(_subtitle));
	const auto maxCard = std::max(
		style::ConvertScale(280),
		std::min(
			style::ConvertScale(430),
			parent->width()
				- shadow.left()
				- shadow.right()
				- style::ConvertScale(24)));
	const auto cardWidth = std::clamp(
		padding + iconSize + textSkip + textWidth + padding,
		style::ConvertScale(280),
		maxCard);
	const auto cardHeight = padding + iconSize + padding;
	resize(
		cardWidth + shadow.left() + shadow.right(),
		cardHeight + shadow.top() + shadow.bottom());
	updatePosition();

	parent->installEventFilter(this);
}

BadgePopup::~BadgePopup() {
	if (const auto parent = parentWidget()) {
		parent->removeEventFilter(this);
	}
}

void BadgePopup::startShow() {
	show();
	raise();
	_animation.start([=](float64 value) {
		_progress = value;
		update();
	}, 0., 1., kShowDuration, anim::transition(
		Custom::Animations::CubicSmoothOut));
	if (anim::Disabled()) {
		startIcon();
	} else {
		_iconDelay.setCallback([=] { startIcon(); });
		_iconDelay.callOnce(kIconDelay);
	}
	_holdTimer.setCallback([=] { startHide(); });
	_holdTimer.callOnce(kHoldDuration + kShowDuration);
}

void BadgePopup::startIcon() {
	_iconAnimation.start([=](float64 value) {
		_iconProgress = value;
		update();
	}, 0., 1., kIconDuration, anim::transition(
		Custom::Animations::SpringSmooth));
	_pulseAnimation.start([=](float64 value) {
		_pulseProgress = value;
		update();
	}, 0., 1., kPulseDuration, anim::linear);
}

void BadgePopup::startHide() {
	if (_hiding) {
		return;
	}
	_hiding = true;
	_holdTimer.cancel();
	_iconDelay.cancel();
	_animation.start([=](float64 value) {
		_progress = value;
		update();
		if (value <= 0.) {
			deleteLater();
		}
	}, _progress, 0., kHideDuration, anim::transition(
		Custom::Animations::CubicSmoothOut));
}

QRect BadgePopup::cardGeometry() const {
	const auto shadow = st::boxRoundShadow.extend;
	return QRect(
		shadow.left(),
		shadow.top(),
		width() - shadow.left() - shadow.right(),
		height() - shadow.top() - shadow.bottom());
}

QColor BadgePopup::accentColor() const {
	const auto accent = Custom::DesignTokensForCurrentMode().accent;
	return accent.isValid() ? accent : st::windowActiveTextFg->c;
}

void BadgePopup::updatePosition() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	const auto shadow = st::boxRoundShadow.extend;
	const auto card = cardGeometry();
	const auto gap = style::ConvertScale(kGap);
	const auto anchor = _anchor.isValid()
		? QRect(parent->mapFromGlobal(_anchor.topLeft()), _anchor.size())
		: QRect(parent->mapFromGlobal(QCursor::pos()), QSize(1, 1));
	auto x = anchor.center().x() - card.width() / 2;
	auto y = anchor.top() - card.height() - gap;
	if (y < 0) {
		y = anchor.bottom() + gap;
	}
	const auto maxX = std::max(
		shadow.left(),
		parent->width() - shadow.right() - card.width());
	const auto maxY = std::max(
		shadow.top(),
		parent->height() - shadow.bottom() - card.height());
	x = std::max(shadow.left(), std::min(x, maxX));
	y = std::max(shadow.top(), std::min(y, maxY));
	move(x - shadow.left(), y - shadow.top());
}

void BadgePopup::paintEvent(QPaintEvent *) {
	const auto progress = std::clamp(_progress, 0., 1.);
	if (progress <= 0.) {
		return;
	}

	auto p = QPainter(this);
	p.setOpacity(progress);

	const auto card = cardGeometry();
	const auto radius = style::ConvertScale(
		Custom::DesignTokensForCurrentMode().radiusId);

	const auto scale = 0.94 + 0.06 * progress;
	p.translate(width() / 2., height() / 2.);
	p.scale(scale, scale);
	p.translate(-width() / 2., -height() / 2.);
	p.translate(0., -style::ConvertScale(12) * (1. - progress));

	Ui::Shadow::paint(p, card, width(), st::boxRoundShadow);

	auto border = st::windowActiveTextFg->c;
	border.setAlphaF(0.3);
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

	if (_pulseProgress > 0. && _pulseProgress < 1.) {
		const auto t = _pulseProgress;
		auto ring = accentColor();
		ring.setAlphaF(0.3 * (1. - t));
		p.setPen(QPen(ring, style::ConvertScale(1.5)));
		p.setBrush(Qt::NoBrush);
		const auto ringRadius = iconSize * (0.34 + 0.3 * t);
		p.drawEllipse(iconTarget.center(), ringRadius, ringRadius);
	}

	const auto iconProgress = _iconProgress;
	if (iconProgress > 0. && !_icon.isNull()) {
		p.save();
		p.translate(iconTarget.center());
		const auto iconScale = 1.4 - 0.4 * iconProgress;
		p.scale(iconScale, iconScale);
		p.translate(-iconTarget.center());
		p.setOpacity(progress * std::clamp(iconProgress, 0., 1.));
		p.drawImage(iconTarget, _icon);
		p.restore();
	}

	const auto textLeft = iconTarget.right() + style::ConvertScale(kTextSkip);
	const auto textWidth = card.right() - padding - textLeft;
	const auto titleHeight = st::semiboldFont->height;
	const auto subtitleHeight = st::normalFont->height;
	const auto textSkip = style::ConvertScale(3);
	const auto total = titleHeight + subtitleHeight + textSkip;
	auto textTop = card.top() + (card.height() - total) / 2.;

	p.setFont(st::semiboldFont);
	p.setPen(accentColor());
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

void ShowClientBadgePopup(
		QWidget *parent,
		not_null<PeerData*> peer,
		const QRect &anchor) {
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
	const auto popup = new BadgePopup(window, icon, anchor);
	CurrentPopup = popup;
	popup->startShow();
}

} // namespace Custom
