/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class RecentForwardTargets final {
public:
	explicit RecentForwardTargets(not_null<Main::Session*> session);
	~RecentForwardTargets();

	[[nodiscard]] const std::vector<not_null<PeerData*>> &list() const;

	void bump(not_null<PeerData*> peer);
	void remove(not_null<PeerData*> peer);
	void clear();

	[[nodiscard]] QByteArray serialize() const;
	void applyLocal(QByteArray serialized);

private:
	const not_null<Main::Session*> _session;

	std::vector<not_null<PeerData*>> _list;

};

} // namespace Data
