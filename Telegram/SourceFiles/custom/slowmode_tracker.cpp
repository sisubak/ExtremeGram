/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/slowmode_tracker.h"

#include "base/flat_set.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "styles/style_basic.h"
#include "ui/painter.h"

#include <cmath>

namespace Custom {
namespace {

[[nodiscard]] bool IsSlowmodeExempt(
		not_null<ChannelData*> channel,
		not_null<PeerData*> sender) {
	if (sender->isSelf() && (channel->amCreator() || channel->hasAdminRights())) {
		return true;
	}
	if (const auto mgInfo = channel->mgInfo.get()) {
		if (const auto user = sender->asUser()) {
			const auto userId = peerToUser(user->id);
			if (mgInfo->creator == user || mgInfo->admins.contains(userId)) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

SlowmodeTracker::SlowmodeTracker(not_null<Main::Session*> session)
: _session(session)
, _tickTimer([=] { onTick(); }) {
	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		registerMessage(item);
	}, _lifetime);
}

void SlowmodeTracker::registerMessage(not_null<HistoryItem*> item) {
	if (!item->isRegular()) {
		return;
	}
	const auto history = item->history();
	const auto channel = history->peer->asChannel();
	if (!channel || !channel->isMegagroup()) {
		return;
	}
	const auto slowSeconds = channel->slowmodeSeconds();
	if (slowSeconds <= 0) {
		return;
	}
	const auto sender = item->displayFrom()
		? not_null<PeerData*>(item->displayFrom())
		: item->from();
	if (IsSlowmodeExempt(channel, sender)) {
		return;
	}

	const auto now = base::unixtime::now();
	const auto expires = item->date() + slowSeconds;
	if (expires <= now) {
		return;
	}

	const auto key = Key{ channel->id, sender->id };
	const auto active = _active.find(key);
	if (active != _active.end() && now < active->second.expiresAt) {
		_exempt.insert(key);
		_active.erase(active);
		repaintAffectedChat(channel->id);
		return;
	}
	if (_exempt.contains(key)) {
		return;
	}
	auto &slot = _active[key];
	if (expires > slot.expiresAt) {
		slot = SlowmodeState{
			.startedAt = item->date(),
			.expiresAt = expires,
			.durationSeconds = slowSeconds,
		};
		repaintAffectedChat(channel->id);
		scheduleNextTick();
	}
}

std::optional<SlowmodeState> SlowmodeTracker::activeState(
		not_null<PeerData*> chatPeer,
		not_null<PeerData*> senderPeer) const {
	const auto it = _active.find(Key{ chatPeer->id, senderPeer->id });
	if (it == _active.end()) {
		return std::nullopt;
	}
	const auto now = base::unixtime::now();
	if (now >= it->second.expiresAt) {
		return std::nullopt;
	}
	return it->second;
}

int SlowmodeTracker::iconWidth(
		not_null<PeerData*> chatPeer,
		not_null<PeerData*> senderPeer) const {
	if (!activeState(chatPeer, senderPeer)) {
		return 0;
	}
	return style::ConvertScale(20);
}

int SlowmodeTracker::drawIconGetWidth(
		QPainter &p,
		int x,
		int y,
		int nameHeight,
		not_null<PeerData*> chatPeer,
		not_null<PeerData*> senderPeer,
		const QColor &badgeColor) const {
	const auto state = activeState(chatPeer, senderPeer);
	if (!state) {
		return 0;
	}

	const auto now = base::unixtime::now();
	const auto ratio = state->remainingRatio(now);
	if (ratio <= 0.0) {
		return 0;
	}

	const auto padLeft = style::ConvertScale(4);
	const auto size = style::ConvertScale(14);
	const auto top = y + (nameHeight - size) / 2;
	const auto rect = QRectF(x + padLeft, top, size, size);

	PainterHighQualityEnabler hq(p);
	p.save();

	auto bgPen = QPen(badgeColor);
	auto faded = badgeColor;
	faded.setAlphaF(faded.alphaF() * 0.28);
	bgPen.setColor(faded);
	bgPen.setWidthF(style::ConvertScaleExact(1.5));
	p.setPen(bgPen);
	p.setBrush(Qt::NoBrush);
	const auto dialRect = rect.adjusted(1.5, 2.5, -1.5, -0.5);
	p.drawEllipse(dialRect);

	auto activePen = QPen(badgeColor);
	activePen.setWidthF(style::ConvertScaleExact(1.6));
	activePen.setCapStyle(Qt::RoundCap);
	p.setPen(activePen);
	const auto cx = dialRect.center().x();
	p.drawLine(
		QPointF(cx - style::ConvertScaleExact(2.0), rect.top() + 0.8),
		QPointF(cx + style::ConvertScaleExact(2.0), rect.top() + 0.8));

	const auto startAngle16 = 90 * 16;
	const auto spanAngle16 = static_cast<int>(std::round(ratio * 360.0 * 16.0));
	p.drawArc(dialRect, startAngle16, spanAngle16);

	const auto angleRad = (0.5 + 2.0 * (1.0 - ratio)) * M_PI;
	const auto radius = dialRect.width() * 0.32;
	p.drawLine(
		dialRect.center(),
		QPointF(
			dialRect.center().x() + std::cos(angleRad) * radius,
			dialRect.center().y() - std::sin(angleRad) * radius));

	p.restore();
	return padLeft + size + style::ConvertScale(2);
}

void SlowmodeTracker::onTick() {
	const auto now = base::unixtime::now();
	base::flat_set<PeerId> chatsToRepaint;

	for (auto it = _active.begin(); it != _active.end();) {
		chatsToRepaint.emplace(it->first.chatId);
		if (now >= it->second.expiresAt) {
			it = _active.erase(it);
		} else {
			++it;
		}
	}

	for (const auto chatId : chatsToRepaint) {
		repaintAffectedChat(chatId);
	}

	scheduleNextTick();
}

void SlowmodeTracker::scheduleNextTick() {
	if (_active.empty()) {
		_tickTimer.cancel();
		return;
	}
	if (!_tickTimer.isActive()) {
		_tickTimer.callEach(1000);
	}
}

void SlowmodeTracker::repaintAffectedChat(PeerId chatId) {
	if (const auto history = _session->data().historyLoaded(chatId)) {
		for (const auto &block : history->blocks) {
			for (const auto &view : block->messages) {
				_session->data().requestItemRepaint(view->data());
			}
		}
	}
}

SlowmodeTracker &SlowmodeFor(not_null<Main::Session*> session) {
	static base::flat_map<not_null<Main::Session*>, std::unique_ptr<SlowmodeTracker>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<SlowmodeTracker>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
