/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/required.h"
#include "data/data_peer_id.h"
#include "rpl/lifetime.h"

#include <memory>

class PeerData;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Custom {

class EternalTags final {
public:
	explicit EternalTags(not_null<Main::Session*> session);

	[[nodiscard]] bool has(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const;
	void set(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		const QString &rank);
	void remove(
		not_null<PeerData*> peer,
		not_null<UserData*> user);

private:
	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, base::flat_map<UserId, QString>> _ranks;
	rpl::lifetime _lifetime;

};

[[nodiscard]] EternalTags &EternalTagsFor(not_null<Main::Session*> session);

} // namespace Custom
