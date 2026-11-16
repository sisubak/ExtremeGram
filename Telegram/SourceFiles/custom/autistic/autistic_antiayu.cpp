/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/autistic/autistic_antiayu.h"

#include "api/api_editing.h"
#include "api/api_common.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/enhanced_settings.h"
#include "custom/autistic/autistic_mode.h"
#include "data/data_histories.h"
#include "data/data_msg_id.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"

#include <memory>
#include <utility>

namespace Autistic {
namespace {

constexpr auto kTauntStepMs = crl::time(700);

using ChainKey = std::pair<Main::Session*, FullMsgId>;

struct TauntChain {
	base::Timer timer;
	base::weak_ptr<Main::Session> session;
	std::vector<QString> steps;
	std::size_t index = 0;
	Fn<mtpRequestId()> finish;
};

base::flat_map<ChainKey, std::unique_ptr<TauntChain>> &Chains() {
	static auto value = base::flat_map<
		ChainKey,
		std::unique_ptr<TauntChain>>{};
	return value;
}

[[nodiscard]] ChainKey KeyFor(not_null<HistoryItem*> item) {
	return { &item->history()->session(), item->fullId() };
}

void Next(ChainKey key) {
	crl::on_main([=] {
		auto &chains = Chains();
		const auto i = chains.find(key);
		if (i == chains.end()) {
			return;
		}
		i->second->timer.callOnce(kTauntStepMs);
	});
}

void Abort(ChainKey key) {
	crl::on_main([=] {
		auto &chains = Chains();
		const auto i = chains.find(key);
		if (i == chains.end()) {
			return;
		}
		auto chain = std::move(i->second);
		chains.erase(i);
		if (chain->finish) {
			chain->finish();
		}
	});
}

void Fire(ChainKey key) {
	auto &chains = Chains();
	const auto i = chains.find(key);
	if (i == chains.end()) {
		return;
	}
	auto &chain = *i->second;
	const auto session = chain.session.get();
	const auto item = session ? session->data().message(key.second) : nullptr;
	if (!item) {
		crl::on_main([=] {
			Chains().erase(key);
		});
		return;
	}
	if (chain.index >= chain.steps.size()) {
		const auto finish = chain.finish;
		crl::on_main([=] {
			finish();
			Chains().erase(key);
		});
		return;
	}
	const auto text = chain.steps[chain.index++];
	Api::EditTextMessage(
		item,
		TextWithEntities{ .text = text },
		Data::WebPageDraft{},
		Api::SendOptions{},
		[=](mtpRequestId) { Next(key); },
		[=](const QString &, mtpRequestId) { Abort(key); },
		false);
}

bool Qualifies(not_null<HistoryItem*> item) {
	return Enabled()
		&& item->out()
		&& item->isRegular()
		&& !item->isService()
		&& !item->isScheduled();
}

[[nodiscard]] std::vector<QString> TauntSteps() {
	return {
		QString::fromUtf8("FUCK YOU"),
		QString::fromUtf8("FUCK"),
		QString::fromUtf8("F"),
	};
}

} // namespace

bool Enabled() {
	return EnhancedSettings::Get(EnhancedSettings::Option::AutisticAntiAyu);
}

bool ShouldTaunt(not_null<HistoryItem*> item) {
	return Qualifies(item)
		&& Chains().find(KeyFor(item)) == Chains().end();
}

void TauntDelete(not_null<HistoryItem*> item, bool revoke) {
	if (!ShouldTaunt(item)) {
		return;
	}
	const auto key = KeyFor(item);
	const auto id = item->fullId();
	auto chain = std::make_unique<TauntChain>();
	chain->session = base::make_weak(&item->history()->session());
	chain->steps = TauntSteps();
	chain->finish = [weak = base::make_weak(&item->history()->session()), revoke, id] -> mtpRequestId {
		const auto session = weak.get();
		if (!session) {
			return 0;
		}
		session->data().histories().deleteMessages(
			MessageIdsList{ id },
			revoke);
		return 0;
	};
	chain->timer.setCallback([=] {
		Fire(key);
	});
	Chains().emplace(key, std::move(chain));
	Fire(key);
}

bool ShouldInterceptEdit(
		not_null<HistoryItem*> item,
		const QString &newText) {
	return Qualifies(item)
		&& Chains().find(KeyFor(item)) == Chains().end()
		&& newText != item->originalText().text;
}

mtpRequestId TauntEdit(
		not_null<HistoryItem*> item,
		Fn<mtpRequestId()> finalCall) {
	const auto key = KeyFor(item);
	auto chain = std::make_unique<TauntChain>();
	chain->session = base::make_weak(&item->history()->session());
	chain->steps = TauntSteps();
	chain->finish = std::move(finalCall);
	chain->timer.setCallback([=] {
		Fire(key);
	});
	Chains().emplace(key, std::move(chain));
	Fire(key);
	return 0;
}

} // namespace Autistic
