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

class QString;

namespace Window {
class SessionController;
} // namespace Window

namespace Autistic {

enum class HideForeign : int {
	Off = 0,
	Blur = 1,
	Invisible = 2,
	Collapsed = 3,
};

class HistoryItem;

[[nodiscard]] bool masterOn();

// Master switch AND the sub-feature.
[[nodiscard]] bool on(EnhancedSettings::Key<bool> key);
[[nodiscard]] rpl::producer<bool> onStream(EnhancedSettings::Key<bool> key);

// Rendering mode for a foreign message in the current chat. Off when the
// feature is off, the item is own/service, or when ghosts / ignore-
// collapsing own the rendering (they have priority).
[[nodiscard]] HideForeign ForeignModeFor(not_null<HistoryItem*> item);

// Valid while the mode is on; clamps to 0..3.
[[nodiscard]] HideForeign hideForeign();
void CycleHideForeign();

// Fires after the master switch or the hide-foreign mode changes: the
// history view must fully re-layout.
[[nodiscard]] rpl::producer<> RelayoutRequested();

// Force the same re-layout from any other visibility-changing feature
// (e.g. the ignore list).
void RequestRelayout();

// Toggles the whole mode. On enable, if the OnlineSpam interval is at its
// unconfigured default, sets it to 4000 ms; on disable restores 10 ms.
void ToggleMaster(not_null<Window::SessionController*> controller);

[[nodiscard]] QString HideForeignLabel(HideForeign mode);

} // namespace Autistic
