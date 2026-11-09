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
#include <queue>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Custom {
namespace {

constexpr auto kUploadAvatarDim = 640;
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
				if (ready) {
					stampSelfAvatar(std::move(done));
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
		_lastVerifiedSelfPhotoId = photoId;
		_autoStampPending = false;
		return;
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

QImage ClientBadgeDetector::StampAvatarWithClientMarker(const QImage &source) {
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

	for (int y = 0; y < out.height(); ++y) {
		auto *row = reinterpret_cast<QRgb*>(out.scanLine(y));
		for (int x = 0; x < out.width(); ++x) {
			const int r = std::max(0, qRed(row[x]) - 1);
			const int g = std::max(0, qGreen(row[x]) - 1);
			const int b = std::max(0, qBlue(row[x]) - 1);
			row[x] = qRgb(r, g, b);
		}
	}

	constexpr int kCornerBlock = 32;
	const QRgb markerRgb = qRgb(kMarkerR, kMarkerG, kMarkerB);
	for (int y = 0; y < kCornerBlock; ++y) {
		auto *topRow = reinterpret_cast<QRgb*>(out.scanLine(y));
		auto *botRow = reinterpret_cast<QRgb*>(
			out.scanLine(kUploadAvatarDim - 1 - y));
		for (int x = 0; x < kCornerBlock; ++x) {
			topRow[x] = markerRgb;
			botRow[kUploadAvatarDim - 1 - x] = markerRgb;
		}
	}

	return out;
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
		entry = PeerCacheEntry{
			.checkedPhotoId = currentPhotoId,
			.evaluated = false,
			.detected = false,
		};
	}

	if (entry.evaluated) {
		return entry.detected;
	}

	const auto *cloudImg = user->userpicCloudImage(entry.view);
	if (!cloudImg || cloudImg->isNull()) {
		_pendingDownloadPeers.emplace(user->id);
		user->loadUserpic();
		return false;
	}

	_pendingDownloadPeers.remove(user->id);
	entry.detected = DetectMarkerInAvatarImage(*cloudImg);
	entry.evaluated = true;
	return entry.detected;
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

rpl::producer<PeerId> ClientBadgeDetector::badgeDetectedChanges() const {
	return _detectionChanges.events();
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

void ClientBadgeDetector::stampSelfAvatar(
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

	if (DetectMarkerInAvatarImage(*cloudImg)) {
		_lastVerifiedSelfPhotoId = self->userpicPhotoId();
		if (done) {
			done(
				true,
				QString::fromUtf8(
					"На вашей аватарке уже установлен маркер #1a0200!"));
		}
		return;
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

	const QImage stamped = StampAvatarWithClientMarker(*cloudImg);
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
