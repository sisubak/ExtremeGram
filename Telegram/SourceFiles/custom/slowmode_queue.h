/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/timer.h"
#include "base/flat_map.h"
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"

#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Custom {

class SlowmodeQueue final {
public:
	explicit SlowmodeQueue(not_null<Main::Session*> session);

	[[nodiscard]] bool intercept(const Api::MessageToSend &message);

private:
	struct Queued {
		Api::MessageToSend message;
		FullMsgId placeholderId;
	};

	void onTick();
	[[nodiscard]] bool ready(PeerId peerId) const;
	void send(Queued &&queued);

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, std::vector<Queued>> _queues;
	PeerId _sendingPeer = 0;
	base::Timer _tickTimer;
	rpl::lifetime _lifetime;
};

[[nodiscard]] SlowmodeQueue &SlowmodeQueueFor(
	not_null<Main::Session*> session);

} // namespace Custom
