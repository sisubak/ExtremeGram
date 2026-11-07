/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "base/basic_types.h"
#include <QtCore/QRect>
#include <cmath>
#include <algorithm>

class QPainter;
class HistoryItem;
namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Custom::Animations {

[[nodiscard]] inline float64 SpringSmooth(float64 delta, float64 dt) {
	constexpr auto kDamping = 0.82;
	constexpr auto kOmega = 14.0;
	const auto clamped = std::clamp(dt, 0.0, 1.0);
	const auto decay = std::exp(-kDamping * kOmega * clamped);
	const auto freq = kOmega * std::sqrt(1.0 - kDamping * kDamping);
	const auto progress = 1.0 - decay * (std::cos(freq * clamped)
		+ (kDamping * kOmega / freq) * std::sin(freq * clamped));
	return delta * std::clamp(progress, 0.0, 1.0);
}

[[nodiscard]] inline float64 QuinticOutImpl(float64 dt) {
	const auto inv = 1.0 - std::clamp(dt, 0.0, 1.0);
	return 1.0 - (inv * inv * inv * inv * inv);
}

[[nodiscard]] inline float64 CubicSmoothOut(float64 delta, float64 dt) {
	return delta * QuinticOutImpl(dt);
}

[[nodiscard]] inline float64 SoftBackOut(float64 delta, float64 dt) {
	constexpr auto c1 = 1.70158;
	constexpr auto c3 = c1 + 1.0;
	const auto t = std::clamp(dt, 0.0, 1.0) - 1.0;
	return delta * (1.0 + c3 * t * t * t + c1 * t * t);
}

struct PaintTransform {
	float64 opacity = 1.0;
	float64 translateY = 0.0;
	float64 scale = 1.0;
};

[[nodiscard]] inline PaintTransform ComputeMessageAppear(float64 rawProgress) {
	const auto eased = CubicSmoothOut(1.0, rawProgress);
	const auto spring = SpringSmooth(1.0, rawProgress);
	return PaintTransform{
		.opacity = std::clamp(eased * 1.15, 0.0, 1.0),
		.translateY = (1.0 - spring) * 14.0,
		.scale = 0.965 + 0.035 * spring,
	};
}

void NotifyMessageAdded(not_null<HistoryItem*> item, Fn<void()> repaint);
void ApplyElementPaintTransform(
	QPainter &p,
	not_null<const HistoryView::Element*> view,
	QRect clipRect,
	Fn<void()> paintBody);

} // namespace Custom::Animations
