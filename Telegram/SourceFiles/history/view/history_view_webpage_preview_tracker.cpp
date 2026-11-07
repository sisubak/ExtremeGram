/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_webpage_preview_tracker.h"

#include "api/api_text_entities.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"

namespace HistoryView {
namespace {

void ApplyWebPagePreview(
		not_null<Main::Session*> session,
		FullMsgId id,
		const TextWithEntities &requestedText,
		const MTPmessages_WebPagePreview &result) {
	const auto owner = &session->data();
	const auto &data = result.data();
	owner->processUsers(data.vusers());
	owner->processChats(data.vchats());

	const auto item = owner->message(id);
	if (!item
		|| item->originalText() != requestedText
		|| !item->needsForceShowWebPagePreview()) {
		return;
	}
	const auto &media = data.vmedia();
	const auto isWebPage = media.match(
		[](const MTPDmessageMediaWebPage &) { return true; },
		[](const auto &) { return false; });
	if (!isWebPage) {
		return;
	}
	auto created = HistoryItem::CreateMedia(item, media);
	if (!created || !created->webpage()) {
		return;
	}
	item->overrideMedia(std::move(created));
	owner->requestItemViewRefresh(item);
}

} // namespace

WebPagePreviewTracker::WebPagePreviewTracker(not_null<History*> history)
: _session(&history->session())
, _api(&_session->mtp()) {
}

void WebPagePreviewTracker::startBunch() {
	_bunch.clear();
}

void WebPagePreviewTracker::add(not_null<Element*> view) {
	const auto item = view->data();
	if (item->needsForceShowWebPagePreview()) {
		_bunch.emplace(item->fullId());
	}
}

void WebPagePreviewTracker::finishBunch() {
	_visible = std::move(_bunch);
	_bunch.clear();
	requestSome();
}

void WebPagePreviewTracker::requestSome() {
	if (_requestInProcess) {
		return;
	}
	const auto owner = &_session->data();
	while (!_visible.empty()) {
		const auto i = begin(_visible);
		const auto id = *i;
		_visible.erase(i);
		const auto item = owner->message(id);
		if (!item || !item->markForceShowWebPagePreviewRequested()) {
			continue;
		}
		const auto text = item->originalText();
		const auto entities = Api::EntitiesToMTP(
			_session,
			text.entities);
		using Flag = MTPmessages_GetWebPagePreview::Flag;
		const auto flags = entities.v.isEmpty() ? Flag(0) : Flag::f_entities;
		_requestInProcess = true;
		_api.request(MTPmessages_GetWebPagePreview(
			MTP_flags(flags),
			MTP_string(text.text),
			entities
		)).done([=](const MTPmessages_WebPagePreview &result) {
			ApplyWebPagePreview(_session, id, text, result);
			_requestInProcess = false;
			requestSome();
		}).fail([=](const MTP::Error &) {
			_requestInProcess = false;
			requestSome();
		}).send();
		return;
	}
}

} // namespace HistoryView
