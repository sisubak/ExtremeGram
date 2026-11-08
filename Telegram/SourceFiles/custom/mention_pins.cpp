/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/mention_pins.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

#include <algorithm>

namespace Custom {
namespace {

const auto kEmpty = std::vector<UserId>();

[[nodiscard]] auto Find(
		std::vector<UserId> &list,
		UserId id) {
	return std::find(list.begin(), list.end(), id);
}

[[nodiscard]] auto Find(
		const std::vector<UserId> &list,
		UserId id) {
	return std::find(list.begin(), list.end(), id);
}

[[nodiscard]] bool IsPureFormattingTag(const QString &tag) {
	return (tag == Ui::InputField::kTagBold)
		|| (tag == Ui::InputField::kTagItalic)
		|| (tag == Ui::InputField::kTagUnderline)
		|| (tag == Ui::InputField::kTagStrikeOut)
		|| (tag == Ui::InputField::kTagSpoiler)
		|| (tag == Ui::InputField::kTagBlockquote)
		|| (tag == Ui::InputField::kTagBlockquoteCollapsed);
}

} // namespace

MentionPins::MentionPins(not_null<Main::Session*> session)
: _session(session) {
	loadFromDisk();
}

const std::vector<UserId> &MentionPins::list(
		not_null<PeerData*> peer) const {
	const auto i = _pins.find(peer->id);
	return (i != end(_pins)) ? i->second : kEmpty;
}

bool MentionPins::has(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return false;
	}
	return Find(i->second, peerToUser(user->id)) != i->second.end();
}

bool MentionPins::canMoveUp(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return false;
	}
	const auto j = Find(i->second, peerToUser(user->id));
	return (j != i->second.end()) && (j != i->second.begin());
}

bool MentionPins::canMoveDown(
		not_null<PeerData*> peer,
		not_null<UserData*> user) const {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return false;
	}
	const auto j = Find(i->second, peerToUser(user->id));
	return (j != i->second.end()) && (j + 1 != i->second.end());
}

void MentionPins::pin(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	auto &users = _pins[peer->id];
	const auto uid = peerToUser(user->id);
	if (Find(users, uid) == users.end()) {
		users.insert(users.begin(), uid);
		saveToDisk();
	}
}

void MentionPins::unpin(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return;
	}
	auto &users = i->second;
	const auto j = Find(users, peerToUser(user->id));
	if (j == users.end()) {
		return;
	}
	users.erase(j);
	if (users.empty()) {
		_pins.remove(peer->id);
	}
	saveToDisk();
}

void MentionPins::moveUp(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return;
	}
	auto &users = i->second;
	const auto j = Find(users, peerToUser(user->id));
	if (j != users.end() && j != users.begin()) {
		std::iter_swap(j, j - 1);
		saveToDisk();
	}
}

void MentionPins::moveDown(
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins)) {
		return;
	}
	auto &users = i->second;
	const auto j = Find(users, peerToUser(user->id));
	if (j != users.end() && j + 1 != users.end()) {
		std::iter_swap(j, j + 1);
		saveToDisk();
	}
}

void MentionPins::reorderPinned(
		not_null<PeerData*> peer,
		const std::vector<UserId> &orderedSubset) {
	const auto i = _pins.find(peer->id);
	if (i == end(_pins) || orderedSubset.size() < 2) {
		return;
	}
	auto &users = i->second;
	auto filteredSubset = std::vector<UserId>();
	filteredSubset.reserve(orderedSubset.size());
	for (const auto &uid : orderedSubset) {
		if (Find(users, uid) != users.end()
			&& Find(filteredSubset, uid) == filteredSubset.end()) {
			filteredSubset.push_back(uid);
		}
	}
	if (filteredSubset.size() < 2) {
		return;
	}
	size_t takeIdx = 0;
	for (auto &slot : users) {
		if (Find(filteredSubset, slot) != filteredSubset.end()) {
			slot = filteredSubset[takeIdx++];
		}
	}
	saveToDisk();
}

QString MentionPins::storageFilePath() const {
	const auto dirPath = cWorkingDir() + u"tdata/extremegram"_q;
	QDir().mkpath(dirPath);
	return QDir(dirPath).filePath(
		u"mention_pins_"_q
		+ QString::number(_session->userId().bare)
		+ u".json"_q);
}

void MentionPins::loadFromDisk() {
	QFile f(storageFilePath());
	if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		return;
	}
	const auto root = doc.object();
	for (auto it = root.begin(); it != root.end(); ++it) {
		bool okPeer = false;
		const auto rawPeer = it.key().toULongLong(&okPeer);
		if (!okPeer || !rawPeer || !it.value().isArray()) {
			continue;
		}
		const auto arr = it.value().toArray();
		auto list = std::vector<UserId>();
		list.reserve(arr.size());
		for (const auto &v : arr) {
			bool okUser = false;
			const auto rawUser = v.toString().toULongLong(&okUser);
			if (okUser && rawUser) {
				const auto uid = UserId(rawUser);
				if (Find(list, uid) == list.end()) {
					list.push_back(uid);
				}
			}
		}
		if (!list.empty()) {
			_pins[PeerId(rawPeer)] = std::move(list);
		}
	}
}

