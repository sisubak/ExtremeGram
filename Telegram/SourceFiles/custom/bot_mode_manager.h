/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "data/data_peer_id.h"
#include "mtproto/sender.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class History;
class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Custom {

struct StatsProgressSnapshot {
	PeerId peerId = 0;
	bool active = false;
	bool waitingRateLimit = false;
	bool sendingLeaderboard = false;
	int waitSecondsLeft = 0;
	int scannedMessages = 0;
	int estimatedTotal = 0;
	int uniqueUsers = 0;
	QString statusText;
};

class BotStatsOverlayWidget final : public Ui::RpWidget {
public:
	BotStatsOverlayWidget(
		QWidget *parent,
		not_null<Main::Session*> session);

	void setPeer(PeerData *peer);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void applySnapshot(const StatsProgressSnapshot &snap);

	const not_null<Main::Session*> _session;
	PeerData *_peer = nullptr;
	StatsProgressSnapshot _snapshot;
	Ui::Animations::Simple _visibilityAnim;
	Ui::Animations::Simple _barAnim;
	float64 _displayedProgress = 0.0;
	rpl::lifetime _peerLifetime;
};

class BotModeManager final {
public:
	explicit BotModeManager(not_null<Main::Session*> session);
	~BotModeManager();

	// Registry of every "!" command of the mode: command (without the "!"
	// prefix) and a short hint. Single source for the !-suggestion bar,
	// the !help reply and the settings description.
	[[nodiscard]] static const std::vector<
		std::pair<QString, QString>
	> &CommandHints();

	bool interceptCommand(
		not_null<History*> history,
		const QString &text,
		FullMsgId replyToId,
		Window::SessionController *controller = nullptr);

	[[nodiscard]] rpl::producer<StatsProgressSnapshot> progressUpdates(
		PeerId peerId) const;
	[[nodiscard]] StatsProgressSnapshot currentProgress(PeerId peerId) const;

private:
	struct UserStatEntry {
		PeerId peerId = 0;
		QString name;
		QString username;
		int64 count = 0;
	};

	struct ScanJob {
		not_null<PeerData*> peer;
		MsgId offsetId = 0;
		int scannedMessages = 0;
		int estimatedTotal = 0;
		bool active = false;
		bool liveTracking = false;
		bool waitingRateLimit = false;
		bool sendingLeaderboard = false;
		int waitSecondsLeft = 0;
		mtpRequestId currentRequestId = 0;
		std::vector<QString> pendingOutgoingChunks;
		base::flat_set<MsgId> countedMsgIds;
		base::flat_map<PeerId, UserStatEntry> counts;
	};

	enum class WhoisStyle {
		Full,     // name + ID + registration + group join date
		Account,  // name + ID + registration date only
		Vxod,     // group join date only (megagroups)
	};

	bool handleMuteCommand(
		not_null<History*> history,
		const QString &argsText,
		FullMsgId replyToId,
		Window::SessionController *controller);
	bool handleStatsCommand(not_null<History*> history);
	bool stopScan(not_null<History*> history);
	bool handleWhoisCommand(
		not_null<History*> history,
		const QString &argsText,
		FullMsgId replyToId,
		Window::SessionController *controller,
		WhoisStyle style = WhoisStyle::Full);
	bool handleIgnoreCommand(
		not_null<History*> history,
		const QString &argsText,
		FullMsgId replyToId,
		Window::SessionController *controller,
		bool unignore);

	void requestBatch(PeerId peerId);
	void scheduleRetry(PeerId peerId, int seconds, Fn<void()> retryFn);
	void finishScan(PeerId peerId);
	void sendChunk(PeerId peerId);
	void sendWithRetry(
		not_null<PeerData*> peer,
		const QString &text,
		FullMsgId replyTo,
		Fn<void()> onDone = nullptr);

	void onItemAdded(not_null<HistoryItem*> item);
	void loadStats(not_null<PeerData*> peer);
	void saveStats(const ScanJob &job) const;
	void reportProgress(const ScanJob &job, const QString &status);

	[[nodiscard]] static std::optional<int> ParseDurationSeconds(
		const QString &token);
	[[nodiscard]] static int ExtractWaitSeconds(const QString &errorType);
	[[nodiscard]] QString statsDirPath() const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<PeerId, std::unique_ptr<ScanJob>> _jobs;
	base::flat_map<PeerId, std::unique_ptr<base::Timer>> _jobTimers;
	base::flat_map<PeerId, Fn<void()>> _retryCallbacks;
	base::Timer _countdownTicker;
	base::Timer _periodicFlushTimer;
	base::flat_set<PeerId> _dirtyLivePeers;
	rpl::event_stream<StatsProgressSnapshot> _progressStream;
	rpl::lifetime _lifetime;
};

[[nodiscard]] BotModeManager &BotModeFor(not_null<Main::Session*> session);

[[nodiscard]] std::optional<int64> ApproximateRegistrationEpoch(
	int64 userId);
[[nodiscard]] QString FormatRegistrationDay(int64 epoch);
[[nodiscard]] QString FormatJoinDateTime(int64 epoch);

} // namespace Custom
