/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/typing_indicator.h"
#include "custom/redesign.h"

#include "base/call_delayed.h"
#include "data/data_peer_values.h"
#include "data/data_send_action.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/view/history_view_send_action.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/text/text_custom_emoji.h"

#include <algorithm>

namespace Custom {
namespace {

constexpr auto kTypingIndicatorHeight = 24;
constexpr auto kTypingIndicatorAvatar = 16;
constexpr auto kTypingIndicatorEmoji = 16;
constexpr auto kExpirePollInterval = crl::time(400);

} // namespace

TypingIndicator::TypingIndicator(QWidget *parent, Fn<void()> changed)
: RpWidget(parent)
, _expireTimer([=] { refresh(); })
, _changed(std::move(changed)) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	TokensChanged() | rpl::on_next([=] { update(); }, lifetime());
	resize(0, 0);
	hide();
}

TypingIndicator::~TypingIndicator() {
	releaseRepliesPainter();
}

int TypingIndicator::fullHeight() const {
	return style::ConvertScale(kTypingIndicatorHeight);
}

int TypingIndicator::currentHeight() const {
	const auto ratio = _slideAnim.value(_targetShown ? 1.0 : 0.0);
	return int(base::SafeRound(fullHeight() * std::clamp(ratio, 0.0, 1.0)));
}

void TypingIndicator::releaseRepliesPainter() {
	if (!_repliesPainter) {
		return;
	}
	const auto history = _history;
	const auto rootId = _topicRootId;
	_repliesPainter = nullptr;
	if (history && rootId && _sessionWeak.get()) {
		history->owner().sendActionManager().repliesPainterRemoved(
			history,
			rootId);
	}
}

void TypingIndicator::setHistory(History *history, MsgId topicRootId) {
	if (_history == history && _topicRootId == topicRootId) {
		return;
	}
	_historyLifetime.destroy();
	releaseRepliesPainter();
	_history = history;
	_topicRootId = topicRootId;
	_sessionWeak = history
		? base::make_weak(&history->session())
		: base::weak_ptr<Main::Session>();
	_userpics.clear();
	_statusEmojis.clear();
	_cachedUsers.clear();
	_slideAnim.stop();
	_targetShown = false;
	hide();

	if (_history) {
		if (_topicRootId) {
			_repliesPainter = _history->owner().sendActionManager().repliesPainter(
				_history,
				_topicRootId);
		}
		_history->owner().sendActionManager().animationUpdated(
		) | rpl::filter([=](const Data::SendActionManager::AnimationUpdate &u) {
			return (u.thread->owningHistory() == _history);
		}) | rpl::on_next([=] {
			if (_refreshScheduled) {
				return;
			}
			_refreshScheduled = true;
			const auto weak = base::make_weak(this);
			crl::on_main([weak] {
				if (const auto indicator = weak.get()) {
					indicator->_refreshScheduled = false;
					indicator->refresh();
				}
			});
		}, _historyLifetime);
	}
	refresh();
}

void TypingIndicator::setTopicRootId(MsgId topicRootId) {
	setHistory(_history, topicRootId);
}

HistoryView::SendActionPainter *TypingIndicator::activePainter() const {
	if (!_history || !_sessionWeak.get()) {
		return nullptr;
	}
	return _topicRootId
		? _repliesPainter.get()
		: _history->sendActionPainter();
}

void TypingIndicator::setShownAnimated(bool shown) {
	if (_targetShown == shown) {
		return;
	}
	_targetShown = shown;
	if (shown && isHidden()) {
		show();
	}
	const auto from = _slideAnim.value(shown ? 0.0 : 1.0);
	const auto to = shown ? 1.0 : 0.0;
	const auto duration = shown ? 210 : 170;
	_slideAnim.start(
		[this] {
			const auto h = currentHeight();
			if (h <= 0 && !_targetShown && !_slideAnim.animating()) {
				_userpics.clear();
				_statusEmojis.clear();
				_cachedUsers.clear();
				hide();
			}
			if (_changed) {
				_changed();
			}
			update();
		},
		from,
		to,
		duration,
		anim::easeOutCubic);
	if (_changed) {
		_changed();
	}
}

void TypingIndicator::refresh() {
	if (_refreshing) {
		return;
	}
	_refreshing = true;
	auto guard = gsl::finally([&] { _refreshing = false; });
	const auto painter = activePainter();
	if (!painter) {
		_expireTimer.cancel();
		setShownAnimated(false);
		return;
	}
	const auto &typing = painter->typingUsers();
	const auto shown = !typing.empty();
	if (shown) {
		_cachedUsers.assign(typing.begin(), typing.end());
		if (!_expireTimer.isActive()) {
			_expireTimer.callEach(kExpirePollInterval);
		}
	} else {
		_expireTimer.cancel();
	}
	setShownAnimated(shown);
	if (!isHidden()) {
		update();
	}
}

