/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/autistic/autistic_mode.h"

#include "core/msg_extra_state.h"
#include "custom/autistic/autistic_ignore.h"
#include "history/history_item.h"

namespace Autistic {
namespace {

rpl::event_stream<> &RelayoutStream() {
	static rpl::event_stream<> stream;
	return stream;
}

// Any relevant option change (from the panel, the settings section or a
// shortcut) must re-layout the history view.
rpl::lifetime &RelayoutSubscription() {
	static rpl::lifetime lifetime;
	return lifetime;
}

void SubscribeRelayout() {
	auto &lifetime = RelayoutSubscription();
	namespace Option = EnhancedSettings::Option;
	const auto watchBool = [&](EnhancedSettings::Key<bool> key) {
		EnhancedSettings::Changes(key) | rpl::on_next([=] {
			RelayoutStream().fire({});
		}, lifetime);
	};
	watchBool(Option::AutisticTelegram);
	watchBool(Option::AutisticIgnore);
	watchBool(Option::AutisticGhosts);
	watchBool(Option::AutisticShowOrig);
	watchBool(Option::AutisticLayoutFix);
	EnhancedSettings::Changes(
		Option::AutisticHideForeign
	) | rpl::on_next([=] {
		RelayoutStream().fire({});
	}, lifetime);
}

} // namespace

bool masterOn() {
	return EnhancedSettings::Get(EnhancedSettings::Option::AutisticTelegram);
}

bool on(EnhancedSettings::Key<bool> key) {
	return masterOn() && EnhancedSettings::Get(key);
}

rpl::producer<bool> onStream(EnhancedSettings::Key<bool> key) {
	SubscribeRelayout();
	return rpl::combine(
		EnhancedSettings::Watch(EnhancedSettings::Option::AutisticTelegram),
		EnhancedSettings::Watch(key)
	) | rpl::map([=](bool master, bool sub) {
		return master && sub;
	});
}

HideForeign hideForeign() {
	const auto raw = EnhancedSettings::Get(
		EnhancedSettings::Option::AutisticHideForeign);
	return HideForeign(std::clamp(raw, 0, 3));
}

HideForeign ForeignModeFor(not_null<HistoryItem*> item) {
	const auto mode = masterOn() ? hideForeign() : HideForeign::Off;
	if (mode == HideForeign::Off || item->out() || item->isService()) {
		return HideForeign::Off;
	}
	// Ghosts and ignore-collapsing own the rendering first.
	if (MessageExtraState::IsGhosted(item) || ShouldCollapseIgnored(item)) {
		return HideForeign::Off;
	}
	return mode;
}

void CycleHideForeign() {
	SubscribeRelayout();
	const auto next = (static_cast<int>(hideForeign()) + 1) % 4;
	EnhancedSettings::Set(
		EnhancedSettings::Option::AutisticHideForeign,
		next);
	RelayoutStream().fire({});
}

rpl::producer<> RelayoutRequested() {
	SubscribeRelayout();
	return RelayoutStream().events();
}

void RequestRelayout() {
	SubscribeRelayout();
	RelayoutStream().fire({});
}

void ToggleMaster(not_null<Window::SessionController*> controller) {
	SubscribeRelayout();
	const auto next = !masterOn();
	EnhancedSettings::ApplyOption(
		controller,
		EnhancedSettings::Option::AutisticTelegram,
		next);
	if (next && EnhancedSettings::Get(
			EnhancedSettings::Option::OnlineSpamInterval) <= 1000) {
		EnhancedSettings::Set(
			EnhancedSettings::Option::OnlineSpamInterval,
			4000);
	} else if (!next && EnhancedSettings::Get(
			EnhancedSettings::Option::OnlineSpamInterval) == 4000) {
		EnhancedSettings::Set(
			EnhancedSettings::Option::OnlineSpamInterval,
			10);
	}
	RelayoutStream().fire({});
}

QString HideForeignLabel(HideForeign mode) {
	switch (mode) {
	case HideForeign::Blur:
		return QString::fromUtf8("размытие");
	case HideForeign::Invisible:
		return QString::fromUtf8("невидимые");
	case HideForeign::Collapsed:
		return QString::fromUtf8("схлопнутые");
	default:
		return QString::fromUtf8("выкл");
	}
}

} // namespace Autistic
