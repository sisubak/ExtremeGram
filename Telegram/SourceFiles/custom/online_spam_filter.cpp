/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/online_spam_filter.h"

#include "base/unixtime.h"
#include "core/enhanced_settings.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Custom {
namespace {

constexpr TimeId kFlipWindow = 90;
constexpr int kFlipThreshold = 4;
constexpr TimeId kQuietRelease = 150;

} // namespace

OnlineSpamFilter::OnlineSpamFilter(not_null<Main::Session*> session)
: _session(session)
, _tickTimer([=] { onTick(); }) {
}

void OnlineSpamFilter::noteLastseen(
		not_null<UserData*> user,
		bool wasOnline,
		bool nowOnline) {
	if (user->isBot() || user->isServiceUser()) {
		return;
	}
	if (!EnhancedSettings::Get(EnhancedSettings::Option::AntiOnlineSpam)) {
		return;
	}
	const auto now = base::unixtime::now();
	auto &entry = _entries[user->id];
	if (wasOnline == nowOnline) {
		return;
	}
	entry.flips.push_back(now);
	while (!entry.flips.empty() && entry.flips.front() <= now - kFlipWindow) {
		entry.flips.erase(entry.flips.begin());
	}
	if (entry.active) {
		entry.activeUntil = now + kQuietRelease;
		return;
	}
	if (static_cast<int>(entry.flips.size()) < kFlipThreshold) {
		return;
	}
	entry.active = true;
	entry.activeUntil = now + kQuietRelease;
	if (!_tickTimer.isActive()) {
		_tickTimer.callEach(1000);
	}
	_session->changes().peerUpdated(
		user,
		Data::PeerUpdate::Flag::OnlineStatus);
}

void OnlineSpamFilter::onTick() {
	const auto now = base::unixtime::now();
	for (auto it = _entries.begin(); it != _entries.end();) {
		auto &entry = it->second;
		if (entry.active && now >= entry.activeUntil) {
			entry.active = false;
			if (const auto user = _session->data().userLoaded(
					peerToUser(it->first))) {
				_session->changes().peerUpdated(
					user,
					Data::PeerUpdate::Flag::OnlineStatus);
			}
		}
		if (!entry.active) {
			const auto last = entry.flips.empty() ? TimeId(0) : entry.flips.back();
			if (last <= now - kFlipWindow) {
				it = _entries.erase(it);
				continue;
			}
		}
		++it;
	}
	if (_entries.empty()) {
		_tickTimer.cancel();
	}
}

bool OnlineSpamFilter::fakingOnline(not_null<UserData*> user) const {
	const auto i = _entries.find(user->id);
	if (i == _entries.end()) {
		return false;
	}
	return i->second.active && (i->second.activeUntil > base::unixtime::now());
}

void NoteOnlineFlip(
		not_null<UserData*> user,
		bool wasOnline,
		bool nowOnline) {
	OnlineSpamFor(&user->session()).noteLastseen(
		user,
		wasOnline,
		nowOnline);
}

bool OnlineSpamFaked(not_null<UserData*> user) {
	return OnlineSpamFor(&user->session()).fakingOnline(user);
}

OnlineSpamFilter &OnlineSpamFor(not_null<Main::Session*> session) {
	static base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<OnlineSpamFilter>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<OnlineSpamFilter>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
