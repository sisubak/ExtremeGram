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

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <algorithm>

namespace Custom {
namespace {

constexpr auto kRestoreCooldownMs = crl::time(3000);
constexpr auto kFailBackoffMs = crl::time(30000);

} // namespace

EternalTags::EternalTags(not_null<Main::Session*> session)
: _session(session)
, _deferredTimer([=] { processPendingRestores(); }) {
	loadFromDisk();

	_session->changes().chatMemberRankChanges(
	) | rpl::on_next([=](const Data::ChatMemberRankChange &change) {
		const auto i = _ranks.find(change.peer->id);
		if (i == end(_ranks)) {
			return;
		}
		const auto uid = peerToUser(change.user->id);
		const auto j = i->second.find(uid);
		if (j == end(i->second)) {
			return;
		}
		if (j->second == change.rank) {
			_deferredRestores.remove(Key{ change.peer->id, uid });
			return;
		}
		scheduleRestore(change.peer, change.user, j->second);
	}, _lifetime);
}

void EternalTags::scheduleRestore(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		const QString &desiredRank) {
	const auto uid = peerToUser(user->id);
	const auto key = Key{ peer->id, uid };
	if (_inFlight.contains(key)) {
		_deferredRestores[key] = desiredRank;
		return;
	}
	const auto now = crl::now();
	const auto cd = _cooldownUntil.find(key);
	if (cd != end(_cooldownUntil) && cd->second > now) {
		_deferredRestores[key] = desiredRank;
		const auto delay = std::max(cd->second - now, crl::time(200));
		if (!_deferredTimer.isActive()) {
			_deferredTimer.callOnce(delay);
		}
		return;
	}

	_deferredRestores.remove(key);
	_inFlight.emplace(key);
	_cooldownUntil[key] = now + kRestoreCooldownMs;
	const auto weakSession = base::make_weak(_session.get());
	const auto peerId = peer->id;
	SaveMemberRank(
		nullptr,
		peer,
		user,
		desiredRank,
		[=] {
			if (const auto strong = weakSession.get()) {
				auto &self = EternalTagsFor(strong);
				self._inFlight.remove(key);
				if (self._deferredRestores.contains(key)) {
					self.processPendingRestores();
				}
			}
		},
		[=] {
			if (const auto strong = weakSession.get()) {
				auto &self = EternalTagsFor(strong);
				self._inFlight.remove(key);
				self._deferredRestores.remove(key);
				self._cooldownUntil[Key{ peerId, uid }] = crl::now() + kFailBackoffMs;
			}
		});
}

void EternalTags::processPendingRestores() {
	if (_deferredRestores.empty()) {
		return;
	}
	const auto now = crl::now();
	crl::time nextWait = 0;
	const auto copy = _deferredRestores;
	for (const auto &[key, rank] : copy) {
		const auto i = _ranks.find(key.first);
		if (i == end(_ranks) || !i->second.contains(key.second)) {
			_deferredRestores.remove(key);
			continue;
		}
		const auto cd = _cooldownUntil.find(key);
		if (cd != end(_cooldownUntil) && cd->second > now) {
			const auto rem = cd->second - now;
			nextWait = nextWait ? std::min(nextWait, rem) : rem;
			continue;
		}
		const auto peer = _session->data().peerLoaded(key.first);
		const auto user = _session->data().userLoaded(key.second);
		_deferredRestores.remove(key);
		if (peer && user) {
			scheduleRestore(peer, user, i->second.find(key.second)->second);
		}
	}
	if (nextWait > 0 && !_deferredTimer.isActive()) {
		_deferredTimer.callOnce(std::max(nextWait, crl::time(200)));
	}
}

bool EternalTags::has(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const {
	const auto i = _ranks.find(peer->id);
	if (i == end(_ranks)) {
		return false;
	}
	return i->second.find(peerToUser(user->id)) != end(i->second);
}

void EternalTags::set(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		const QString &rank) {
	const auto uid = peerToUser(user->id);
	_ranks[peer->id][uid] = rank;
	_cooldownUntil.remove(Key{ peer->id, uid });
	saveToDisk();
}

void EternalTags::remove(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto uid = peerToUser(user->id);
	_deferredRestores.remove(Key{ peer->id, uid });
	_cooldownUntil.remove(Key{ peer->id, uid });
	const auto i = _ranks.find(peer->id);
	if (i == end(_ranks)) {
		return;
	}
	i->second.remove(uid);
	if (i->second.empty()) {
		_ranks.remove(peer->id);
	}
	saveToDisk();
}

QString EternalTags::storageFilePath() const {
	const auto dirPath = cWorkingDir() + u"tdata/extremegram"_q;
	QDir().mkpath(dirPath);
	return QDir(dirPath).filePath(
		u"eternal_tags_"_q
		+ QString::number(_session->userId().bare)
		+ u".json"_q);
}

void EternalTags::loadFromDisk() {
	QFile f(storageFilePath());
	if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		return;
	}
	const auto root = doc.object();
	for (auto it = root.begin(); it != root.end(); ++it) {
		bool okPeer = false;
		const auto rawPeer = it.key().toULongLong(&okPeer);
		if (!okPeer || !rawPeer || !it.value().isObject()) {
			continue;
		}
		const auto peerObj = it.value().toObject();
		auto &peerMap = _ranks[PeerId(rawPeer)];
		for (auto uit = peerObj.begin(); uit != peerObj.end(); ++uit) {
			bool okUser = false;
			const auto rawUser = uit.key().toULongLong(&okUser);
			if (okUser && rawUser && uit.value().isString()) {
				peerMap[UserId(rawUser)] = uit.value().toString();
			}
		}
		if (peerMap.empty()) {
			_ranks.remove(PeerId(rawPeer));
		}
	}
}

void EternalTags::saveToDisk() const {
	QJsonObject root;
	for (const auto &[peerId, users] : _ranks) {
		if (users.empty()) {
			continue;
		}
		QJsonObject peerObj;
		for (const auto &[userId, rank] : users) {
			peerObj[QString::number(userId.bare)] = rank;
		}
		root[QString::number(peerId.value)] = peerObj;
	}
	QSaveFile f(storageFilePath());
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
		f.commit();
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
