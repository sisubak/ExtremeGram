/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flat_map.h"
#include "data/data_peer_id.h"

#include <algorithm>
#include <optional>

class QPainter;
class QColor;
class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Custom {

struct SlowmodeState {
	TimeId startedAt = 0;
	TimeId expiresAt = 0;
	int durationSeconds = 0;

	[[nodiscard]] float64 remainingRatio(TimeId now) const {
		if (durationSeconds <= 0 || now >= expiresAt) {
			return 0.0;
		}
		return std::clamp(
			static_cast<float64>(expiresAt - now) / durationSeconds,
			0.0,
			1.0);
	}
};

class SlowmodeTracker final {
public:
	explicit SlowmodeTracker(not_null<Main::Session*> session);

	void registerMessage(not_null<HistoryItem*> item);

	[[nodiscard]] std::optional<SlowmodeState> activeState(
		not_null<PeerData*> chatPeer,
		not_null<PeerData*> senderPeer) const;

	[[nodiscard]] int iconWidth(
		not_null<PeerData*> chatPeer,
		not_null<PeerData*> senderPeer) const;

	int drawIconGetWidth(
		QPainter &p,
		int x,
		int y,
		int nameHeight,
		not_null<PeerData*> chatPeer,
		not_null<PeerData*> senderPeer,
		const QColor &badgeColor) const;

private:
	struct Key {
		PeerId chatId;
		PeerId senderId;
		friend inline constexpr auto operator<=>(Key, Key) = default;
	};

	void onTick();
	void scheduleNextTick();
	void repaintAffectedChat(PeerId chatId);

	const not_null<Main::Session*> _session;
	base::flat_map<Key, SlowmodeState> _active;
	base::Timer _tickTimer;
	rpl::lifetime _lifetime;
};

[[nodiscard]] SlowmodeTracker &SlowmodeFor(not_null<Main::Session*> session);

} // namespace Custom
