/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "data/data_msg_id.h"
#include "mtproto/sender.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {

class Element;

class WebPagePreviewTracker final {
public:
	explicit WebPagePreviewTracker(not_null<History*> history);

	void startBunch();
	void add(not_null<Element*> view);
	void finishBunch();

private:
	void requestSome();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::flat_set<FullMsgId> _visible;
	base::flat_set<FullMsgId> _bunch;
	bool _requestInProcess = false;

};

} // namespace HistoryView
