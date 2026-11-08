/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/eternal_tags.h"

#include "boxes/peers/edit_participants_box.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Custom {

EternalTags::EternalTags(not_null<Main::Session*> session)
: _session(session) {
	_session->changes().chatMemberRankChanges(
	) | rpl::on_next([=](const Data::ChatMemberRankChange &change) {
		const auto i = _ranks.find(change.peer->id);
		if (i == end(_ranks)) {
			return;
		}
		const auto j = i->second.find(change.user->id);
		if (j == end(i->second) || j->second == change.rank) {
			return;
		}
		SaveMemberRank(
			nullptr,
			change.peer,
			change.user,
			j->second,
			nullptr,
			nullptr);
	}, _lifetime);
}

bool EternalTags::has(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const {
	const auto i = _ranks.find(peer->id);
	if (i == end(_ranks)) {
		return false;
	}
	return i->second.find(user->id) != end(i->second);
}

void EternalTags::set(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		const QString &rank) {
	_ranks[peer->id][user->id] = rank;
}

void EternalTags::remove(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _ranks.find(peer->id);
	if (i == end(_ranks)) {
		return;
	}
	i->second.remove(user->id);
	if (i->second.empty()) {
		_ranks.remove(peer->id);
	}
}

EternalTags &EternalTagsFor(not_null<Main::Session*> session) {
	static base::flat_map<not_null<Main::Session*>, std::unique_ptr<EternalTags>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<EternalTags>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
