/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/client_badge_detector.h"

#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/random.h"
#include "core/enhanced_settings.h"
#include "data/data_changes.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_user_photos.h"
#include "styles/palette.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "ui/painter.h"

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QPainterPath>
#include <array>
#include <limits>
#include <queue>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Custom {
namespace {

constexpr auto kUploadAvatarDim = 800;
constexpr auto kPartSize = 128 * 1024;

[[nodiscard]] double PerceivedLuminance(const QColor &c) {
	return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
}

[[nodiscard]] bool IsNearWhiteBackground(QRgb px) {
	if (qAlpha(px) < 12) {
		return true;
	}
	return (qRed(px) >= 236) && (qGreen(px) >= 236) && (qBlue(px) >= 236);
}

[[nodiscard]] QImage GenerateFallbackBadge() {
	QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, true);

	QPainterPath star;
	star.moveTo(32, 6);
	star.quadTo(34, 28, 58, 32);
	star.quadTo(34, 36, 32, 58);
	star.quadTo(30, 36, 6, 32);
	star.quadTo(30, 28, 32, 6);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(20, 20, 20));
	p.drawPath(star);
	return img;
}

constexpr auto kGradientMagic = 0xEA7;
constexpr auto kGradientVersion = 1;
constexpr auto kGradientMaxColors = 6;
constexpr auto kGradientPayloadBytes = 23;
constexpr auto kGradientPayloadNibbles = kGradientPayloadBytes * 2;
constexpr auto kGradientDataTiles = 92;
constexpr auto kGradientCornerTiles = 5;
constexpr auto kGradientCornerSize = kUploadAvatarDim / 8;
constexpr auto kGradientTileSize = kGradientCornerSize / kGradientCornerTiles;
constexpr auto kGradientCopies = kGradientPayloadNibbles;

constexpr std::array<QRgb, 16> kGradientPalette = {
	0xFF000000u, 0xFFFFFFFFu, 0xFFFF0000u, 0xFF00FF00u,
	0xFF0000FFu, 0xFF00FFFFu, 0xFFFF00FFu, 0xFFFFFF00u,
	0xFFFF8000u, 0xFF8000FFu, 0xFF0080FFu, 0xFF00FF80u,
	0xFF80FF00u, 0xFFFF0080u, 0xFF808080u, 0xFF404040u,
};

struct GradientTile {
	int corner = 0;
	int x = 0;
	int y = 0;
};

[[nodiscard]] const std::array<GradientTile, kGradientDataTiles>
&GradientTiles() {
	static const auto value = [] {
		auto result = std::array<GradientTile, kGradientDataTiles>{};
		auto index = 0;
		const auto appendCorner = [&](int corner, bool skipMarker) {
			for (auto y = 0; y != kGradientCornerTiles; ++y) {
				for (auto x = 0; x != kGradientCornerTiles; ++x) {
					const auto marker = (corner == 0 && x < 2 && y < 2)
						|| (corner == 3 && x >= 3 && y >= 3);
					if (skipMarker && marker) {
						continue;
					}
					result[index++] = GradientTile{ corner, x, y };
				}
			}
		};
		appendCorner(0, true);
		appendCorner(1, false);
		appendCorner(2, false);
		appendCorner(3, true);
		return result;
	}();
	return value;
}

class GradientBitWriter final {
public:
	void write(uint32 value, int bits) {
		for (auto i = bits - 1; i >= 0; --i) {
			if ((value >> i) & 1) {
				_data[_bit / 8] |= uint8(1 << (7 - (_bit % 8)));
			}
			++_bit;
		}
	}

	[[nodiscard]] std::array<uint8, kGradientPayloadBytes> data() const {
		return _data;
	}

private:
	std::array<uint8, kGradientPayloadBytes> _data = {};
	int _bit = 0;
};

class GradientBitReader final {
public:
	explicit GradientBitReader(
		const std::array<uint8, kGradientPayloadBytes> &data)
	: _data(data) {
	}

	[[nodiscard]] uint32 read(int bits) {
		auto result = uint32(0);
		for (auto i = 0; i != bits; ++i) {
			result = (result << 1)
				| ((_data[_bit / 8] >> (7 - (_bit % 8))) & 1);
			++_bit;
		}
		return result;
	}

private:
	const std::array<uint8, kGradientPayloadBytes> &_data;
	int _bit = 0;
};

[[nodiscard]] uint8 GradientCrc8(const uint8 *data, int size) {
	auto crc = uint8(0);
	for (auto i = 0; i != size; ++i) {
		crc ^= data[i];
		for (auto bit = 0; bit != 8; ++bit) {
			crc = (crc & 0x80)
				? uint8((crc << 1) ^ 0x07)
				: uint8(crc << 1);
		}
	}
	return crc;
}

[[nodiscard]] std::array<uint8, kGradientPayloadBytes>
EncodeGradientPayload(const NicknameGradientConfig &config) {
	auto writer = GradientBitWriter();
	writer.write(kGradientMagic, 12);
	writer.write(kGradientVersion, 4);
	const auto count = int(std::min<std::size_t>(
		config.colors.size(),
		kGradientMaxColors));
	writer.write(count, 3);
	writer.write(std::clamp(config.speed, 0, 2), 2);
	for (auto i = 0; i != count; ++i) {
		const auto &color = config.colors[i];
		writer.write(
			(uint32(color.red()) << 16)
			| (uint32(color.green()) << 8)
			| uint32(color.blue()),
			24);
	}
	auto data = writer.data();
	data[kGradientPayloadBytes - 1] = GradientCrc8(
		data.data(),
		kGradientPayloadBytes - 1);
	return data;
}

