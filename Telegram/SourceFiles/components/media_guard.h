#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

class DocumentData;

namespace MediaGuard {

[[nodiscard]] bool by_utf8xbot_1183(const QByteArray &content);

[[nodiscard]] QByteArray by_utf8xbot_5528(const QByteArray &content);

[[nodiscard]] bool by_utf8xbot_3409(const DocumentData *document);

[[nodiscard]] bool by_utf8xbot_4517(const QString &url);

}