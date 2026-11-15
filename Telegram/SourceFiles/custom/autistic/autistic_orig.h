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
#include <vector>

class HistoryItem;

namespace Autistic {

void RememberOriginal(FullMsgId id, TextWithEntities text);
[[nodiscard]] std::optional<TextWithEntities> OriginalFor(FullMsgId id);
[[nodiscard]] std::vector<TextWithEntities> OriginalHistoryFor(FullMsgId id);
[[nodiscard]] bool ShowOrigFor(not_null<HistoryItem*> item);

void ShowOriginalHistory(not_null<HistoryItem*> item);

[[nodiscard]] int OrigPillHeight();
[[nodiscard]] int OrigTopSkip();
[[nodiscard]] int OrigExtraHeight();

} // namespace Autistic
