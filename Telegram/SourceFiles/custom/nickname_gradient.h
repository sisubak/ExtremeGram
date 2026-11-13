/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flat_map.h"
#include "data/data_peer_id.h"

#include <QColor>
#include <QPen>
#include <optional>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Custom {

struct NicknameGradientConfig {
	QColor c1;
	QColor c2;
	int speed = 1;

	friend inline constexpr bool operator==(
		const NicknameGradientConfig &,
		const NicknameGradientConfig &) = default;
};

[[nodiscard]] std::optional<NicknameGradientConfig> ParseBioGradient(
	const QString &about);

[[nodiscard]] QString EncodeBioGradient(
	const NicknameGradientConfig &config);

[[nodiscard]] QString StripBioGradient(const QString &about);

class NicknameGradient final {
public:
	explicit NicknameGradient(not_null<Main::Session*> session);

	[[nodiscard]] std::optional<NicknameGradientConfig> configFor(
		not_null<PeerData*> peer);

	[[nodiscard]] QPen penFor(
		const NicknameGradientConfig &config,
		float64 width) const;

	void repaintPeer(PeerId peerId);

private:
	struct Entry {
		QString about;
		std::optional<NicknameGradientConfig> config;
		crl::time lastFullRequest = 0;
	};

	void ensureTimer();
	void onTick();

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, Entry> _cache;
	base::Timer _tickTimer;
	rpl::lifetime _lifetime;
};

void ApplyNicknameGradient(
	not_null<Main::Session*> session,
	const NicknameGradientConfig &config);

[[nodiscard]] NicknameGradient &NicknameGradientFor(
	not_null<Main::Session*> session);

void ShowNicknameGradientBox(
	not_null<Window::SessionController*> controller);

} // namespace Custom
