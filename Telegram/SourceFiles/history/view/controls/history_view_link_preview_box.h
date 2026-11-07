/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <memory>

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace HistoryView::Controls {

void ShowLinkPreviewUrlBox(
	std::shared_ptr<ChatHelpers::Show> show,
	QString initialUrl,
	Fn<void(QString)> done);

} // namespace HistoryView::Controls
