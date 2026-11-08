/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"
#include "data/data_chat_participant_status.h"

class ChannelData;
class UserData;

namespace Window {
class SessionController;
} // namespace Window

void ShowExtremeGramMuteBox(
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel,
	not_null<UserData*> user,
	ChatRestrictionsInfo oldRights);
