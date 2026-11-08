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
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

#include <memory>

class History;

namespace Main {
class Session;
} // namespace Main

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

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] HistoryView::SendActionPainter *activePainter() const;
	void releaseRepliesPainter();
	void refresh();

	History *_history = nullptr;
	MsgId _topicRootId = 0;
	base::weak_ptr<Main::Session> _sessionWeak;
	std::shared_ptr<HistoryView::SendActionPainter> _repliesPainter;
	rpl::lifetime _historyLifetime;
	base::flat_map<UserId, Ui::PeerUserpicView> _userpics;
	base::Timer _expireTimer;
	Fn<void()> _changed;

};

} // namespace Custom
