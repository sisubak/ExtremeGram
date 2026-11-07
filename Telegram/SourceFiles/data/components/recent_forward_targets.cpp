/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_forward_targets.h"

#include "core/version.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kLimit = 48;

} // namespace

RecentForwardTargets::RecentForwardTargets(
		not_null<Main::Session*> session)
: _session(session) {
}

RecentForwardTargets::~RecentForwardTargets() = default;

const std::vector<not_null<PeerData*>> &RecentForwardTargets::list() const {
	_session->local().readSearchSuggestions();

	return _list;
}

void RecentForwardTargets::bump(not_null<PeerData*> peer) {
	_session->local().readSearchSuggestions();

	if (!_list.empty() && _list.front() == peer) {
		return;
	}
	auto i = ranges::find(_list, peer);
	if (i == end(_list)) {
		_list.push_back(peer);
		i = end(_list) - 1;
	}
	ranges::rotate(begin(_list), i, i + 1);

	_session->local().writeSearchSuggestionsDelayed();
}

void RecentForwardTargets::remove(not_null<PeerData*> peer) {
	_session->local().readSearchSuggestions();

	const auto i = ranges::find(_list, peer);
	if (i != end(_list)) {
		_list.erase(i);
	}
	_session->local().writeSearchSuggestionsDelayed();
}

void RecentForwardTargets::clear() {
	_session->local().readSearchSuggestions();

	_list.clear();
	_session->local().writeSearchSuggestionsDelayed();
}

QByteArray RecentForwardTargets::serialize() const {
	_session->local().readSearchSuggestions();

	if (_list.empty()) {
		return {};
	}
	auto size = 2 * sizeof(quint32);
	const auto count = std::min(int(_list.size()), kLimit);
	auto &&list = _list | ranges::views::take(count);
	for (const auto &peer : list) {
		size += Serialize::peerSize(peer);
	}
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< quint32(AppVersion)
		<< quint32(count);
	for (const auto &peer : list) {
		Serialize::writePeer(stream, peer);
	}
	return std::move(stream).result();
}

void RecentForwardTargets::applyLocal(QByteArray serialized) {
	_list.clear();
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = Serialize::ByteArrayReader(serialized);
	auto streamAppVersion = quint32();
	auto count = quint32();
	stream >> streamAppVersion >> count;
	if (!stream.ok()) {
		return;
	}
	_list.reserve(count);
	for (auto i = 0; i != int(count); ++i) {
		const auto peer = Serialize::readPeer(
			_session,
			streamAppVersion,
			stream);
		if (stream.ok() && peer) {
			_list.push_back(peer);
		} else {
			_list.clear();
			return;
		}
	}
}

} // namespace Data
