/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/mention_pins.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "main/main_session.h"

#include <algorithm>

namespace Custom {
namespace {

const auto kEmpty = std::vector<UserId>();

[[nodiscard]] auto Find(
		std::vector<UserId> &list,
		UserId id) {
	return std::find(list.begin(), list.end(), id);
}

} // namespace

MentionPins::MentionPins(not_null<Main::Session*> session)
: _session(session) {
}

const std::vector<UserId> &MentionPins::list(
		not_null<PeerData*> peer) const {
	const auto i = _pins.find(peer->id);
	return (i != end(_pins)) ? i->second : kEmpty;
}

bool MentionPins::has(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return false;
	}
	const auto &users = i->second;
	const auto uid = peerToUser(user->id);
	return std::find(users.begin(), users.end(), uid) != users.end();
}

void MentionPins::pin(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	auto &users = _pins[peer->id];
	const auto uid = peerToUser(user->id);
	if (Find(users, uid) == users.end()) {
		users.insert(users.begin(), uid);
	}
}

void MentionPins::unpin(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return;
	}
	auto &users = i->second;
	const auto j = Find(users, peerToUser(user->id));
	if (j != users.end()) {
		users.erase(j);
	}
	if (users.empty()) {
		_pins.remove(peer->id);
	}
}

void MentionPins::moveUp(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return;
	}
	auto &users = i->second;
	const auto j = Find(users, peerToUser(user->id));
	if (j != users.end() && j != users.begin()) {
		std::iter_swap(j, j - 1);
	}
}

void MentionPins::moveDown(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return;
	}
	auto &users = i->second;
	const auto j = Find(users, peerToUser(user->id));
	if (j != users.end() && j + 1 != users.end()) {
		std::iter_swap(j, j + 1);
	}
}

MentionPins &MentionPinsFor(not_null<Main::Session*> session) {
	static base::flat_map<not_null<Main::Session*>, std::unique_ptr<MentionPins>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<MentionPins>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
