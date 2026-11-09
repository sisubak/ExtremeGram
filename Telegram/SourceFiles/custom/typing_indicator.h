/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_peer_id.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

#include <memory>
#include <vector>

class History;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace HistoryView {
class SendActionPainter;
} // namespace HistoryView

namespace Custom {

class TypingIndicator final : public Ui::RpWidget {
public:
	TypingIndicator(QWidget *parent, Fn<void()> changed);
	~TypingIndicator();

	void setHistory(History *history, MsgId topicRootId = 0);
	void setTopicRootId(MsgId topicRootId);
	[[nodiscard]] int currentHeight() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct StatusEmojiEntry {
		DocumentId documentId = 0;
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
	};

	[[nodiscard]] HistoryView::SendActionPainter *activePainter() const;
	[[nodiscard]] int fullHeight() const;
	void releaseRepliesPainter();
	void refresh();
	void setShownAnimated(bool shown);

	History *_history = nullptr;
	MsgId _topicRootId = 0;
	base::weak_ptr<Main::Session> _sessionWeak;
	std::shared_ptr<HistoryView::SendActionPainter> _repliesPainter;
	rpl::lifetime _historyLifetime;
	base::flat_map<UserId, Ui::PeerUserpicView> _userpics;
	base::flat_map<UserId, StatusEmojiEntry> _statusEmojis;
	std::vector<not_null<UserData*>> _cachedUsers;
	Ui::Animations::Simple _slideAnim;
	bool _refreshing = false;
	bool _refreshScheduled = false;
	bool _targetShown = false;
	base::Timer _expireTimer;
	Fn<void()> _changed;

};

} // namespace Custom
