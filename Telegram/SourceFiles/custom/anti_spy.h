/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

class QWidget;

namespace Window {
class SessionController;
} // namespace Window

namespace Custom::AntiSpy {

struct AppInfo {
	QString name;
	QStringList processes;
};

[[nodiscard]] const std::vector<AppInfo> &KnownApps();
[[nodiscard]] bool PlatformSupported();

void RegisterWindow(not_null<QWidget*> window);
void ShowSettingsBox(not_null<Window::SessionController*> controller);

} // namespace Custom::AntiSpy
