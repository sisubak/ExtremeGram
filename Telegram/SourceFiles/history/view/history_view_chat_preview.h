/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

class PeerData;

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace HistoryView {

struct ChatStatus {
	QString text;
	bool active = false;
};

[[nodiscard]] rpl::producer<ChatStatus> ChatStatusValue(
	not_null<PeerData*> peer);

struct ChatPreviewAction {
	FullMsgId openItemId;
	bool cancel = false;
	bool openInfo = false;
	bool markRead = false;
	bool markUnread = false;
};

struct ChatPreview {
	base::unique_qptr<Ui::PopupMenu> menu;
	rpl::producer<ChatPreviewAction> actions;
};

[[nodiscard]] ChatPreview MakeChatPreview(
	QWidget *parent,
	not_null<Dialogs::Entry*> entry);

} // namespace HistoryView
