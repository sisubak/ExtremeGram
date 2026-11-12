/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/autistic/autistic_mode.h"

namespace Autistic {
namespace {

rpl::event_stream<> &RelayoutStream() {
	static rpl::event_stream<> stream;
	return stream;
}

} // namespace

bool masterOn() {
	return EnhancedSettings::Get(EnhancedSettings::Option::AutisticTelegram);
}

bool on(EnhancedSettings::Key<bool> key) {
	return masterOn() && EnhancedSettings::Get(key);
}

rpl::producer<bool> onStream(EnhancedSettings::Key<bool> key) {
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

void CycleHideForeign() {
	const auto next = (static_cast<int>(hideForeign()) + 1) % 4;
	EnhancedSettings::Set(
		EnhancedSettings::Option::AutisticHideForeign,
		next);
	RelayoutStream().fire({});
}

rpl::producer<> RelayoutRequested() {
	return RelayoutStream().events();
}

void ToggleMaster(not_null<Window::SessionController*> controller) {
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
