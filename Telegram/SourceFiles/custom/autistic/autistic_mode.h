/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "core/enhanced_settings.h"
#include "rpl/rpl.h"

namespace Autistic {

enum class HideForeign : int {
	Off = 0,
	Blur = 1,
	Invisible = 2,
	Collapsed = 3,
};

[[nodiscard]] bool masterOn();

// Master switch AND the sub-feature.
[[nodiscard]] bool on(EnhancedSettings::Key<bool> key);
[[nodiscard]] rpl::producer<bool> onStream(EnhancedSettings::Key<bool> key);

// Valid while the mode is on; clamps to 0..3.
[[nodiscard]] HideForeign hideForeign();
void CycleHideForeign();

// Fires after the master switch or the hide-foreign mode changes: the
// history view must fully re-layout.
[[nodiscard]] rpl::producer<> RelayoutRequested();

} // namespace Autistic
