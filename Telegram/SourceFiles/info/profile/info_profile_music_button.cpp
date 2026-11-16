/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_music_button.h"

#include "ui/color_contrast.h"
#include "ui/effects/animation_value.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"

namespace Info::Profile {

MusicButton::MusicButton(
	QWidget *parent,
	MusicButtonData data,
	Fn<void()> handler)
: RippleButton(parent, st::infoMusicButtonRipple)
, _noteSymbol(u"\u266B"_q + QChar(' '))
, _noteWidth(st::normalFont->width(_noteSymbol)) {
	updateData(std::move(data));
	setClickedCallback(std::move(handler));
}

MusicButton::~MusicButton() = default;

void MusicButton::updateData(MusicButtonData data) {
	const auto result = data.name.textWithEntities();
	const auto performerLength = result.entities.empty()
		? 0
		: int(result.entities.front().length());
	_performer.setText(
		st::semiboldTextStyle,
		result.text.mid(0, performerLength));
	_title.setText(
		st::defaultTextStyle,
		result.text.mid(performerLength, result.text.size()));
	setAccessibleName(result.text);
	update();
}

QColor MusicButton::currentBg() const {
	const auto fallback = st::shadowFg->c;
	if (!_bgAnimation.animating()) {
		return _overrideBg.value_or(fallback);
	}
	return anim::color(
		_previousBg.value_or(fallback),
		_overrideBg.value_or(fallback),
		_bgAnimation.value(0.));
}

void MusicButton::setOverrideBg(std::optional<QColor> color) {
	if (_overrideBg == color) {
		return;
	}
	_previousBg = currentBg();
	_overrideBg = color;
	_bgAnimation.start([=] {
		update();
	}, 0., 1., crl::time(220));
	update();
}

void MusicButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto color = currentBg();
	p.fillRect(e->rect(), _overrideBg
		? Ui::BlendColors(
			color,
			Qt::black,
			st::infoProfileTopBarActionButtonBgOpacity)
		: color);
	const auto hovered = _overAnimation.value(isOver() ? 1. : 0.);
	if (hovered > 0.) {
		const auto light = _overrideBg
			&& Ui::IsLightBackground(*_overrideBg);
		auto overlay = light ? QColor(Qt::black) : QColor(Qt::white);
		overlay.setAlphaF(0.06 * hovered);
		p.fillRect(e->rect(), overlay);
	}
	const auto lightBackground = _overrideBg
		&& Ui::IsLightBackground(*_overrideBg);
	const auto rippleColor = !_overrideBg
		? std::nullopt
		: std::make_optional(anim::with_alpha(
			(lightBackground
				? QColor(Qt::black)
				: st::groupCallMembersFg->c),
			st::infoProfileTopBarBackdropRippleOpacity));
	paintRipple(p, QPoint(), rippleColor ? &*rippleColor : nullptr);

	const auto &icon = st::topicButtonArrow;
	const auto iconWidth = icon.width();
	const auto iconHeight = icon.height();

	const auto padding = st::infoMusicButtonPadding;
	const auto skip = st::normalFont->spacew;

	const auto titleWidth = _title.maxWidth();
	const auto performerWidth = _performer.maxWidth();
	const auto totalNeeded = titleWidth + performerWidth + skip;
	const auto availableWidth = width()
		- rect::m::sum::h(padding)
		- iconWidth
		- skip
		- _noteWidth;

	auto actualTitleWidth = 0;
	auto actualPerformerWidth = 0;
	if (totalNeeded <= availableWidth) {
		actualTitleWidth = titleWidth;
		actualPerformerWidth = performerWidth;
	} else {
		const auto ratio = float64(titleWidth) / totalNeeded;
		actualPerformerWidth = int(availableWidth * (1.0 - ratio));
		actualTitleWidth = availableWidth - actualPerformerWidth;
	}

	const auto totalContentWidth = _noteWidth
		+ actualPerformerWidth
		+ skip
		+ actualTitleWidth
		+ skip
		+ iconWidth;
	const auto centerX = width() / 2;
	const auto contentStartX = centerX - totalContentWidth / 2;
	const auto textTop = (height() - st::normalFont->height) / 2;

	p.setPen(!_overrideBg
		? st::windowBoldFg->c
		: lightBackground
		? QColor(Qt::black)
		: st::groupCallMembersFg->c);
	p.setFont(st::normalFont);
	p.drawText(contentStartX, textTop + st::normalFont->ascent, _noteSymbol);

	_performer.draw(p, {
		.position = { contentStartX + _noteWidth, textTop },
		.availableWidth = actualPerformerWidth,
		.now = crl::now(),
		.elisionLines = 1,
		.elisionMiddle = true,
	});

	p.setPen(!_overrideBg
		? st::windowSubTextFg->c
		: lightBackground
		? anim::with_alpha(
			QColor(Qt::black),
			st::groupCallVideoSubTextFg->c.alphaF())
		: st::groupCallVideoSubTextFg->c);
	_title.draw(p, {
		.position = QPoint(
			contentStartX + _noteWidth + actualPerformerWidth + skip,
			textTop),
		.availableWidth = actualTitleWidth,
		.now = crl::now(),
		.elisionLines = 1,
		.elisionMiddle = true,
	});

	const auto iconLeft = contentStartX
		+ _noteWidth
		+ actualPerformerWidth
		+ actualTitleWidth
		+ skip
		+ skip;
	const auto iconTop = (height() - iconHeight) / 2;
	icon.paint(p, iconLeft, iconTop, iconWidth, p.pen().color());
}

int MusicButton::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMusicButtonPadding;
	const auto &font = st::defaultTextStyle.font;

	return padding.top() + font->height + padding.bottom();
}

void MusicButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	const auto over = isOver();
	if (over != ((was & StateFlag::Over) != 0)) {
		_overAnimation.start(
			[=] { update(); },
			over ? 0. : 1.,
			over ? 1. : 0.,
			crl::time(160));
	}
}

} // namespace Info::Profile
