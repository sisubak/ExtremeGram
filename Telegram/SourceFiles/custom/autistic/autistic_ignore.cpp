/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/autistic/autistic_ignore.h"

#include "custom/autistic/autistic_mode.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <QStringList>

namespace Autistic {
namespace {

constexpr auto kIgnoredKey = "extremegram.autistic.ignored";

QStringList IgnoredIds(Main::Session &session) {
	return session.local().readPref<QString>(kIgnoredKey).split(
		QLatin1Char(','),
		Qt::SkipEmptyParts);
}

QString IgnoredId(not_null<PeerData*> peer) {
	return QString::number(SerializePeerId(peer->id));
}

} // namespace

bool IsIgnored(not_null<PeerData*> peer) {
	return IgnoredIds(peer->session()).contains(IgnoredId(peer));
}

void IgnorePeer(not_null<PeerData*> peer) {
	auto &session = peer->session();
	auto ids = IgnoredIds(session);
	const auto id = IgnoredId(peer);
	if (!ids.contains(id)) {
		ids.push_back(id);
		session.local().writePref<QString>(
			kIgnoredKey,
			ids.join(QLatin1Char(',')));
	}
	Autistic::RequestRelayout();
}

void UnignorePeer(not_null<PeerData*> peer) {
	auto &session = peer->session();
	auto ids = IgnoredIds(session);
	if (!ids.removeOne(IgnoredId(peer))) {
		return;
	}
	session.local().writePref<QString>(
		kIgnoredKey,
		ids.join(QLatin1Char(',')));
	Autistic::RequestRelayout();
}

QString IgnoredListText(not_null<Main::Session*> session) {
	const auto ids = IgnoredIds(*session);
	if (ids.isEmpty()) {
		return QString::fromUtf8("Игнор-лист пуст.");
	}
	auto names = QStringList();
	for (const auto &text : ids) {
		auto ok = false;
		const auto serialized = text.toULongLong(&ok);
		if (!ok) {
			continue;
		}
		if (const auto peer = session->data().peerLoaded(
				DeserializePeerId(serialized))) {
			names.push_back(peer->name());
		}
	}
	if (names.isEmpty()) {
		return QString::fromUtf8(
			"Игнор-лист пуст (участники ещё не загружены).");
	}
	return QString::fromUtf8("Игнор-лист (%1): %2")
		.arg(names.size())
		.arg(names.join(u", "_q));
}

int IgnoredCount(not_null<Main::Session*> session) {
	return IgnoredIds(*session).size();
}

} // namespace Autistic