void TypingIndicator::paintEvent(QPaintEvent *e) {
	if (width() <= 0 || height() <= 0 || !_sessionWeak.get()) {
		return;
	}
	const auto painter = activePainter();
	const auto liveUsers = painter
		? painter->typingUsers()
		: std::vector<not_null<UserData*>>();
	if (liveUsers.empty() && _cachedUsers.empty()) {
		return;
	}

	auto p = Painter(this);
	const auto ratio = std::clamp(
		_slideAnim.value(_targetShown ? 1.0 : 0.0),
		0.0,
		1.0);
	const auto targetH = fullHeight();
	p.setClipRect(rect());
	p.fillRect(rect(), st::historyComposeAreaBg);

	const auto tokens = DesignTokensForCurrentMode();
	if (tokens.accentWashAlpha > 0 && tokens.accent.isValid()) {
		auto wash = tokens.accent;
		wash.setAlphaF(tokens.accentWashAlpha / 640.0);
		p.fillRect(
			0,
			st::lineWidth,
			width(),
			height() - st::lineWidth,
			wash);
	}
	p.fillRect(0, 0, width(), st::lineWidth, st::shadowFg);

	p.setOpacity(ratio);
	p.translate(0, height() - targetH);

	const auto now = crl::now();
	const auto avatarSize = style::ConvertScale(kTypingIndicatorAvatar);
	const auto emojiSize = style::ConvertScale(kTypingIndicatorEmoji);
	auto x = st::historySendPadding;
	const auto avatarTop = (targetH - avatarSize) / 2;

	auto count = 0;
	struct PendingBadge {
		Ui::Text::CustomEmoji *emoji = nullptr;
		const style::icon *premium = nullptr;
	};
	std::vector<PendingBadge> badges;
	const auto drawOneUser = [&](not_null<UserData*> user) {
		const auto uid = peerToUser(user->id);
		auto &view = _userpics[uid];
		user->paintUserpicLeft(
			p,
			view,
			x,
			avatarTop,
			width(),
			avatarSize);
		x += avatarSize + style::ConvertScale(3);

		const auto statusId = user->emojiStatusId();
		if (statusId) {
			auto &entry = _statusEmojis[uid];
			if (!(entry.statusId == statusId) || !entry.emoji) {
				entry.statusId = statusId;
				entry.emoji = user->owner().customEmojiManager().create(
					Data::EmojiStatusCustomId(statusId),
					[this] { update(); },
					Data::CustomEmojiSizeTag::Normal,
					emojiSize);
			}
			if (entry.emoji) {
				badges.push_back({ .emoji = entry.emoji.get() });
			}
		} else {
			_statusEmojis.remove(uid);
			if (user->isPremium()) {
				badges.push_back({
					.premium = &st::dialogsPremiumIcon.icon,
				});
			}
		}
		x += style::ConvertScale(2);
	};

	if (!liveUsers.empty()) {
		for (const auto &user : liveUsers) {
			drawOneUser(user);
			if (++count == 3) {
				break;
			}
		}
	} else {
		for (const auto &user : _cachedUsers) {
			drawOneUser(user);
			if (++count == 3) {
				break;
			}
		}
	}

	const auto badgeSpacing = style::ConvertScale(3);
	auto badgesWidth = 0;
	for (const auto &badge : badges) {
		badgesWidth += badgeSpacing
			+ (badge.emoji ? emojiSize : badge.premium->width());
	}
	if (painter && !liveUsers.empty()) {
		const auto textTop = (targetH - st::normalFont->height) / 2;
		const auto textRight = width() - badgesWidth;
		painter->paint(
			p,
			x,
			textTop,
			std::max(textRight - x - st::historySendPadding, 0),
			width(),
			st::windowSubTextFg,
			now);
	}
	auto badgeX = width() - badgesWidth;
	for (const auto &badge : badges) {
		if (badge.emoji) {
			badge.emoji->paint(p, {
				.textColor = st::dialogsVerifiedIconBg->c,
				.now = now,
				.position = QPoint(
					badgeX + badgeSpacing,
					(targetH - emojiSize) / 2),
				.paused = On(PowerSaving::kEmojiStatus),
			});
			badgeX += badgeSpacing + emojiSize;
		} else {
			badge.premium->paint(
				p,
				badgeX + badgeSpacing,
				(targetH - badge.premium->height()) / 2,
				width(),
				st::dialogsVerifiedIconBg->c);
			badgeX += badgeSpacing + badge.premium->width();
		}
	}
}

} // namespace Custom