[[nodiscard]] std::optional<NicknameGradientConfig> DecodeGradientPayload(
		const std::array<uint8, kGradientPayloadBytes> &data) {
	if (GradientCrc8(data.data(), kGradientPayloadBytes - 1)
		!= data[kGradientPayloadBytes - 1]) {
		return std::nullopt;
	}
	auto reader = GradientBitReader(data);
	if (reader.read(12) != kGradientMagic
		|| reader.read(4) != kGradientVersion) {
		return std::nullopt;
	}
	const auto count = int(reader.read(3));
	const auto speed = int(reader.read(2));
	if (count > kGradientMaxColors || speed > 2) {
		return std::nullopt;
	}
	auto result = NicknameGradientConfig{ .colors = {}, .speed = speed };
	result.colors.reserve(count);
	for (auto i = 0; i != count; ++i) {
		result.colors.push_back(QColor::fromRgb(reader.read(24)));
	}
	return result;
}

[[nodiscard]] QColor SampleGradientTile(
		const QImage &image,
		const GradientTile &tile) {
	const auto side = std::min(image.width(), image.height());
	const auto cornerSize = side / 8;
	const auto tileSize = cornerSize / kGradientCornerTiles;
	if (tileSize < 2) {
		return QColor();
	}
	const auto originX = (tile.corner == 1 || tile.corner == 3)
		? (side - cornerSize)
		: 0;
	const auto originY = (tile.corner >= 2)
		? (side - cornerSize)
		: 0;
	const auto x0 = originX + tile.x * tileSize;
	const auto y0 = originY + tile.y * tileSize;
	const auto inset = std::max(1, tileSize / 4);
	const auto x1 = x0 + inset;
	const auto y1 = y0 + inset;
	const auto x2 = x0 + tileSize - inset;
	const auto y2 = y0 + tileSize - inset;
	auto sumR = 0;
	auto sumG = 0;
	auto sumB = 0;
	auto count = 0;
	for (auto y = y1; y < y2; ++y) {
		for (auto x = x1; x < x2; ++x) {
			const auto pixel = image.pixel(x, y);
			sumR += qRed(pixel);
			sumG += qGreen(pixel);
			sumB += qBlue(pixel);
			++count;
		}
	}
	if (!count) {
		return QColor();
	}
	return QColor(sumR / count, sumG / count, sumB / count);
}

[[nodiscard]] int NearestGradientPaletteIndex(const QColor &color) {
	auto result = 0;
	auto best = std::numeric_limits<int>::max();
	for (auto i = 0; i != int(kGradientPalette.size()); ++i) {
		const auto rgb = kGradientPalette[i];
		const auto dr = color.red() - int(qRed(rgb));
		const auto dg = color.green() - int(qGreen(rgb));
		const auto db = color.blue() - int(qBlue(rgb));
		const auto distance = dr * dr + dg * dg + db * db;
		if (distance < best) {
			best = distance;
			result = i;
		}
	}
	return result;
}

} // namespace

ClientBadgeDetector::ClientBadgeDetector(not_null<Main::Session*> session)
: _session(session)
, _api(&_session->mtp())
, _autoStampTimer([=, this] { tryStamp(); }) {
	reloadBadgeSourceImage();

	style::PaletteChanged(
	) | rpl::on_next([=, this] {
		_tintedIconCache.clear();
	}, _lifetime);

	_session->changes().peerUpdates(
		_session->user(),
		Data::PeerUpdate::Flag::Photo
	) | rpl::on_next([=, this] {
		const auto currentId = _session->user()->userpicPhotoId();
		if (currentId && currentId != _lastVerifiedSelfPhotoId) {
			if (currentId != _lastAttemptPhotoId) {
				_lastAttemptPhotoId = currentId;
				_autoStampAttempts = 0;
			}
			scheduleStamp(1500);
		}
	}, _lifetime);

	_session->downloaderTaskFinished(
	) | rpl::on_next([=, this] {
		if (_waitingForVideo) {
			const auto ready = _videoMedia
				&& !_videoMedia->videoContent(Data::PhotoSize::Large).isEmpty();
			if (ready || (++_videoWaitChecks >= 50)) {
				_waitingForVideo = false;
				_videoWaitChecks = 0;
				auto done = base::take(_waitingDone);
				auto gradient = base::take(_waitingGradient);
				if (ready) {
					stampSelfAvatarWithGradient(
						std::move(gradient),
						std::move(done));
				} else {
					_videoMedia = nullptr;
					if (done) {
						done(
							false,
							QString::fromUtf8("Не удалось загрузить видео-аватарку."));
					}
				}
			}
		}
		pollPendingPeers();
		if (_autoStampPending) {
			tryStamp();
		}
	}, _lifetime);

	scheduleStamp(2500);
}

void ClientBadgeDetector::scheduleStamp(crl::time delayMs) {
	_autoStampPending = true;
	_autoStampTimer.callOnce(delayMs);
}

