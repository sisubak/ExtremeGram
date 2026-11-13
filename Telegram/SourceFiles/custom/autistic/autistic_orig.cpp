/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/autistic/autistic_orig.h"

#include "base/flat_map.h"
#include "core/enhanced_settings.h"
#include "custom/autistic/autistic_mode.h"
#include "history/history_item.h"
#include "styles/style_chat.h"

#include <utility>

namespace Autistic {
namespace {

base::flat_map<FullMsgId, TextWithEntities> &Originals() {
	static auto value = base::flat_map<FullMsgId, TextWithEntities>{};
	return value;
}

} // namespace

void RememberOriginal(FullMsgId id, TextWithEntities text) {
	Originals().insert_or_assign(id, std::move(text));
}

std::optional<TextWithEntities> OriginalFor(FullMsgId id) {
	const auto &map = Originals();
	const auto i = map.find(id);
	if (i == map.end()) {
		return std::nullopt;
	}
	return i->second;
}

bool ShowOrigFor(not_null<HistoryItem*> item) {
	if (!on(EnhancedSettings::Option::AutisticShowOrig)
		|| item->isService()
		|| item->out()) {
		return false;
	}
	const auto original = OriginalFor(item->fullId());
	return original.has_value() && !original->text.isEmpty();
}

int OrigExtraHeight() {
	return st::msgServiceFont->height * 5 / 4;
}

} // namespace Autistic
