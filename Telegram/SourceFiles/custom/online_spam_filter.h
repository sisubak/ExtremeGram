/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flat_map.h"
#include "data/data_peer_id.h"

#include <vector>

class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Custom {

class OnlineSpamFilter final {
public:
	explicit OnlineSpamFilter(not_null<Main::Session*> session);

	void noteLastseen(
		not_null<UserData*> user,
		bool wasOnline,
		bool nowOnline);

	[[nodiscard]] bool fakingOnline(not_null<UserData*> user) const;

private:
	struct Entry {
		std::vector<TimeId> flips;
		TimeId activeUntil = 0;
		bool active = false;
	};

	void onTick();

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, Entry> _entries;
	base::Timer _tickTimer;
	rpl::lifetime _lifetime;
};

void NoteOnlineFlip(
	not_null<UserData*> user,
	bool wasOnline,
	bool nowOnline);

[[nodiscard]] bool OnlineSpamFaked(not_null<UserData*> user);

[[nodiscard]] OnlineSpamFilter &OnlineSpamFor(not_null<Main::Session*> session);

} // namespace Custom
