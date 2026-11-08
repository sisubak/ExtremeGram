/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/typing_indicator.h"

#include "data/data_send_action.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_send_action.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "ui/painter.h"

#include <algorithm>

namespace Custom {
namespace {

constexpr auto kTypingIndicatorHeight = 20;
constexpr auto kTypingIndicatorAvatar = 16;
constexpr auto kExpirePollInterval = crl::time(500);

} // namespace

TypingIndicator::TypingIndicator(QWidget *parent, Fn<void()> changed)
: RpWidget(parent)
, _expireTimer([=] { refresh(); })
, _changed(std::move(changed)) {
	setFixedHeight(style::ConvertScale(kTypingIndicatorHeight));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();
}

TypingIndicator::~TypingIndicator() {
	releaseRepliesPainter();
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
			refresh();
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

void TypingIndicator::refresh() {
	const auto painter = activePainter();
	if (!painter) {
		_expireTimer.cancel();
		if (!isHidden()) {
			setVisible(false);
			if (_changed) {
				_changed();
			}
		}
		return;
	}
	painter->updateNeedsAnimating(crl::now(), true);
	const auto shown = !painter->typingUsers().empty();
	if (shown) {
		if (!_expireTimer.isActive()) {
			_expireTimer.callEach(kExpirePollInterval);
		}
	} else {
		_expireTimer.cancel();
		_userpics.clear();
	}
	if (shown != !isHidden()) {
		setVisible(shown);
		if (_changed) {
			_changed();
		}
	}
	if (shown) {
		update();
	}
}

void TypingIndicator::paintEvent(QPaintEvent *e) {
	const auto painter = activePainter();
	if (!painter) {
		return;
	}
	auto p = Painter(this);
	const auto users = painter->typingUsers();
	if (users.empty()) {
		return;
	}
	const auto avatarSize = style::ConvertScale(kTypingIndicatorAvatar);
	auto x = st::historySendPadding;
	const auto top = (height() - avatarSize) / 2;
	for (auto i = 0; i != int(users.size()) && i != 3; ++i) {
		const auto user = users[i];
		auto &view = _userpics[peerToUser(user->id)];
		user->paintUserpicLeft(
			p,
			view,
			x,
			top,
			width(),
			avatarSize);
		x += avatarSize + st::lineWidth * 2;
		if (user->isPremium()) {
			const auto &icon = st::dialogsPremiumIcon.icon;
			icon.paint(
				p,
				x,
				top + avatarSize - icon.height(),
				width(),
				st::windowSubTextFg->c);
			x += icon.width();
		}
		x += st::lineWidth * 2;
	}
	const auto textTop = (height() - st::normalFont->height) / 2;
	painter->paint(
		p,
		x,
		textTop,
		std::max(width() - x, 0),
		width(),
		st::windowSubTextFg,
		crl::now());
}

} // namespace Custom
