/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/timer.h"
#include "data/data_peer_id.h"
#include "mtproto/sender.h"
#include "ui/userpic_view.h"

#include <memory>
#include <optional>

class PeerData;
class UserData;

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Custom {

class ClientBadgeDetector final {
public:
	static constexpr int kMarkerR = 0x1a;
	static constexpr int kMarkerG = 0x02;
	static constexpr int kMarkerB = 0x00;
	static constexpr int kJpegTolerance = 10;

	explicit ClientBadgeDetector(not_null<Main::Session*> session);

	[[nodiscard]] bool hasCustomClientBadge(not_null<PeerData*> peer);
	[[nodiscard]] int badgeWidth(not_null<PeerData*> peer);
	[[nodiscard]] rpl::producer<PeerId> badgeDetectedChanges() const;

	void stampAndReuploadSelfAvatar(
		Fn<void(bool ok, QString message)> done = nullptr);

	int drawBadgeGetWidth(
		QPainter &p,
		int x,
		int y,
		int nameHeight,
		not_null<PeerData*> peer,
		const QColor &surfaceBackground,
		std::optional<QColor> desiredTint = std::nullopt);

	void reloadBadgeSourceImage();

	[[nodiscard]] static QImage StampAvatarWithClientMarker(
		const QImage &source);
	[[nodiscard]] static bool DetectMarkerInAvatarImage(
		const QImage &avatarSquare);
	[[nodiscard]] static QImage CropAndCleanBadgeImage(const QImage &rawBadge);
	[[nodiscard]] static QImage AdaptBadgeForSurface(
		const QImage &cleanBadge,
		bool darkSurface,
		int targetPixelSize,
		std::optional<QColor> tintColor = std::nullopt,
		const QColor &surfaceBackground = QColor());

private:
	struct PeerCacheEntry {
		PhotoId checkedPhotoId = 0;
		Ui::PeerUserpicView view;
		bool evaluated = false;
		bool detected = false;
	};

	void pollPendingPeers();
	void scheduleAutoStampCheck(crl::time delayMs);
	void maybeAutoStampSelfAvatar();
	void deletePreviousSelfAvatar(
		PhotoId newlyUploadedId,
		Fn<void(bool, QString)> done);
	[[nodiscard]] QImage loadRawBadgeAsset(bool preferLightStroke) const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::flat_map<PeerId, PeerCacheEntry> _cache;
	base::flat_set<PeerId> _pendingDownloadPeers;
	rpl::event_stream<PeerId> _detectionChanges;

	QImage _cleanLightStrokeBadge;
	QImage _cleanDarkStrokeBadge;
	base::flat_map<uint64, QImage> _tintedIconCache;
	int _cachedPixelSize = 0;
	bool _stampInProgress = false;
	bool _waitingForVideo = false;
	bool _autoStampPending = false;
	PhotoId _lastVerifiedSelfPhotoId = 0;
	PhotoId _lastAttemptPhotoId = 0;
	std::shared_ptr<Data::PhotoMedia> _videoMedia;
	base::Timer _autoStampTimer;
	int _autoStampAttempts = 0;
	int _videoWaitChecks = 0;
	Fn<void(bool, QString)> _waitingDone;
	rpl::lifetime _lifetime;
};

[[nodiscard]] ClientBadgeDetector &ClientBadgeFor(
	not_null<Main::Session*> session);

} // namespace Custom
