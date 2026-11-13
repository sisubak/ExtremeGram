/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "mtproto/core_types.h"

#include <functional>

class HistoryItem;

namespace Autistic {

// Anti-AyuGram: before an own message is deleted for everyone it is
// edited three times (FUCK YOU -> FUCK -> F) and only then removed.
// Message edits go through the same sequence before the real text.
[[nodiscard]] bool Enabled();

[[nodiscard]] bool ShouldTaunt(not_null<HistoryItem*> item);
void TauntDelete(not_null<HistoryItem*> item, bool revoke);

[[nodiscard]] bool ShouldInterceptEdit(
	not_null<HistoryItem*> item,
	const QString &newText);
mtpRequestId TauntEdit(not_null<HistoryItem*> item, Fn<mtpRequestId()> finalCall);

} // namespace Autistic
