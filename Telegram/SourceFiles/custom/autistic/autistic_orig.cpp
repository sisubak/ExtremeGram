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
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <utility>

namespace Autistic {
namespace {

base::flat_map<FullMsgId, std::vector<TextWithEntities>> &Originals() {
	static auto value = base::flat_map<
		FullMsgId,
		std::vector<TextWithEntities>>{};
	return value;
}

} // namespace

void RememberOriginal(FullMsgId id, TextWithEntities text) {
	auto &versions = Originals()[id];
	if (!versions.empty() && versions.back().text == text.text) {
		return;
	}
	versions.push_back(std::move(text));
	if (versions.size() > 32) {
		versions.erase(versions.begin());
	}
}

std::optional<TextWithEntities> OriginalFor(FullMsgId id) {
	const auto &map = Originals();
	const auto i = map.find(id);
	if (i == map.end() || i->second.empty()) {
		return std::nullopt;
	}
	return i->second.back();
}

std::vector<TextWithEntities> OriginalHistoryFor(FullMsgId id) {
	const auto &map = Originals();
	const auto i = map.find(id);
	return (i == map.end())
		? std::vector<TextWithEntities>()
		: i->second;
}

bool ShowOrigFor(not_null<HistoryItem*> item) {
	if (!on(EnhancedSettings::Option::AutisticShowOrig)
		|| item->isService()
		|| item->out()
		|| item->originalText().text.isEmpty()) {
		return false;
	}
	const auto original = OriginalFor(item->fullId());
	return original.has_value() && !original->text.isEmpty();
}

void ShowOriginalHistory(not_null<HistoryItem*> item) {
	const auto versions = OriginalHistoryFor(item->fullId());
	const auto window = item->history()->session().tryResolveWindow(
		item->history()->peer);
	if (!window || versions.empty()) {
		return;
	}
	auto text = QString();
	for (auto i = 0; i != int(versions.size()); ++i) {
		if (i > 0) {
			text += QString::fromUtf8("\n\n");
		}
		text += QString::fromUtf8("Версия %1 из %2:\n")
			.arg(i + 1)
			.arg(int(versions.size()));
		text += versions[i].text;
	}
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_orig_history());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			text,
			st::boxLabel));
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

int OrigPillHeight() {
	return st::msgServiceFont->height + 8;
}

int OrigTopSkip() {
	return 4;
}

int OrigExtraHeight() {
	return OrigPillHeight() + OrigTopSkip();
}

} // namespace Autistic