void MentionPins::saveToDisk() const {
	QJsonObject root;
	for (const auto &[peerId, users] : _pins) {
		if (users.empty()) {
			continue;
		}
		QJsonArray arr;
		for (const auto &uid : users) {
			arr.append(QString::number(uid.bare));
		}
		root[QString::number(peerId.value)] = arr;
	}
	QSaveFile f(storageFilePath());
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
		f.commit();
	}
}

MentionPins &MentionPinsFor(not_null<Main::Session*> session) {
	static base::flat_map<not_null<Main::Session*>, std::unique_ptr<MentionPins>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<MentionPins>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

bool LooksLikeMarkdown(const QString &text) {
	if (text.contains(u"**"_q)
		|| text.contains(u"__"_q)
		|| text.contains(u"~~"_q)
		|| text.contains(u"||"_q)
		|| text.contains(u"`"_q)
		|| text.contains(u"]("_q)
		|| text.contains(u"\n#"_q)
		|| text.startsWith(u"# "_q)
		|| text.startsWith(u"## "_q)
		|| text.startsWith(u"### "_q)
		|| text.startsWith(u"> "_q)
		|| text.contains(u"\n> "_q)
		|| text.contains(u"\n- "_q)
		|| text.startsWith(u"- "_q)
		|| text.contains(u"\n* "_q)
		|| text.startsWith(u"* "_q)) {
		return true;
	}
	static const QRegularExpression kSingleItalicOrList(
		u"(?:^|\\s)\\*[^\\s*][^\\n*]{0,200}[^\\s*]\\*(?:$|\\s|[.,!?;:])|(?:^|\\n)\\d+\\.\\s+"_q);
	return kSingleItalicOrList.match(text).hasMatch();
}

TextWithTags ApplyMentionNames(
		not_null<Main::Session*> session,
		TextWithTags text) {
	struct Replacement {
		int start = 0;
		int length = 0;
		int newLength = 0;
	};
	static const auto expression = QRegularExpression(
		u"@([A-Za-z][A-Za-z0-9_]{4,31})(?![A-Za-z0-9_])"_q);
	const auto overlapsProtectedTag = [&](int start, int finish) {
		for (const auto &tag : text.tags) {
			if (!IsPureFormattingTag(tag.id)
				&& (tag.offset < finish)
				&& (tag.offset + tag.length > start)) {
				return true;
			}
		}
		return false;
	};
	auto replacements = std::vector<Replacement>();
	auto result = TextWithTags();
	auto position = 0;
	auto iterator = expression.globalMatch(text.text);
	while (iterator.hasNext()) {
		const auto match = iterator.next();
		const auto start = int(match.capturedStart());
		const auto length = int(match.capturedLength());
		if (start > 0) {
			const auto prev = text.text[start - 1];
			if (prev.isLetterOrNumber()
				|| (prev == '_')
				|| (prev == '/')
				|| (prev == '.')) {
				continue;
			}
		}
		if (overlapsProtectedTag(start, start + length)) {
			continue;
		}
		const auto username = match.captured(1);
		const auto peer = session->data().peerByUsername(username);
		const auto user = peer ? peer->asUser() : nullptr;
		if (!user) {
			continue;
		}
		const auto name = user->name();
		if (name.isEmpty()) {
			continue;
		}
		result.text.append(text.text.mid(position, start - position));
		const auto mentionStart = int(result.text.size());
		result.text.append(name);
		result.tags.push_back(TextWithTags::Tag{
			mentionStart,
			int(name.size()),
			u"mention://"_q + TextUtilities::MentionNameDataFromFields({
				.selfId = session->userId().bare,
				.userId = peerToUser(user->id).bare,
				.accessHash = user->accessHash(),
			}),
		});
		replacements.push_back({ start, length, int(name.size()) });
		position = start + length;
	}
	if (replacements.empty()) {
		return text;
	}
	result.text.append(text.text.mid(position));

	const auto mapOffset = [&](int offset, bool end) {
		auto delta = 0;
		for (const auto &replacement : replacements) {
			if (offset <= replacement.start) {
				break;
			} else if (offset >= replacement.start + replacement.length) {
				delta += replacement.newLength - replacement.length;
			} else {
				return replacement.start + delta
					+ (end ? replacement.newLength : 0);
			}
		}
		return offset + delta;
	};
	for (const auto &tag : text.tags) {
		const auto start = mapOffset(tag.offset, false);
		const auto finish = mapOffset(tag.offset + tag.length, true);
		if (finish > start) {
			result.tags.push_back(TextWithTags::Tag{
				start,
				finish - start,
				tag.id,
			});
		}
	}
	std::sort(
		result.tags.begin(),
		result.tags.end(),
		[](const TextWithTags::Tag &a, const TextWithTags::Tag &b) {
			return a.offset < b.offset;
		});
	return result;
}

} // namespace Custom
