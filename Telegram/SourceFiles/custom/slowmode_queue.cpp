/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/slowmode_queue.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/enhanced_settings.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "main/main_session.h"
#include "ui/item_text_options.h"
#include "ui/text/text_utilities.h"

namespace Custom {

SlowmodeQueue::SlowmodeQueue(not_null<Main::Session*> session)
: _session(session)
, _tickTimer([=] { onTick(); }) {
}

bool SlowmodeQueue::intercept(const Api::MessageToSend &message) {
	if (!EnhancedSettings::Get(EnhancedSettings::Option::AntiSlowmode)) {
		return false;
	}
	const auto history = message.action.history;
	if (!history) {
		return false;
	}
	const auto peer = history->peer;
	if (_sendingPeer == peer->id) {
		return false;
	}
	if (!peer->slowmodeApplied()) {
		return false;
	}
	if (peer->slowmodeSecondsLeft() <= 0
		&& !history->latestSendingMessage()) {
		return false;
	}
	if (message.action.options.scheduled
		|| message.action.options.shortcutId
		|| peer->starsPerMessageChecked()) {
		return false;
	}
	auto text = TextWithEntities{
		message.textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(message.textWithTags.tags),
	};
	TextUtilities::PrepareForSending(
		text,
		Ui::ItemTextOptions(history, _session->user()).flags);
	auto flags = NewMessageFlags(peer);
	if (message.action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	const auto placeholderId = FullMsgId(
		peer->id,
		_session->data().nextLocalMessageId());
	history->addNewLocalMessage(
		HistoryItemCommonFields{
			.id = placeholderId.msg,
			.flags = flags,
			.from = NewMessageFromId(message.action),
			.replyTo = message.action.replyTo,
			.date = NewMessageDate(message.action.options),
			.postAuthor = NewMessagePostAuthor(message.action),
		},
		text,
		MTP_messageMediaEmpty());
	auto &queue = _queues[peer->id];
	queue.push_back({ message, placeholderId });
	if (!_tickTimer.isActive()) {
		_tickTimer.callEach(1000);
	}
	return true;
}

void SlowmodeQueue::onTick() {
	for (auto it = _queues.begin(); it != _queues.end();) {
		auto &queue = it->second;
		while (!queue.empty() && ready(it->first)) {
			auto queued = std::move(queue.front());
			queue.erase(queue.begin());
			send(std::move(queued));
		}
		if (queue.empty()) {
			it = _queues.erase(it);
		} else {
			++it;
		}
	}
	if (_queues.empty()) {
		_tickTimer.cancel();
	}
}

bool SlowmodeQueue::ready(PeerId peerId) const {
	const auto peer = _session->data().peerLoaded(peerId);
	const auto history = _session->data().historyLoaded(peerId);
	if (!peer || !history) {
		return false;
	}
	return !peer->slowmodeSecondsLeft() && !history->latestSendingMessage();
}

void SlowmodeQueue::send(Queued &&queued) {
	const auto peerId = queued.message.action.history->peer->id;
	if (const auto placeholder = _session->data().message(
			queued.placeholderId)) {
		placeholder->destroy();
	}
	_sendingPeer = peerId;
	_session->api().sendMessage(
		std::move(queued.message),
		queued.placeholderId.msg);
	_sendingPeer = 0;
}

SlowmodeQueue &SlowmodeQueueFor(not_null<Main::Session*> session) {
	static base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<SlowmodeQueue>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<SlowmodeQueue>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
