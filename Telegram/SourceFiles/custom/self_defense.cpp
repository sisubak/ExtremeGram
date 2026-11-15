/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/self_defense.h"

#include "settings.h"
#include "storage/storage_device_binding.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QString>

#include <array>

#ifdef Q_OS_WIN
#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCoreApplication>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else // Q_OS_WIN
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#endif // Q_OS_WIN

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif // Q_OS_LINUX

namespace Custom::SelfDefense {
namespace {

QString _dataName;
int _pauseDepth = 0;

#ifdef Q_OS_WIN

HANDLE _keyHandle = INVALID_HANDLE_VALUE;
HANDLE _secretHandle = INVALID_HANDLE_VALUE;
bool _sessionEnding = false;

class SessionEndFilter final : public QAbstractNativeEventFilter {
public:
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		qintptr *result) override;
};

bool SessionEndFilter::nativeEventFilter(
		const QByteArray &,
		void *message,
		qintptr *) {
	const auto msg = static_cast<MSG*>(message);
	if (msg
		&& (msg->message == WM_QUERYENDSESSION
			|| msg->message == WM_ENDSESSION)) {
		_sessionEnding = true;
	}
	return false;
}

void CloseHandleValue(HANDLE &handle) {
	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
	}
}

void OpenHandleValue(HANDLE &handle, const QString &path) {
	CloseHandleValue(handle);
	if (!QFileInfo::exists(path)) {
		return;
	}
	const auto native = QDir::toNativeSeparators(path);
	handle = CreateFileW(
		reinterpret_cast<const wchar_t*>(native.utf16()),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
}

#else // Q_OS_WIN

int _keyHandle = -1;
int _secretHandle = -1;

void CloseHandleValue(int &handle) {
	if (handle >= 0) {
		::close(handle);
		handle = -1;
	}
}

void OpenHandleValue(int &handle, const QString &path) {
	CloseHandleValue(handle);
	if (!QFileInfo::exists(path)) {
		return;
	}
	const auto local = QFile::encodeName(path);
	handle = ::open(local.constData(), O_RDONLY | O_CLOEXEC);
	if (handle >= 0) {
		::flock(handle, LOCK_EX | LOCK_NB);
	}
}

#endif // Q_OS_WIN

void OpenKeyFiles() {
	if (_dataName.isEmpty()) {
		return;
	}
	const auto folder = cWorkingDir() + u"tdata/"_q;
	const auto base = folder + u"key_"_q + _dataName;
	auto path = base + u"s"_q;
	if (!QFileInfo::exists(path)) {
		path = base + u"0"_q;
		if (!QFileInfo::exists(path)) {
			path = base + u"1"_q;
		}
	}
	OpenHandleValue(_keyHandle, path);
	OpenHandleValue(_secretHandle, Storage::DeviceSecretPath(_dataName));
}

void CloseKeyFiles() {
	CloseHandleValue(_keyHandle);
	CloseHandleValue(_secretHandle);
}

} // namespace

void Start() {
#ifdef Q_OS_WIN
	PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extension = {};
	extension.DisableExtensionPoints = TRUE;
	SetProcessMitigationPolicy(
		ProcessExtensionPointDisablePolicy,
		&extension,
		sizeof(extension));

	PROCESS_MITIGATION_IMAGE_LOAD_POLICY image = {};
	image.NoRemoteImages = TRUE;
	image.NoLowMandatoryLabelImages = TRUE;
	SetProcessMitigationPolicy(
		ProcessImageLoadPolicy,
		&image,
		sizeof(image));

	if (const auto app = QCoreApplication::instance()) {
		app->installNativeEventFilter(new SessionEndFilter());
	}
#else // Q_OS_WIN
	struct rlimit limit = { 0, 0 };
	setrlimit(RLIMIT_CORE, &limit);
#ifdef Q_OS_LINUX
	prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif // Q_OS_LINUX
#endif // Q_OS_WIN
}

void GuardKeyFiles(const QString &dataName) {
	_dataName = dataName;
	OpenKeyFiles();
}

void PauseKeyFiles() {
	if (++_pauseDepth == 1) {
		CloseKeyFiles();
	}
}

void ResumeKeyFiles() {
	if (_pauseDepth <= 0) {
		return;
	} else if (--_pauseDepth == 0) {
		OpenKeyFiles();
	}
}

void LockMemory(void *data, std::size_t size) {
	if (!data || !size) {
		return;
	}
#ifdef Q_OS_WIN
	VirtualLock(data, size);
#else // Q_OS_WIN
	mlock(data, size);
#endif // Q_OS_WIN
}

void UnlockMemory(void *data, std::size_t size) {
	if (!data || !size) {
		return;
	}
#ifdef Q_OS_WIN
	VirtualUnlock(data, size);
#else // Q_OS_WIN
	munlock(data, size);
#endif // Q_OS_WIN
}

QString ForeignCloseRequester() {
#ifdef Q_OS_WIN
	if (_sessionEnding) {
		return QString();
	}
	const auto foreground = GetForegroundWindow();
	if (!foreground) {
		return QString();
	}
	auto pid = DWORD(0);
	GetWindowThreadProcessId(foreground, &pid);
	if (!pid || pid == GetCurrentProcessId()) {
		return QString();
	}
	const auto process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE,
		pid);
	if (!process) {
		return QString::number(pid);
	}
	auto name = QString();
	auto buffer = std::array<wchar_t, 1024>{};
	auto size = DWORD(buffer.size());
	if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
		name = QFileInfo(QString::fromWCharArray(
			buffer.data(),
			int(size))).fileName();
	}
	CloseHandle(process);
	return name.isEmpty() ? QString::number(pid) : name;
#else // Q_OS_WIN
	return QString();
#endif // Q_OS_WIN
}

} // namespace Custom::SelfDefense
