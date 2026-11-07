/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <crl/crl_time.h>

namespace Media::Clip {

[[nodiscard]] QByteArray AddSilentAudioTrack(
	const QByteArray &content,
	crl::time duration);

} // namespace Media::Clip
