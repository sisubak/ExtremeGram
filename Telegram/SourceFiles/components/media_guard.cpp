#include "components/media_guard.h"

#include "data/data_document.h"

#include <QtCore/QUrl>

#include <zlib.h>

#include <array>

namespace MediaGuard {
namespace {

constexpr auto kMaxJsonBytes = 4 * 1024 * 1024;
constexpr auto kMaxCompressedBytes = 2 * 1024 * 1024;
constexpr auto kMaxStickerBytes = int64(64) * 1024 * 1024;
constexpr auto kMaxSide = 8192;
constexpr auto kMaxArea = int64(4096) * 4096;
constexpr auto kMaxFps = 240.;
constexpr auto kMaxFrames = 6000.;
constexpr auto kMaxSeconds = 60.;

[[nodiscard]] bool by_utf8xbot_6118(const QByteArray &content) {
	return (content.size() >= 2)
		&& (uchar(content[0]) == 0x1F)
		&& (uchar(content[1]) == 0x8B);
}

[[nodiscard]] bool by_utf8xbot_3141(const QByteArray &content) {
	for (const auto value : content) {
		if (value == ' ' || value == '\t'
			|| value == '\r' || value == '\n') {
			continue;
		}
		return (value == '{');
	}
	return false;
}

[[nodiscard]] qint64 by_utf8xbot_7821(const QByteArray &content) {
	if (content.size() < 18) {
		return -1;
	}
	const auto bytes = reinterpret_cast<const uchar*>(content.constData());
	const auto at = content.size() - 4;
	return qint64(
		quint32(bytes[at])
		| (quint32(bytes[at + 1]) << 8)
		| (quint32(bytes[at + 2]) << 16)
		| (quint32(bytes[at + 3]) << 24));
}

[[nodiscard]] QByteArray by_utf8xbot_9264(const QByteArray &content) {
	auto stream = z_stream();
	if (inflateInit2(&stream, 15 + 16) != Z_OK) {
		return {};
	}
	auto result = QByteArray();
	const auto expected = by_utf8xbot_7821(content);
	if (expected > 0) {
		result.reserve(expected <= kMaxJsonBytes ? int(expected) : kMaxJsonBytes);
	}
	auto buffer = std::array<char, 16384>();
	stream.next_in = reinterpret_cast<Bytef*>(
		const_cast<char*>(content.constData()));
	stream.avail_in = uInt(content.size());
	auto finished = false;
	while (true) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = uInt(buffer.size());
		const auto step = inflate(&stream, Z_NO_FLUSH);
		const auto produced = int(buffer.size() - stream.avail_out);
		if (produced > 0) {
			if (result.size() + produced > kMaxJsonBytes) {
				break;
			}
			result.append(buffer.data(), produced);
		}
		if (step == Z_STREAM_END) {
			finished = true;
			break;
		} else if (step != Z_OK) {
			break;
		}
	}
	inflateEnd(&stream);
	return finished ? result : QByteArray();
}

[[nodiscard]] int by_utf8xbot_4437(
		const QByteArray &json,
		const char *key,
		double *value) {
	const auto needle = QByteArray("\"") + key + '"';
	auto from = qsizetype(0);
	auto at = qsizetype(-1);
	while (true) {
		at = json.indexOf(needle, from);
		if (at < 0) {
			return 0;
		}
		auto back = at - 1;
		while (back >= 0
			&& (json[back] == ' '
				|| json[back] == '\t'
				|| json[back] == '\r'
				|| json[back] == '\n')) {
			--back;
		}
		if (back >= 0 && (json[back] == '{' || json[back] == ',')) {
			break;
		}
		from = at + needle.size();
	}
	auto index = at + needle.size();
	const auto skipSpaces = [&] {
		while (index < json.size()
			&& (json[index] == ' '
				|| json[index] == '\t'
				|| json[index] == '\r'
				|| json[index] == '\n')) {
			++index;
		}
	};
	skipSpaces();
	if (index >= json.size() || json[index] != ':') {
		return -1;
	}
	++index;
	skipSpaces();
	const auto begin = index;
	if (index < json.size() && json[index] == '-') {
		++index;
	}
	auto digits = 0;
	while (index < json.size() && json[index] >= '0' && json[index] <= '9') {
		++index;
		++digits;
	}
	if (index < json.size() && json[index] == '.') {
		++index;
		while (index < json.size()
			&& json[index] >= '0'
			&& json[index] <= '9') {
			++index;
			++digits;
		}
	}
	if (index < json.size() && (json[index] == 'e' || json[index] == 'E')) {
		++index;
		if (index < json.size() && (json[index] == '+' || json[index] == '-')) {
			++index;
		}
		auto exponent = 0;
		while (index < json.size()
			&& json[index] >= '0'
			&& json[index] <= '9') {
			++index;
			++exponent;
		}
		if (!exponent) {
			return -1;
		}
	}
	if (!digits) {
		return -1;
	}
	auto ok = false;
	const auto parsed = json.mid(begin, index - begin).toDouble(&ok);
	if (!ok) {
		return -1;
	}
	*value = parsed;
	return 1;
}

[[nodiscard]] bool by_utf8xbot_7384(const QByteArray &json) {
	constexpr auto kMaxDepth = 64;
	auto depth = 0;
	auto inString = false;
	auto escaped = false;
	for (const auto value : json) {
		if (inString) {
			if (escaped) {
				escaped = false;
			} else if (value == '\\') {
				escaped = true;
			} else if (value == '"') {
				inString = false;
			}
			continue;
		}
		if (value == '"') {
			inString = true;
		} else if (value == '{' || value == '[') {
			if (++depth > kMaxDepth) {
				return false;
			}
		} else if (value == '}' || value == ']') {
			if (--depth < 0) {
				return false;
			}
		}
	}
	return (depth == 0) && !inString;
}

[[nodiscard]] bool by_utf8xbot_6650(const QByteArray &json) {
	if (json.size() > kMaxJsonBytes || !by_utf8xbot_7384(json)) {
		return false;
	}
	auto width = 0.;
	auto height = 0.;
	auto fps = 0.;
	auto outPoint = 0.;
	auto inPoint = 0.;
	const auto hasWidth = by_utf8xbot_4437(json, "w", &width);
	const auto hasHeight = by_utf8xbot_4437(json, "h", &height);
	const auto hasFps = by_utf8xbot_4437(json, "fr", &fps);
	const auto hasOutPoint = by_utf8xbot_4437(json, "op", &outPoint);
	const auto hasInPoint = by_utf8xbot_4437(json, "ip", &inPoint);
	if (hasWidth < 0
		|| hasHeight < 0
		|| hasFps < 0
		|| hasOutPoint < 0
		|| hasInPoint < 0) {
		return false;
	}
	if (hasWidth && !(width > 0. && width <= double(kMaxSide))) {
		return false;
	}
	if (hasHeight && !(height > 0. && height <= double(kMaxSide))) {
		return false;
	}
	if (hasWidth && hasHeight && width * height > double(kMaxArea)) {
		return false;
	}
	if (hasFps && !(fps > 0. && fps <= kMaxFps)) {
		return false;
	}
	if (hasOutPoint && !(outPoint > 0. && outPoint <= kMaxFrames)) {
		return false;
	}
	if (hasInPoint && (inPoint < 0. || inPoint > kMaxFrames)) {
		return false;
	}
	if (hasOutPoint
		&& hasInPoint
		&& outPoint > inPoint
		&& hasFps
		&& (outPoint - inPoint) / fps > kMaxSeconds) {
		return false;
	}
	return true;
}

}

