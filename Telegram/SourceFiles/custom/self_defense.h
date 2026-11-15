/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

#include <cstddef>

namespace Custom::SelfDefense {

void Start();
void GuardKeyFiles(const QString &dataName);
void RefreshKeyFiles();
void PauseKeyFiles();
void ResumeKeyFiles();
void LockMemory(void *data, std::size_t size);
void UnlockMemory(void *data, std::size_t size);
[[nodiscard]] QString ForeignCloseRequester();

} // namespace Custom::SelfDefense
