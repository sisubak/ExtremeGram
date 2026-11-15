/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_device_binding.h"

#include "base/openssl_help.h"
#include "base/random.h"
#include "mtproto/mtproto_auth_key.h"
#include "settings.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QSysInfo>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>

#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif // Q_OS_WIN

namespace Storage {
namespace {

constexpr auto kDeviceBindingMagic = quint32(0x45474442);
constexpr auto kDeviceBindingVersion = quint32(1);
constexpr auto kDeviceBindingSaltSize = 32;
constexpr auto kDeviceBindingVerifierSize = 32;
constexpr auto kDeviceBindingMaxBlobSize = 256;

constexpr auto kDeviceWrapInfo = "eg-device-wrap-v1";
constexpr auto kDeviceCheckInfo = "eg-device-check-v1";
constexpr auto kDeviceFileInfo = "eg-device-file-v1";

[[nodiscard]] QByteArray RandomBytes(int size) {
	auto result = QByteArray(size, Qt::Uninitialized);
	base::RandomFill(result.data(), result.size());
	return result;
}

[[nodiscard]] QByteArray HkdfSha256(
		const QByteArray &ikm,
		const QByteArray &salt,
		const char *info,
		int size) {
	if (ikm.isEmpty() || size <= 0) {
		return QByteArray();
	}
	const auto context = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
	if (!context) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] {
		EVP_PKEY_CTX_free(context);
	});
	auto result = QByteArray(size, Qt::Uninitialized);
	auto length = size_t(size);
	const auto infoSize = int(std::strlen(info));
	const auto ok = (EVP_PKEY_derive_init(context) > 0)
		&& (EVP_PKEY_CTX_set_hkdf_md(context, EVP_sha256()) > 0)
		&& (EVP_PKEY_CTX_set1_hkdf_salt(
			context,
			reinterpret_cast<const unsigned char*>(salt.constData()),
			int(salt.size())) > 0)
		&& (EVP_PKEY_CTX_set1_hkdf_key(
			context,
			reinterpret_cast<const unsigned char*>(ikm.constData()),
			int(ikm.size())) > 0)
		&& (infoSize <= 0
			|| (EVP_PKEY_CTX_add1_hkdf_info(
				context,
				reinterpret_cast<const unsigned char*>(info),
				infoSize) > 0))
		&& (EVP_PKEY_derive(
			context,
			reinterpret_cast<unsigned char*>(result.data()),
			&length) > 0)
		&& (length == size_t(size));
	if (!ok) {
		return QByteArray();
	}
	return result;
}

[[nodiscard]] bool ConstantTimeEquals(
		const QByteArray &a,
		const QByteArray &b) {
	return (a.size() == b.size())
		&& !CRYPTO_memcmp(a.constData(), b.constData(), a.size());
}

[[nodiscard]] QByteArray ReadSecretFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	} else if (file.size() > 4096) {
		return QByteArray();
	}
	return file.readAll();
}

[[nodiscard]] bool WriteSecretFile(
		const QString &path,
		const QByteArray &bytes) {
	const auto folder = QFileInfo(path).absolutePath();
	if (!QDir().exists(folder) && !QDir().mkpath(folder)) {
		return false;
	}
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
#ifndef Q_OS_WIN
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif // !Q_OS_WIN
	if (file.write(bytes) != bytes.size()) {
		file.cancelWriting();
		return false;
	}
	return file.commit();
}

#ifdef Q_OS_WIN
[[nodiscard]] DATA_BLOB DeviceSecretEntropy() {
	constexpr char kEntropy[] = "ExtremeGram device secret v1";
	return DATA_BLOB{
		DWORD(sizeof(kEntropy) - 1),
		reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy)),
	};
}
#endif // Q_OS_WIN

[[nodiscard]] QByteArray PlatformLoadSecret(const QString &path) {
#ifdef Q_OS_WIN
	const auto blob = ReadSecretFile(path);
	if (blob.isEmpty()) {
		return QByteArray();
	}
	auto in = DATA_BLOB{
		DWORD(blob.size()),
		reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData())),
	};
	auto entropy = DeviceSecretEntropy();
	auto out = DATA_BLOB();
	if (!CryptUnprotectData(
			&in,
			nullptr,
			&entropy,
			nullptr,
			nullptr,
			0,
			&out)) {
		return QByteArray();
	}
	auto result = QByteArray(
		reinterpret_cast<const char*>(out.pbData),
		int(out.cbData));
	LocalFree(out.pbData);
	if (result.size() != kDeviceSecretSize) {
		OPENSSL_cleanse(result.data(), result.size());
		return QByteArray();
	}
	return result;
#else // Q_OS_WIN
	const auto raw = ReadSecretFile(path);
	if (raw.size() != kDeviceSecretSize) {
		return QByteArray();
	}
	const auto machine = QSysInfo::machineUniqueId();
	if (machine.isEmpty()) {
		return raw;
	}
	return HkdfSha256(raw, machine, kDeviceFileInfo, kDeviceSecretSize);
#endif // Q_OS_WIN
}

