/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "rpl/rpl.h"

class QColor;

namespace Window {
class SessionController;
} // namespace Window

namespace Custom {

void ApplyRedesignDesign(not_null<Window::SessionController*> controller);

[[nodiscard]] rpl::producer<> TokensChanged();

struct DesignTokens {
	int radiusId = 10;
	float64 cardAlpha = 0.94;
	int accentWashAlpha = 0;
	QColor accent = QColor();
	bool chatListCards = false;
	bool denseChatList = false;
	bool cardProfile = false;
	bool minimalProfile = false;
};

[[nodiscard]] DesignTokens DesignTokensForCurrentMode();

} // namespace Custom
