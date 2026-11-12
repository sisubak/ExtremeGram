/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

class PeerData;
class QString;

namespace Main {
class Session;
} // namespace Main

namespace Autistic {

// Local ignore list: persisted per session, empty after the peer is removed.
[[nodiscard]] bool IsIgnored(not_null<PeerData*> peer);
void IgnorePeer(not_null<PeerData*> peer);
void UnignorePeer(not_null<PeerData*> peer);

// Human-readable list for the !ignore command reply.
[[nodiscard]] QString IgnoredListText(not_null<Main::Session*> session);
[[nodiscard]] int IgnoredCount(not_null<Main::Session*> session);

} // namespace Autistic
