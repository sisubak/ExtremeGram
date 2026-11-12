/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "ui/text/text_entity.h"

#include <optional>

class HistoryItem;

namespace Autistic {

// "Orig before edit": when the AutisticShowOrig feature is on, every
// message edition first snapshots the current text; the view renders it
// as a small "orig: ..." line under the message text. In-memory only,
// cleaned by the app restart.
void RememberOriginal(FullMsgId id, TextWithEntities text);
[[nodiscard]] std::optional<TextWithEntities> OriginalFor(FullMsgId id);
[[nodiscard]] bool ShowOrigFor(not_null<HistoryItem*> item);

// Extra bubble height taken by the "orig: ..." line (drawn under text).
[[nodiscard]] int OrigExtraHeight();

} // namespace Autistic
