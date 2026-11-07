/*
This file is part of 64Gram Desktop fork,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/frknkrc44/tdesktop-x64/blob/dev/LEGAL
*/
#ifndef __GOOGLE_TRANSLATE_H__
#define __GOOGLE_TRANSLATE_H__

#include <QJsonArray>
#include <QJsonDocument>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QString>
#include <QTextDocument>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

struct TranslationResult {
	QString text;
	bool success = false;
};

using TranslationCallback = std::function<void(TranslationResult)>;

class GTranslate : public QObject {
    Q_OBJECT
public:
	explicit GTranslate();
	~GTranslate() {
		delete manager;
	}
	void translate(
		QString from,
		QString to,
		QString query,
		TranslationCallback onFinished);
	std::string replaceLangCode(const std::string &lang);


private:
	QNetworkAccessManager *manager;
};

#endif // __GOOGLE_TRANSLATE_H__