void ClientBadgeDetector::tryStamp() {
	if (!_autoStampPending
		|| _stampInProgress
		|| _waitingForVideo) {
		return;
	}
	const auto self = _session->user();
	if (self->userpicPhotoUnknown()) {
		self->updateFull();
		_autoStampTimer.callOnce(2000);
		return;
	}
	const auto photoId = self->userpicPhotoId();
	if (!photoId) {
		_autoStampPending = false;
		return;
	}
	if (photoId == _lastVerifiedSelfPhotoId) {
		_autoStampPending = false;
		return;
	}
	if (photoId != _lastAttemptPhotoId) {
		_lastAttemptPhotoId = photoId;
		_autoStampAttempts = 0;
	}
	if (_autoStampAttempts >= 3) {
		_autoStampPending = false;
		return;
	}
	Ui::PeerUserpicView view;
	const auto *cloud = self->userpicCloudImage(view);
	if (!cloud || cloud->isNull()) {
		self->loadUserpic();
		_autoStampTimer.callOnce(1500);
		return;
	}
	if (DetectMarkerInAvatarImage(*cloud)) {
		const auto existing = DetectGradientInAvatarImage(*cloud);
		const auto existingConfig = (existing && !existing->colors.empty())
			? existing
			: std::optional<NicknameGradientConfig>();
		if (existingConfig == currentSelfGradient()) {
			_lastVerifiedSelfPhotoId = photoId;
			_autoStampPending = false;
			return;
		}
	}
	++_autoStampAttempts;
	_autoStampPending = false;
	stampSelfAvatar(nullptr);
}

void ClientBadgeDetector::reloadBadgeSourceImage() {
	_cleanLightStrokeBadge = CropAndCleanBadgeImage(loadRawBadgeAsset(true));
	_cleanDarkStrokeBadge = CropAndCleanBadgeImage(loadRawBadgeAsset(false));
	_tintedIconCache.clear();
	_cachedPixelSize = 0;
}

QImage ClientBadgeDetector::loadRawBadgeAsset(bool preferLightStroke) const {
	const auto customDir = cWorkingDir() + u"tdata/custom_client"_q;
	QDir().mkpath(customDir);
	const QStringList candidates = {
		customDir + u"/client_badge.png"_q,
		customDir + u"/client_badge.jpg"_q,
		preferLightStroke
			? u":/gui/art/extremegram/badge-light.png"_q
			: u":/gui/art/extremegram/badge-dark.png"_q,
	};
	for (const auto &path : candidates) {
		QImage loaded(path);
		if (!loaded.isNull()) {
			return loaded;
		}
	}
	return GenerateFallbackBadge();
}

QImage ClientBadgeDetector::CropAndCleanBadgeImage(const QImage &rawBadge) {
	if (rawBadge.isNull()) {
		return GenerateFallbackBadge();
	}

	QImage img = rawBadge.convertToFormat(QImage::Format_ARGB32);
	const int w = img.width();
	const int h = img.height();
	if (w <= 0 || h <= 0) {
		return img;
	}

	auto hasTrueTransparency = false;
	for (int y = 0; y < h && !hasTrueTransparency; ++y) {
		const auto *scan = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; ++x) {
			if (qAlpha(scan[x]) < 200) {
				hasTrueTransparency = true;
				break;
			}
		}
	}

	if (!hasTrueTransparency) {
		std::vector<uint8_t> visited(static_cast<size_t>(w) * h, 0);
		std::queue<QPoint> q;

		const auto tryPush = [&](int x, int y) {
			if (x < 0 || x >= w || y < 0 || y >= h) {
				return;
			}
			const auto idx = static_cast<size_t>(y) * w + x;
			if (visited[idx]) {
				return;
			}
			if (IsNearWhiteBackground(img.pixel(x, y))) {
				visited[idx] = 1;
				q.push(QPoint(x, y));
			}
		};

		for (int x = 0; x < w; ++x) {
			tryPush(x, 0);
			tryPush(x, h - 1);
		}
		for (int y = 0; y < h; ++y) {
			tryPush(0, y);
			tryPush(w - 1, y);
		}

		constexpr int dx[4] = { 1, -1, 0, 0 };
		constexpr int dy[4] = { 0, 0, 1, -1 };

		while (!q.empty()) {
			const auto pt = q.front();
			q.pop();
			img.setPixel(pt.x(), pt.y(), qRgba(0, 0, 0, 0));

			for (int dir = 0; dir < 4; ++dir) {
				const int nx = pt.x() + dx[dir];
				const int ny = pt.y() + dy[dir];
				if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
					continue;
				}
				const auto nIdx = static_cast<size_t>(ny) * w + nx;
				if (visited[nIdx]) {
					continue;
				}
				const auto npx = img.pixel(nx, ny);
				if (IsNearWhiteBackground(npx)) {
					visited[nIdx] = 1;
					q.push(QPoint(nx, ny));
				} else {
					const int maxCh = std::max({
						qRed(npx),
						qGreen(npx),
						qBlue(npx) });
					const int minCh = std::min({
						qRed(npx),
						qGreen(npx),
						qBlue(npx) });
					if (minCh > 200 && (maxCh - minCh) < 24) {
						const float alphaFactor = std::clamp(
							(236.0f - minCh) / 36.0f,
							0.0f,
							1.0f);
						const int newAlpha = static_cast<int>(
							std::round(qAlpha(npx) * alphaFactor));
						img.setPixel(
							nx,
							ny,
							qRgba(qRed(npx), qGreen(npx), qBlue(npx), newAlpha));
					}
				}
			}
		}
	}

	int minX = w, minY = h, maxX = -1, maxY = -1;
	for (int y = 0; y < h; ++y) {
		const auto *scan = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; ++x) {
			if (qAlpha(scan[x]) > 8) {
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
	}

	if (maxX < minX || maxY < minY) {
		return GenerateFallbackBadge();
	}
	return img.copy(QRect(QPoint(minX, minY), QPoint(maxX, maxY)));
}

