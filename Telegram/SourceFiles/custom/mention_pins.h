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
#include "ui/text/text_entity.h"

#include <memory>
#include <vector>

class PeerData;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Custom {

class MentionPins final {
public:
	explicit MentionPins(not_null<Main::Session*> session);

	[[nodiscard]] const std::vector<UserId> &list(
		not_null<PeerData*> peer) const;
	[[nodiscard]] bool has(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const;
	[[nodiscard]] bool canMoveUp(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const;
	[[nodiscard]] bool canMoveDown(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const;
	void pin(
		not_null<PeerData*> peer,
		not_null<UserData*> user);
	void unpin(
		not_null<PeerData*> peer,
		not_null<UserData*> user);
	void moveUp(
		not_null<PeerData*> peer,
		not_null<UserData*> user);
	void moveDown(
		not_null<PeerData*> peer,
		not_null<UserData*> user);

private:
	[[nodiscard]] QString storageFilePath() const;
	void loadFromDisk();
	void saveToDisk() const;

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, std::vector<UserId>> _pins;
	rpl::lifetime _lifetime;

};

[[nodiscard]] MentionPins &MentionPinsFor(not_null<Main::Session*> session);

[[nodiscard]] bool LooksLikeMarkdown(const QString &text);
[[nodiscard]] TextWithTags ApplyMentionNames(
	not_null<Main::Session*> session,
	TextWithTags text);

} // namespace Custom