bool by_utf8xbot_1183(const QByteArray &content) {
	if (content.isEmpty()) {
		return true;
	}
	if (by_utf8xbot_6118(content)) {
		if (content.size() > kMaxCompressedBytes) {
			return false;
		}
		const auto expected = by_utf8xbot_7821(content);
		if (expected <= 0 || expected > kMaxJsonBytes) {
			return false;
		}
		const auto json = by_utf8xbot_9264(content);
		return !json.isEmpty() && by_utf8xbot_6650(json);
	}
	if (!by_utf8xbot_3141(content)) {
		return true;
	}
	return by_utf8xbot_6650(content);
}

QByteArray by_utf8xbot_5528(const QByteArray &content) {
	return by_utf8xbot_1183(content) ? content : QByteArray();
}

bool by_utf8xbot_3409(const DocumentData *document) {
	if (!document) {
		return false;
	}
	if (document->size <= 0 || document->size > kMaxStickerBytes) {
		return false;
	}
	const auto dimensions = document->dimensions;
	if (dimensions.width() <= 0 || dimensions.height() <= 0) {
		return false;
	}
	if (dimensions.width() > kMaxSide || dimensions.height() > kMaxSide) {
		return false;
	}
	return int64(dimensions.width()) * dimensions.height() <= kMaxArea;
}

bool by_utf8xbot_4517(const QString &url) {
	const auto parsed = QUrl(url);
	if (!parsed.isValid() || parsed.isRelative()) {
		return false;
	}
	const auto scheme = parsed.scheme().toLower();
	if (scheme == QStringLiteral("https")) {
		return !parsed.host().isEmpty();
	}
	if (scheme == QStringLiteral("about")) {
		return (parsed.toString() == QStringLiteral("about:blank"));
	}
	if (scheme != QStringLiteral("http") || parsed.host().isEmpty()) {
		return false;
	}
	const auto host = parsed.host().toLower();
	return host == QStringLiteral("localhost")
		|| host == QStringLiteral("127.0.0.1")
		|| host == QStringLiteral("::1")
		|| host == QStringLiteral("[::1]");
}

}
