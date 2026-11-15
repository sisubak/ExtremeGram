/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <memory>
#include <optional>

namespace MTP {
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Storage {

// The device-bound profile keeps its only working passcode wrap out of reach
// of every build that does not know this blob: the wrap key is not the bare
// memory-hard passcode key, but HKDF-SHA256 over that key and a random secret
// that never leaves the machine. The blob below travels after the format 2
// trailer, where readers that predate it stop parsing, so an old build asks
// for a passcode and fails instead of resetting the profile. The verifier
// tells a lost device secret apart from a wrong passcode before the expensive
// derivation runs; it is a HKDF output over a 256-bit secret, so it gives no
// offline shortcut to the secret itself.
struct DeviceBinding final {
	QByteArray salt;
	QByteArray verifier;
};

inline constexpr auto kDeviceSecretSize = 32;

[[nodiscard]] QByteArray GenerateDeviceSecret();
[[nodiscard]] DeviceBinding MakeDeviceBinding(const QByteArray &secret);
[[nodiscard]] bool DeviceBindingMatches(
	const DeviceBinding &binding,
	const QByteArray &secret);
[[nodiscard]] MTP::AuthKeyPtr MixDeviceWrapKey(
	const MTP::AuthKeyPtr &passcodeKey,
	const QByteArray &secret);

[[nodiscard]] QByteArray SerializeDeviceBinding(const DeviceBinding &binding);
[[nodiscard]] std::optional<DeviceBinding> ParseDeviceBinding(
	const QByteArray &bytes);

[[nodiscard]] QString DeviceSecretPath(const QString &dataName);
[[nodiscard]] QByteArray LoadDeviceSecret(const QString &dataName);
[[nodiscard]] bool StoreDeviceSecret(
	const QString &dataName,
	const QByteArray &secret);
bool RemoveDeviceSecret(const QString &dataName);

// Generates a seed, stores it and reads back the secret the rest of the
// module will actually mix into wraps: on Windows that read-back proves the
// DPAPI round trip, elsewhere it applies the machine mix. Empty means the
// machine cannot keep a secret and the caller must not bind anything.
[[nodiscard]] QByteArray CreateDeviceSecret(const QString &dataName);

} // namespace Storage
