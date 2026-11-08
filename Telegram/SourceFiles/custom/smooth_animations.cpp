/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/smooth_animations.h"

#include "core/enhanced_settings.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"

namespace Custom::Animations {
namespace {

constexpr auto kAppearDurationMs = crl::time(420);

struct MessageAppearState {
	Ui::Animations::Simple progress;
	bool finished = false;
};

class AppearRegistry final {
public:
	void registerNewItem(FullMsgId itemId, Fn<void()> repaintCallback) {
		if (!EnhancedSettings::Get(
				EnhancedSettings::Option::SmoothInterfaceAnimations)) {
			return;
		}
		auto &entry = _states[itemId];
		if (entry.finished || entry.progress.animating()) {
			return;
		}
		entry.progress.start(
			[=, this] {
				if (repaintCallback) {
					repaintCallback();
				}
				const auto it = _states.find(itemId);
				if (it != _states.end() && !it->second.progress.animating()) {
					it->second.finished = true;
				}
			},
			0.0,
			1.0,
			kAppearDurationMs,
			CubicSmoothOut);
	}

	[[nodiscard]] std::optional<PaintTransform> currentTransform(
			FullMsgId itemId) {
		if (!EnhancedSettings::Get(
				EnhancedSettings::Option::SmoothInterfaceAnimations)) {
			_states.clear();
			return std::nullopt;
		}
		const auto it = _states.find(itemId);
		if (it == _states.end() || it->second.finished) {
			return std::nullopt;
		}
		const auto val = it->second.progress.value(1.0);
		if (val >= 0.999 && !it->second.progress.animating()) {
			_states.erase(it);
			return std::nullopt;
		}
		return ComputeMessageAppear(val);
	}

private:
	base::flat_map<FullMsgId, MessageAppearState> _states;
};

AppearRegistry &GlobalAppearRegistry() {
	static AppearRegistry instance;
	return instance;
}

} // namespace

void NotifyMessageAdded(not_null<HistoryItem*> item, Fn<void()> repaint) {
	GlobalAppearRegistry().registerNewItem(item->fullId(), std::move(repaint));
}

void ApplyElementPaintTransform(
		QPainter &p,
		not_null<const HistoryView::Element*> view,
		QRect clipRect,
		Fn<void()> paintBody) {
	const auto item = view->data();
	const auto transform = GlobalAppearRegistry().currentTransform(
		item->fullId());
	if (!transform) {
		paintBody();
		return;
	}

	p.save();
	p.setOpacity(p.opacity() * transform->opacity);
	const auto center = QPointF(
		clipRect.left()
			+ (view->hasOutLayout()
				? clipRect.width() * 0.85
				: clipRect.width() * 0.15),
		clipRect.center().y());
	p.translate(0.0, transform->translateY);
	p.translate(center);
	p.scale(transform->scale, transform->scale);
	p.translate(-center);
	paintBody();
	p.restore();
}

} // namespace Custom::Animations
