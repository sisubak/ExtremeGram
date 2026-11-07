/*
This file is part of Yurigram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history.h"
#include "history/history_item.h"
#include "rpl/producer.h"

#include <optional>
#include <vector>

namespace Data {
class Session;
} // namespace Data

namespace MessageExtraState {

enum class HiddenSource : uchar {
	Manual = 0x01,
	BlockedPeer = 0x02,
};

struct HiddenScope {
	MsgId repliesRootId = 0;
	PeerId monoforumPeerId = 0;

	[[nodiscard]] bool empty() const {
		return !repliesRootId && !monoforumPeerId;
	}
	explicit operator bool() const {
		return !empty();
	}
	friend inline constexpr auto operator<=>(HiddenScope, HiddenScope)
		= default;
	friend inline constexpr bool operator==(HiddenScope, HiddenScope)
		= default;
};

struct UnhiddenMessages {
	PeerId peerId;
	std::vector<MsgId> ids;
};

[[nodiscard]] bool hide(
	PeerId peerId,
	MsgId messageId,
	HiddenSource source,
	PeerId blockedPeerId = PeerId(),
	std::optional<HiddenScope> scope = std::nullopt);
[[nodiscard]] bool hide(
	not_null<HistoryItem*> item,
	HiddenSource source = HiddenSource::Manual,
	std::optional<HiddenScope> scope = std::nullopt);
[[nodiscard]] bool isHidden(PeerId peerId, MsgId messageId);
[[nodiscard]] bool isHidden(not_null<HistoryItem*> item);
[[nodiscard]] int hiddenCount(
	PeerId peerId,
	HiddenScope scope,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] int hiddenCount(
	PeerId peerId,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] std::vector<MsgId> hiddenMessageIds(
	PeerId peerId,
	HiddenScope scope,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] std::vector<MsgId> hiddenMessageIds(
	PeerId peerId,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] std::vector<MsgId> unhide(
	PeerId peerId,
	const std::vector<MsgId> &ids,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] std::vector<MsgId> unhideByBlockedPeer(
	PeerId peerId,
	PeerId blockedPeerId);
[[nodiscard]] std::vector<MsgId> unhideAll(
	PeerId peerId,
	HiddenScope scope,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] std::vector<MsgId> unhideAll(
	PeerId peerId,
	std::optional<HiddenSource> source = std::nullopt);
[[nodiscard]] std::vector<UnhiddenMessages> unhideAll(HiddenSource source);
void hideMessages(
	not_null<Data::Session*> owner,
	const MessageIdsList &ids,
	HiddenSource source = HiddenSource::Manual,
	std::optional<HiddenScope> scope = std::nullopt);
[[nodiscard]] bool shouldHideBlockedMessage(not_null<HistoryItem*> item);
[[nodiscard]] rpl::producer<PeerId> changes();

} // namespace MessageExtraState