[[nodiscard]] bool PlatformStoreSecret(
		const QString &path,
		const QByteArray &secret) {
#ifdef Q_OS_WIN
	auto in = DATA_BLOB{
		DWORD(secret.size()),
		reinterpret_cast<BYTE*>(const_cast<char*>(secret.constData())),
	};
	auto entropy = DeviceSecretEntropy();
	auto out = DATA_BLOB();
	if (!CryptProtectData(
			&in,
			L"ExtremeGram device secret",
			&entropy,
			nullptr,
			nullptr,
			0,
			&out)) {
		return false;
	}
	const auto blob = QByteArray(
		reinterpret_cast<const char*>(out.pbData),
		int(out.cbData));
	LocalFree(out.pbData);
	return WriteSecretFile(path, blob);
#else // Q_OS_WIN
	return WriteSecretFile(path, secret);
#endif // Q_OS_WIN
}

} // namespace

QByteArray GenerateDeviceSecret() {
	return RandomBytes(kDeviceSecretSize);
}

DeviceBinding MakeDeviceBinding(const QByteArray &secret) {
	if (secret.size() != kDeviceSecretSize) {
		return {};
	}
	auto result = DeviceBinding();
	result.salt = RandomBytes(kDeviceBindingSaltSize);
	result.verifier = HkdfSha256(
		secret,
		result.salt,
		kDeviceCheckInfo,
		kDeviceBindingVerifierSize);
	return result;
}

bool DeviceBindingMatches(
		const DeviceBinding &binding,
		const QByteArray &secret) {
	if (secret.size() != kDeviceSecretSize
		|| binding.salt.size() != kDeviceBindingSaltSize
		|| binding.verifier.size() != kDeviceBindingVerifierSize) {
		return false;
	}
	const auto verifier = HkdfSha256(
		secret,
		binding.salt,
		kDeviceCheckInfo,
		kDeviceBindingVerifierSize);
	return (verifier.size() == binding.verifier.size())
		&& ConstantTimeEquals(verifier, binding.verifier);
}

MTP::AuthKeyPtr MixDeviceWrapKey(
		const MTP::AuthKeyPtr &passcodeKey,
		const QByteArray &secret) {
	if (!passcodeKey || secret.size() != kDeviceSecretSize) {
		return nullptr;
	}
	const auto passcode = passcodeKey->data();
	const auto mixed = HkdfSha256(
		secret,
		QByteArray(
			reinterpret_cast<const char*>(passcode.data()),
			int(passcode.size())),
		kDeviceWrapInfo,
		int(MTP::AuthKey::kSize));
	if (mixed.size() != MTP::AuthKey::kSize) {
		return nullptr;
	}
	auto data = MTP::AuthKey::Data();
	std::memcpy(data.data(), mixed.constData(), MTP::AuthKey::kSize);
	return std::make_shared<MTP::AuthKey>(data);
}

QByteArray SerializeDeviceBinding(const DeviceBinding &binding) {
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	const auto opened = buffer.open(QIODevice::WriteOnly);
	Assert(opened);
	auto stream = QDataStream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< kDeviceBindingMagic
		<< kDeviceBindingVersion
		<< binding.salt
		<< binding.verifier;
	buffer.close();
	return result;
}

std::optional<DeviceBinding> ParseDeviceBinding(const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > kDeviceBindingMaxBlobSize) {
		return std::nullopt;
	}
	auto buffer = QBuffer();
	buffer.setData(bytes);
	if (!buffer.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	auto stream = QDataStream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
	auto magic = quint32();
	auto version = quint32();
	auto result = DeviceBinding();
	stream >> magic >> version >> result.salt >> result.verifier;
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| magic != kDeviceBindingMagic
		|| version > kDeviceBindingVersion
		|| result.salt.size() != kDeviceBindingSaltSize
		|| result.verifier.size() != kDeviceBindingVerifierSize) {
		return std::nullopt;
	}
	return result;
}

QString DeviceSecretPath(const QString &dataName) {
#ifdef Q_OS_WIN
	return cWorkingDir() + u"tdata/device_"_q + dataName;
#else // Q_OS_WIN
	return cWorkingDir() + u"device_"_q + dataName;
#endif // Q_OS_WIN
}

QByteArray LoadDeviceSecret(const QString &dataName) {
	return PlatformLoadSecret(DeviceSecretPath(dataName));
}

bool StoreDeviceSecret(
		const QString &dataName,
		const QByteArray &secret) {
	if (secret.size() != kDeviceSecretSize) {
		return false;
	}
	return PlatformStoreSecret(DeviceSecretPath(dataName), secret);
}

bool RemoveDeviceSecret(const QString &dataName) {
	const auto path = DeviceSecretPath(dataName);
	return !QFileInfo::exists(path) || QFile::remove(path);
}

QByteArray CreateDeviceSecret(const QString &dataName) {
	auto seed = GenerateDeviceSecret();
	if (seed.size() != kDeviceSecretSize) {
		return QByteArray();
	}
	const auto stored = StoreDeviceSecret(dataName, seed);
	OPENSSL_cleanse(seed.data(), seed.size());
	if (!stored) {
		LOG(("App Error: could not store the device secret."));
		return QByteArray();
	}
	auto secret = LoadDeviceSecret(dataName);
	if (secret.size() != kDeviceSecretSize) {
		LOG(("App Error: the stored device secret does not read back."));
		RemoveDeviceSecret(dataName);
		return QByteArray();
	}
	return secret;
}

} // namespace Storage
