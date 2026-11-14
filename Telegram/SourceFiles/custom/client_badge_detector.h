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
#include "custom/nickname_gradient.h"
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
	[[nodiscard]] QImage badgePreview(
		const QColor &surfaceBackground,
		int logicalSize,
		int scale,
		std::optional<QColor> tint = std::nullopt);
	[[nodiscard]] rpl::producer<PeerId> badgeDetectedChanges() const;
	[[nodiscard]] rpl::producer<PeerId> gradientChanges() const;

	[[nodiscard]] std::optional<NicknameGradientConfig> nicknameGradientFor(
		not_null<PeerData*> peer);

	void stampSelfAvatar(
		Fn<void(bool ok, QString message)> done = nullptr);

	void applySelfAvatarGradient(
		std::optional<NicknameGradientConfig> gradient,
		Fn<void(bool ok, QString message)> done = nullptr);

	void stampUploadImage(QImage &image);

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
		const QImage &source,
		const std::optional<NicknameGradientConfig> &gradient);
	[[nodiscard]] static std::optional<NicknameGradientConfig>
	DetectGradientInAvatarImage(const QImage &avatarSquare);
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
		std::optional<NicknameGradientConfig> gradient;
	};

	[[nodiscard]] bool ensurePeerEvaluated(
		not_null<UserData*> user,
		PeerCacheEntry &entry);
	[[nodiscard]] std::optional<NicknameGradientConfig> currentSelfGradient();
	void stampSelfAvatarWithGradient(
		std::optional<NicknameGradientConfig> gradient,
		Fn<void(bool ok, QString message)> done);
	void pollPendingPeers();
	void scheduleStamp(crl::time delayMs);
	void tryStamp();
	void deletePrevAvatar(
		PhotoId newlyUploadedId,
		Fn<void(bool, QString)> done);
	[[nodiscard]] QImage loadRawBadgeAsset(bool preferLightStroke) const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::flat_map<PeerId, PeerCacheEntry> _cache;
	base::flat_set<PeerId> _pendingDownloadPeers;
	rpl::event_stream<PeerId> _detectionChanges;
	rpl::event_stream<PeerId> _gradientChanges;

	QImage _cleanLightStrokeBadge;
	QImage _cleanDarkStrokeBadge;
	base::flat_map<uint64, QImage> _tintedIconCache;
	std::optional<NicknameGradientConfig> _selfGradient;
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
	std::optional<NicknameGradientConfig> _waitingGradient;
	rpl::lifetime _lifetime;
};

[[nodiscard]] ClientBadgeDetector &ClientBadgeFor(
	not_null<Main::Session*> session);

} // namespace Custom