QImage ClientBadgeDetector::AdaptBadgeForSurface(
		const QImage &cleanBadge,
		bool darkSurface,
		int targetPixelSize,
		std::optional<QColor> tintColor,
		const QColor &surfaceBackground) {
	if (cleanBadge.isNull() || targetPixelSize <= 0) {
		return {};
	}

	QImage scaled = cleanBadge.scaled(
		targetPixelSize,
		targetPixelSize,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);

	QColor resolved = tintColor.value_or(st::dialogsVerifiedIconBg->c);
	if (surfaceBackground.isValid()) {
		const double bgLuma = PerceivedLuminance(surfaceBackground);
		const double fgLuma = PerceivedLuminance(resolved);
		if (std::abs(fgLuma - bgLuma) < 60.0) {
			if (bgLuma < 128.0) {
				resolved = QColor(
					std::clamp(resolved.red() + 90, 0, 255),
					std::clamp(resolved.green() + 90, 0, 255),
					std::clamp(resolved.blue() + 90, 0, 255));
			} else {
				resolved = QColor(
					std::clamp(resolved.red() - 90, 0, 255),
					std::clamp(resolved.green() - 90, 0, 255),
					std::clamp(resolved.blue() - 90, 0, 255));
			}
		}
	}

	const int tr = resolved.red();
	const int tg = resolved.green();
	const int tb = resolved.blue();
	for (int y = 0; y < scaled.height(); ++y) {
		auto *row = reinterpret_cast<QRgb*>(scaled.scanLine(y));
		for (int x = 0; x < scaled.width(); ++x) {
			const int a = qAlpha(row[x]);
			if (a == 0) {
				continue;
			}
			row[x] = qRgba(tr, tg, tb, a);
		}
	}

	return scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage ClientBadgeDetector::StampAvatarWithClientMarker(
		const QImage &source,
		const std::optional<NicknameGradientConfig> &gradient) {
	if (source.isNull()) {
		return {};
	}

	const int side = std::min(source.width(), source.height());
	const QImage square = source.copy(
		(source.width() - side) / 2,
		(source.height() - side) / 2,
		side,
		side);

	QImage out = square.scaled(
		kUploadAvatarDim,
		kUploadAvatarDim,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);

	if (!DetectMarkerInAvatarImage(out)) {
		for (int y = 0; y < out.height(); ++y) {
			auto *row = reinterpret_cast<QRgb*>(out.scanLine(y));
			for (int x = 0; x < out.width(); ++x) {
				const int r = std::max(0, qRed(row[x]) - 1);
				const int g = std::max(0, qGreen(row[x]) - 1);
				const int b = std::max(0, qBlue(row[x]) - 1);
				row[x] = qRgb(r, g, b);
			}
		}
	}

	const int cornerBlock = std::max(8, kUploadAvatarDim / 20);
	const QRgb markerRgb = qRgb(kMarkerR, kMarkerG, kMarkerB);
	for (int y = 0; y < cornerBlock; ++y) {
		auto *topRow = reinterpret_cast<QRgb*>(out.scanLine(y));
		auto *botRow = reinterpret_cast<QRgb*>(
			out.scanLine(kUploadAvatarDim - 1 - y));
		for (int x = 0; x < cornerBlock; ++x) {
			topRow[x] = markerRgb;
			botRow[kUploadAvatarDim - 1 - x] = markerRgb;
		}
	}

	const auto payload = EncodeGradientPayload(
		gradient.value_or(NicknameGradientConfig{}));
	const auto &tiles = GradientTiles();
	for (int i = 0; i != kGradientDataTiles; ++i) {
		const auto &tile = tiles[i];
		const auto nibbleIndex = i % kGradientCopies;
		const auto nibble = (payload[nibbleIndex / 2]
			>> ((nibbleIndex % 2) ? 0 : 4)) & 0xF;
		const auto rgb = kGradientPalette[nibble];
		const auto originX = (tile.corner == 1 || tile.corner == 3)
			? (kUploadAvatarDim - kGradientCornerSize)
			: 0;
		const auto originY = (tile.corner >= 2)
			? (kUploadAvatarDim - kGradientCornerSize)
			: 0;
		const auto x0 = originX + tile.x * kGradientTileSize;
		const auto y0 = originY + tile.y * kGradientTileSize;
		for (int y = 0; y != kGradientTileSize; ++y) {
			auto *row = reinterpret_cast<QRgb*>(out.scanLine(y0 + y));
			for (int x = 0; x != kGradientTileSize; ++x) {
				row[x0 + x] = rgb;
			}
		}
	}

	return out;
}

std::optional<NicknameGradientConfig>
ClientBadgeDetector::DetectGradientInAvatarImage(
		const QImage &avatarSquare) {
	if (avatarSquare.isNull()
		|| avatarSquare.width() < 80
		|| avatarSquare.height() < 80
		|| !DetectMarkerInAvatarImage(avatarSquare)) {
		return std::nullopt;
	}

	const auto &tiles = GradientTiles();
	auto first = std::array<uint8, kGradientCopies>{};
	auto second = std::array<uint8, kGradientCopies>{};
	for (int i = 0; i != kGradientCopies; ++i) {
		first[i] = uint8(NearestGradientPaletteIndex(
			SampleGradientTile(avatarSquare, tiles[i])));
		second[i] = uint8(NearestGradientPaletteIndex(
			SampleGradientTile(avatarSquare, tiles[i + kGradientCopies])));
	}

	const auto build = [&](
			const std::array<uint8, kGradientCopies> &nibbles) {
		auto payload = std::array<uint8, kGradientPayloadBytes>{};
		for (int i = 0; i != kGradientCopies; ++i) {
			payload[i / 2] |= uint8(nibbles[i]
				<< ((i % 2) ? 0 : 4));
		}
		return payload;
	};

	auto nibbles = first;
	auto ambiguous = std::vector<int>();
	for (int i = 0; i != kGradientCopies; ++i) {
		if (first[i] != second[i]) {
			ambiguous.push_back(i);
		}
	}
	if (const auto decoded = DecodeGradientPayload(build(nibbles))) {
		return decoded;
	}
	if (ambiguous.empty() || ambiguous.size() > 12) {
		return std::nullopt;
	}
	for (auto mask = 1u; mask != (1u << ambiguous.size()); ++mask) {
		auto candidate = nibbles;
		for (auto bit = 0; bit != int(ambiguous.size()); ++bit) {
			if (mask & (1u << bit)) {
				candidate[ambiguous[bit]] = second[ambiguous[bit]];
			}
		}
		if (const auto decoded = DecodeGradientPayload(build(candidate))) {
			return decoded;
		}
	}
	return std::nullopt;
}

bool ClientBadgeDetector::DetectMarkerInAvatarImage(
		const QImage &avatarSquare) {
	if (avatarSquare.isNull()
		|| avatarSquare.width() < 32
		|| avatarSquare.height() < 32) {
		return false;
	}

	const int w = avatarSquare.width();
	const int h = avatarSquare.height();
	const int sampleSide = std::clamp(w / 40, 2, 8);

	const auto checkCorner = [&](int startX, int startY) {
		int sumR = 0, sumG = 0, sumB = 0, count = 0;
		for (int y = startY; y < startY + sampleSide; ++y) {
			for (int x = startX; x < startX + sampleSide; ++x) {
				const QRgb px = avatarSquare.pixel(x, y);
				sumR += qRed(px);
				sumG += qGreen(px);
				sumB += qBlue(px);
				++count;
			}
		}
		const int avgR = (sumR + count / 2) / count;
		const int avgG = (sumG + count / 2) / count;
		const int avgB = (sumB + count / 2) / count;
		return (std::abs(avgR - kMarkerR) <= kJpegTolerance)
			&& (std::abs(avgG - kMarkerG) <= kJpegTolerance)
			&& (std::abs(avgB - kMarkerB) <= kJpegTolerance);
	};

	return checkCorner(0, 0) && checkCorner(w - sampleSide, h - sampleSide);
}

bool ClientBadgeDetector::ensurePeerEvaluated(
		not_null<UserData*> user,
		PeerCacheEntry &entry) {
	if (entry.evaluated) {
		return true;
	}
	const auto *cloudImg = user->userpicCloudImage(entry.view);
	if (!cloudImg || cloudImg->isNull()) {
		_pendingDownloadPeers.emplace(user->id);
		user->loadUserpic();
		return false;
	}
	_pendingDownloadPeers.remove(user->id);
	entry.detected = DetectMarkerInAvatarImage(*cloudImg);
	const auto decoded = entry.detected
		? DetectGradientInAvatarImage(*cloudImg)
		: std::optional<NicknameGradientConfig>();
	entry.gradient = (decoded && !decoded->colors.empty())
		? decoded
		: std::optional<NicknameGradientConfig>();
	entry.evaluated = true;
	if (user->isSelf() && decoded) {
		_selfGradient = decoded->colors.empty()
			? std::nullopt
			: decoded;
	}
	_gradientChanges.fire_copy(user->id);
	return true;
}

bool ClientBadgeDetector::hasCustomClientBadge(not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	if (!user) {
		return false;
	}

	if (user->userpicPhotoUnknown()) {
		user->updateFull();
		return false;
	}

	const auto currentPhotoId = user->userpicPhotoId();
	if (!currentPhotoId) {
		_cache.remove(user->id);
		_pendingDownloadPeers.remove(user->id);
		return false;
	}

	auto &entry = _cache[user->id];
	if (entry.checkedPhotoId != currentPhotoId) {
		entry = PeerCacheEntry{ .checkedPhotoId = currentPhotoId };
	}

	if (!ensurePeerEvaluated(user, entry)) {
		return false;
	}
	return entry.detected;
}

std::optional<NicknameGradientConfig> ClientBadgeDetector::nicknameGradientFor(
		not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	if (!user) {
		return std::nullopt;
	}
	if (user->userpicPhotoUnknown()) {
		user->updateFull();
		return std::nullopt;
	}
	const auto currentPhotoId = user->userpicPhotoId();
	if (!currentPhotoId) {
		_cache.remove(user->id);
		_pendingDownloadPeers.remove(user->id);
		return std::nullopt;
	}
	auto &entry = _cache[user->id];
	if (entry.checkedPhotoId != currentPhotoId) {
		entry = PeerCacheEntry{ .checkedPhotoId = currentPhotoId };
	}
	if (!ensurePeerEvaluated(user, entry)) {
		return std::nullopt;
	}
	return entry.gradient;
}

void ClientBadgeDetector::pollPendingPeers() {
	if (_pendingDownloadPeers.empty()) {
		return;
	}
	const auto copy = _pendingDownloadPeers;
	for (const auto peerId : copy) {
		const auto peer = _session->data().peerLoaded(peerId);
		if (!peer) {
			_pendingDownloadPeers.remove(peerId);
			continue;
		}
		if (hasCustomClientBadge(peer)) {
			_detectionChanges.fire_copy(peerId);
		}
	}
}

int ClientBadgeDetector::badgeWidth(not_null<PeerData*> peer) {
	return hasCustomClientBadge(peer) ? style::ConvertScale(20) : 0;
}

QImage ClientBadgeDetector::badgePreview(
		const QColor &surfaceBackground,
		int logicalSize,
		int scale,
		std::optional<QColor> tint) {
	const auto &src = _cleanLightStrokeBadge.isNull()
		? _cleanDarkStrokeBadge
		: _cleanLightStrokeBadge;
	if (src.isNull() || logicalSize <= 0) {
		return {};
	}
	const auto dpr = std::max(1, scale);
	auto result = AdaptBadgeForSurface(
		src,
		PerceivedLuminance(surfaceBackground) < 128.0,
		logicalSize * dpr,
		tint,
		surfaceBackground);
	result.setDevicePixelRatio(dpr);
	return result;
}

rpl::producer<PeerId> ClientBadgeDetector::badgeDetectedChanges() const {
	return _detectionChanges.events();
}

rpl::producer<PeerId> ClientBadgeDetector::gradientChanges() const {
	return _gradientChanges.events();
}

int ClientBadgeDetector::drawBadgeGetWidth(
		QPainter &p,
		int x,
		int y,
		int nameHeight,
		not_null<PeerData*> peer,
		const QColor &surfaceBackground,
		std::optional<QColor> desiredTint) {
	if (!hasCustomClientBadge(peer)) {
		return 0;
	}

	const int logicalSize = style::ConvertScale(14);
	const int dpr = static_cast<int>(
		std::ceil(p.device()->devicePixelRatioF()));
	const int pixelSize = logicalSize * std::max(1, dpr);

	if (_cachedPixelSize != pixelSize) {
		_tintedIconCache.clear();
		_cachedPixelSize = pixelSize;
	}

	const bool darkSurface = (PerceivedLuminance(surfaceBackground) < 128.0);
	const QColor baseTint = desiredTint.value_or(st::dialogsVerifiedIconBg->c);
	const uint64 cacheKey = (uint64(baseTint.rgba()) << 32)
		| (uint64(surfaceBackground.rgba() & 0x00FFFFFFU) << 1)
		| (darkSurface ? 1ULL : 0ULL);

	auto &icon = _tintedIconCache[cacheKey];
	if (icon.isNull()) {
		if (_tintedIconCache.size() > 64) {
			_tintedIconCache.clear();
		}
		const auto &src = _cleanLightStrokeBadge.isNull()
			? _cleanDarkStrokeBadge
			: _cleanLightStrokeBadge;
		auto built = AdaptBadgeForSurface(
			src,
			darkSurface,
			pixelSize,
			baseTint,
			surfaceBackground);
		built.setDevicePixelRatio(std::max(1, dpr));
		_tintedIconCache[cacheKey] = std::move(built);
	}
	const auto &finalIcon = _tintedIconCache[cacheKey];
	if (finalIcon.isNull()) {
		return 0;
	}

	const int padLeft = style::ConvertScale(3);
	const QSize logicalIconSize = finalIcon.size()
		/ finalIcon.devicePixelRatio();
	const int drawY = y + std::max(0, (nameHeight - logicalIconSize.height()) / 2);

	p.drawImage(QPoint(x + padLeft, drawY), finalIcon);
	return padLeft + logicalIconSize.width() + style::ConvertScale(2);
}

void ClientBadgeDetector::stampUploadImage(QImage &image) {
	if (image.isNull()) {
		return;
	}
	const auto side = std::min(image.width(), image.height());
	const auto gradient = currentSelfGradient();
	if (image.width() == side && image.height() == side) {
		const auto marker = DetectMarkerInAvatarImage(image);
		const auto existing = marker
			? DetectGradientInAvatarImage(image)
			: std::optional<NicknameGradientConfig>();
		const auto existingConfig = (existing && !existing->colors.empty())
			? existing
			: std::optional<NicknameGradientConfig>();
		if (marker && existingConfig == gradient) {
			return;
		}
	}
	image = StampAvatarWithClientMarker(image, gradient);
}

void ClientBadgeDetector::stampSelfAvatar(
		Fn<void(bool ok, QString message)> done) {
	stampSelfAvatarWithGradient(currentSelfGradient(), std::move(done));
}

void ClientBadgeDetector::applySelfAvatarGradient(
		std::optional<NicknameGradientConfig> gradient,
		Fn<void(bool ok, QString message)> done) {
	_selfGradient = gradient;
	stampSelfAvatarWithGradient(std::move(gradient), std::move(done));
}

std::optional<NicknameGradientConfig>
ClientBadgeDetector::currentSelfGradient() {
	const auto self = _session->user();
	Ui::PeerUserpicView view;
	const auto *cloud = self->userpicCloudImage(view);
	if (cloud && !cloud->isNull()) {
		if (const auto decoded = DetectGradientInAvatarImage(*cloud)) {
			return decoded->colors.empty()
				? std::nullopt
				: decoded;
		}
	}
	if (const auto bio = ParseBioGradient(self->about())) {
		return bio;
	}
	return _selfGradient;
}

void ClientBadgeDetector::stampSelfAvatarWithGradient(
		std::optional<NicknameGradientConfig> gradient,
		Fn<void(bool ok, QString message)> done) {
	if (_stampInProgress) {
		if (done) {
			done(
				false,
				QString::fromUtf8("Обновление аватарки уже выполняется..."));
		}
		return;
	}

	const auto self = _session->user();
	Ui::PeerUserpicView view;
	const auto *cloudImg = self->userpicCloudImage(view);
	if (!cloudImg || cloudImg->isNull()) {
		self->loadUserpic();
		if (done) {
			done(
				false,
				QString::fromUtf8(
					"Аватарка ещё подгружается или не установлена. Повторите через пару секунд."));
		}
		return;
	}

	const auto marker = DetectMarkerInAvatarImage(*cloudImg);
	if (marker) {
		const auto existing = DetectGradientInAvatarImage(*cloudImg);
		const auto existingConfig = (existing && !existing->colors.empty())
			? existing
			: std::optional<NicknameGradientConfig>();
		if (existingConfig == gradient) {
			_lastVerifiedSelfPhotoId = self->userpicPhotoId();
			if (done) {
				done(
					true,
					QString::fromUtf8(
						"На вашей аватарке уже установлен нужный маркер!"));
			}
			return;
		}
	}

	auto videoBytes = QByteArray();
	if (self->userpicHasVideo()) {
		const auto photo = _session->data().photo(self->userpicPhotoId());
		if (photo->hasVideo()) {
			if (!_videoMedia || (_videoMedia->owner() != photo)) {
				_videoMedia = photo->createMediaView();
			}
			videoBytes = _videoMedia->videoContent(Data::PhotoSize::Large);
			if (videoBytes.isEmpty()) {
				photo->loadVideo(
					Data::PhotoSize::Large,
					self->userpicOrigin());
				_waitingForVideo = true;
				_videoWaitChecks = 0;
				_waitingDone = std::move(done);
				_waitingGradient = std::move(gradient);
				return;
			}
		} else {
			_videoMedia = nullptr;
		}
	} else {
		_videoMedia = nullptr;
	}
	_waitingForVideo = false;
	_videoWaitChecks = 0;
	_waitingDone = nullptr;
	_waitingGradient = std::nullopt;

	const QImage stamped = StampAvatarWithClientMarker(*cloudImg, gradient);
	QByteArray jpegBytes;
	{
		QBuffer buf(&jpegBytes);
		buf.open(QIODevice::WriteOnly);
		stamped.save(&buf, "JPG", 98);
	}

	if (jpegBytes.isEmpty()) {
		if (done) {
			done(false, QString::fromUtf8("Ошибка кодирования аватарки."));
		}
		return;
	}

	_stampInProgress = true;
	const uint64 fileId = base::RandomValue<uint64>();
	const uint64 videoFileId = base::RandomValue<uint64>();
	const int totalParts = (jpegBytes.size() + kPartSize - 1) / kPartSize;
	const int videoTotalParts = videoBytes.isEmpty()
		? 0
		: (videoBytes.size() + kPartSize - 1) / kPartSize;

	const auto sendUpload = [=, this] {
		const auto inputFile = MTP_inputFile(
			MTP_long(fileId),
			MTP_int(totalParts),
			MTP_string("avatar.jpg"),
			MTP_bytes(""));
		const auto videoInput = videoBytes.isEmpty()
			? MTPInputFile()
			: MTP_inputFile(
				MTP_long(videoFileId),
				MTP_int(videoTotalParts),
				MTP_string("avatar.mp4"),
				MTP_bytes(""));

		using Flag = MTPphotos_UploadProfilePhoto::Flag;
		const auto flags = videoBytes.isEmpty()
			? Flag::f_file
			: (Flag::f_file | Flag::f_video | Flag::f_video_start_ts);
		_api.request(MTPphotos_UploadProfilePhoto(
			MTP_flags(flags),
			MTPInputUser(),
			inputFile,
			videoInput,
			MTP_double(0.),
			MTPVideoSize()
		)).done([=, this](const MTPphotos_Photo &result) {
			const auto &data = result.data();
			_session->data().processUsers(data.vusers());
			const auto photo = _session->data().processPhoto(
				data.vphoto());
			const auto newPhotoId = photo->id;
			_lastVerifiedSelfPhotoId = newPhotoId;
			_session->storage().add(Storage::UserPhotosAddNew(
				peerToUser(self->id),
				newPhotoId));
			_videoMedia = nullptr;
			deletePrevAvatar(newPhotoId, done);
		}).fail([=, this](const MTP::Error &err) {
			_stampInProgress = false;
			_videoMedia = nullptr;
			if (done) {
				done(
					false,
					QString::fromUtf8("Ошибка загрузки аватарки: %1")
						.arg(err.type()));
			}
		}).send();
	};

	auto uploadPart = std::make_shared<Fn<void(int)>>();
	auto uploadVideoPart = std::make_shared<Fn<void(int)>>();
	*uploadPart = [=, this](int partIdx) {
		if (partIdx >= totalParts) {
			if (videoBytes.isEmpty()) {
				sendUpload();
			} else {
				(*uploadVideoPart)(0);
			}
			return;
		}

		const auto chunk = jpegBytes.mid(partIdx * kPartSize, kPartSize);
		_api.request(MTPupload_SaveFilePart(
			MTP_long(fileId),
			MTP_int(partIdx),
			MTP_bytes(chunk)
		)).done([=](const MTPBool &) {
			(*uploadPart)(partIdx + 1);
		}).fail([=, this](const MTP::Error &err) {
			_stampInProgress = false;
			if (done) {
				done(
					false,
					QString::fromUtf8("Ошибка передачи файла: %1")
						.arg(err.type()));
			}
		}).send();
	};
	*uploadVideoPart = [=, this](int partIdx) {
		if (partIdx >= videoTotalParts) {
			sendUpload();
			return;
		}

		const auto chunk = videoBytes.mid(partIdx * kPartSize, kPartSize);
		_api.request(MTPupload_SaveFilePart(
			MTP_long(videoFileId),
			MTP_int(partIdx),
			MTP_bytes(chunk)
		)).done([=](const MTPBool &) {
			(*uploadVideoPart)(partIdx + 1);
		}).fail([=, this](const MTP::Error &err) {
			_stampInProgress = false;
			if (done) {
				done(
					false,
					QString::fromUtf8("Ошибка передачи видео-аватарки: %1")
						.arg(err.type()));
			}
		}).send();
	};

	(*uploadPart)(0);
}

void ClientBadgeDetector::deletePrevAvatar(
		PhotoId newlyUploadedId,
		Fn<void(bool, QString)> done) {
	const auto self = _session->user();
	_api.request(MTPphotos_GetUserPhotos(
		self->inputUser(),
		MTP_int(0),
		MTP_long(0),
		MTP_int(5)
	)).done([=, this](const MTPphotos_Photos &res) {
		const QVector<MTPPhoto> *photos = nullptr;
		res.match([&](const MTPDphotos_photos &d) {
			photos = &d.vphotos().v;
		}, [&](const MTPDphotos_photosSlice &d) {
			photos = &d.vphotos().v;
		});

		QVector<MTPInputPhoto> toDelete;
		PhotoId deletedPhotoId = 0;
		if (photos && photos->size() >= 2) {
			for (int i = 1; i < photos->size(); ++i) {
				(*photos)[i].match([&](const MTPDphoto &p) {
					if (PhotoId(p.vid().v) != newlyUploadedId) {
						deletedPhotoId = PhotoId(p.vid().v);
						toDelete.push_back(MTP_inputPhoto(
							p.vid(),
							p.vaccess_hash(),
							p.vfile_reference()));
					}
				}, [&](const MTPDphotoEmpty &) {});
				if (!toDelete.isEmpty()) {
					break;
				}
			}
		}

		if (toDelete.isEmpty()) {
			_stampInProgress = false;
			_cache.remove(self->id);
			_detectionChanges.fire_copy(self->id);
			if (done) {
				done(
					true,
					QString::fromUtf8("Аватарка обновлена с маркером #1a0200!"));
			}
			return;
		}

		_api.request(MTPphotos_DeletePhotos(
			MTP_vector<MTPInputPhoto>(toDelete)
		)).done([=, this](const MTPVector<MTPlong> &) {
			if (deletedPhotoId) {
				_session->storage().remove(Storage::UserPhotosRemoveOne(
					peerToUser(self->id),
					deletedPhotoId));
			}
			_stampInProgress = false;
			_cache.remove(self->id);
			_detectionChanges.fire_copy(self->id);
			if (done) {
				done(
					true,
					QString::fromUtf8(
						"Маркер #1a0200 установлен, старая аватарка удалена!"));
			}
		}).fail([=, this](const MTP::Error &) {
			_stampInProgress = false;
			_cache.remove(self->id);
			_detectionChanges.fire_copy(self->id);
			if (done) {
				done(
					true,
					QString::fromUtf8(
						"Новая аватарка с #1a0200 установлена!"));
			}
		}).send();
	}).fail([=, this](const MTP::Error &) {
		_stampInProgress = false;
		_cache.remove(self->id);
		_detectionChanges.fire_copy(self->id);
		if (done) {
			done(
				true,
				QString::fromUtf8("Новая аватарка с #1a0200 установлена!"));
		}
	}).send();
}

ClientBadgeDetector &ClientBadgeFor(not_null<Main::Session*> session) {
	static base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<ClientBadgeDetector>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<ClientBadgeDetector>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
