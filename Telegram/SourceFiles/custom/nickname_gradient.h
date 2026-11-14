/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "base/timer.h"
#include "data/data_peer_id.h"

#include <rpl/event_stream.h>

#include <QColor>
#include <QPen>
#include <optional>
#include <vector>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Custom {

struct NicknameGradientConfig {
	std::vector<QColor> colors;
	int speed = 1;

	friend inline bool operator==(
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

	[[nodiscard]] rpl::producer<> ticks() const;

	void repaintPeer(PeerId peerId);

private:
	void ensureTimer();
	void onTick();
	void fireChanged();

	const not_null<Main::Session*> _session;
	base::flat_set<PeerId> _animated;
	base::Timer _tickTimer;
	rpl::event_stream<> _tickEvents;
	rpl::lifetime _lifetime;
	bool _firing = false;
};

void ApplyNicknameGradient(
	not_null<Main::Session*> session,
	std::optional<NicknameGradientConfig> config,
	Fn<void(bool ok, QString message)> done = nullptr);

[[nodiscard]] NicknameGradient &NicknameGradientFor(
	not_null<Main::Session*> session);

void ShowNicknameGradientBox(
	not_null<Window::SessionController*> controller);

} // namespace Custom
