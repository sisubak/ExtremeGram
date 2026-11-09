/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/bot_mode_manager.h"
#include "custom/smooth_animations.h"

#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/enhanced_settings.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "styles/palette.h"
#include "styles/style_basic.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

#include <algorithm>
#include <cmath>

namespace Custom {
namespace {

constexpr auto kBatchSize = 100;
constexpr auto kBatchDelayMs = crl::time(380);
constexpr auto kMaxMessageChars = 3700;

[[nodiscard]] QString FormatDurationHuman(int seconds) {
	if (seconds % 86400 == 0) {
		return QString::number(seconds / 86400) + u"д"_q;
	} else if (seconds % 3600 == 0) {
		return QString::number(seconds / 3600) + u"ч"_q;
	} else if (seconds % 60 == 0) {
		return QString::number(seconds / 60) + u"м"_q;
	}
	return QString::number(seconds) + u"с"_q;
}

} // namespace

BotStatsOverlayWidget::BotStatsOverlayWidget(
	QWidget *parent,
	not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session) {
	setFixedHeight(style::ConvertScale(54));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();
}

void BotStatsOverlayWidget::setPeer(PeerData *peer) {
	if (_peer == peer) {
		return;
	}
	_peer = peer;
	_peerLifetime.destroy();
	_visibilityAnim.stop();
	_barAnim.stop();
	_displayedProgress = 0.0;

	if (!_peer) {
		_snapshot = {};
		hide();
		return;
	}

	auto &mgr = BotModeFor(_session);
	applySnapshot(mgr.currentProgress(_peer->id));

	mgr.progressUpdates(
		_peer->id
	) | rpl::on_next([=](const StatsProgressSnapshot &snap) {
		applySnapshot(snap);
	}, _peerLifetime);
}

void BotStatsOverlayWidget::applySnapshot(const StatsProgressSnapshot &snap) {
	const auto wasActive = _snapshot.active;
	_snapshot = snap;

	if (snap.active != wasActive) {
		if (snap.active) {
			show();
			raise();
		}
		_visibilityAnim.start(
			[=] {
				update();
				if (!_snapshot.active && !_visibilityAnim.animating()) {
					hide();
				}
			},
			snap.active ? 0.0 : 1.0,
			snap.active ? 1.0 : 0.0,
			260,
			Animations::CubicSmoothOut);
	}

	const auto targetProgress = (snap.estimatedTotal > 0)
		? std::clamp(
			static_cast<float64>(snap.scannedMessages) / snap.estimatedTotal,
			0.02,
			1.0)
		: (snap.sendingLeaderboard ? 1.0 : 0.15);

	_barAnim.start(
		[=] {
			_displayedProgress = _barAnim.value(targetProgress);
			update();
		},
		_displayedProgress,
		targetProgress,
		240,
		Animations::CubicSmoothOut);

	if (!snap.active && !_visibilityAnim.animating()) {
		hide();
	}
	update();
}

void BotStatsOverlayWidget::paintEvent(QPaintEvent *) {
	if (width() <= style::ConvertScale(28) || height() <= style::ConvertScale(14)) {
		return;
	}
	const auto alpha = _visibilityAnim.value(_snapshot.active ? 1.0 : 0.0);
	if (alpha <= 0.001) {
		return;
	}

	QPainter p(this);
	PainterHighQualityEnabler hq(p);
	p.setOpacity(alpha);

	const auto marginX = style::ConvertScale(12);
	const auto marginY = style::ConvertScale(6);
	const auto cardRect = QRectF(rect()).adjusted(
		marginX,
		marginY,
		-marginX,
		-marginY);
	const auto radius = style::ConvertScaleExact(10.0);

	auto cardBg = st::windowBgOver->c;
	cardBg.setAlphaF(0.94);
	p.setPen(QPen(st::shadowFg->c, style::ConvertScaleExact(1.0)));
	p.setBrush(cardBg);
	p.drawRoundedRect(cardRect, radius, radius);

	const auto textLeft = cardRect.left() + style::ConvertScale(12);
	const auto textRight = cardRect.right() - style::ConvertScale(12);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg->c);

	const auto titleRect = QRectF(
		textLeft,
		cardRect.top() + style::ConvertScale(6),
		textRight - textLeft,
		style::ConvertScale(18));

	auto header = _snapshot.statusText;
	if (_snapshot.waitingRateLimit && _snapshot.waitSecondsLeft > 0) {
		header = QString::fromUtf8(
			"Ожидание лимита API: %1 с — обработано %2 сообщ.")
			.arg(_snapshot.waitSecondsLeft)
			.arg(_snapshot.scannedMessages);
	}
	p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, header);

	const auto barHeight = style::ConvertScaleExact(5.0);
	const auto barRect = QRectF(
		textLeft,
		cardRect.bottom() - style::ConvertScale(11),
		textRight - textLeft,
		barHeight);

	auto trackColor = st::windowSubTextFg->c;
	trackColor.setAlphaF(0.22);
	p.setPen(Qt::NoPen);
	p.setBrush(trackColor);
	p.drawRoundedRect(barRect, barHeight / 2.0, barHeight / 2.0);

	const auto fillColor = _snapshot.waitingRateLimit
		? QColor(235, 162, 42)
		: st::windowBgActive->c;
	const auto fillWidth = std::max(
		barHeight,
		barRect.width() * _displayedProgress);
	const auto fillRect = QRectF(
		barRect.left(),
		barRect.top(),
		fillWidth,
		barHeight);
	p.setBrush(fillColor);
	p.drawRoundedRect(fillRect, barHeight / 2.0, barHeight / 2.0);
}

BotModeManager::BotModeManager(not_null<Main::Session*> session)
: _session(session)
, _api(&_session->mtp())
, _countdownTicker([=] {
	auto anyWaiting = false;
	for (auto &[peerId, job] : _jobs) {
		if (job && job->waitingRateLimit && job->waitSecondsLeft > 0) {
			--job->waitSecondsLeft;
			anyWaiting = true;
			reportProgress(*job, job->sendingLeaderboard
				? QString::fromUtf8("Ожидание лимита перед отправкой...")
				: QString::fromUtf8("Ожидание лимита Telegram API..."));
		}
	}
	if (!anyWaiting) {
		_countdownTicker.cancel();
	}
})
, _periodicFlushTimer([=] {
	for (const auto peerId : base::take(_dirtyLivePeers)) {
		const auto it = _jobs.find(peerId);
		if (it != _jobs.end() && it->second) {
			saveStats(*it->second);
		}
	}
}) {
	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		onItemAdded(item);
	}, _lifetime);
}

BotModeManager::~BotModeManager() {
	for (const auto peerId : _dirtyLivePeers) {
		const auto it = _jobs.find(peerId);
		if (it != _jobs.end() && it->second) {
			saveStats(*it->second);
		}
	}
}

bool BotModeManager::interceptCommand(
		not_null<History*> history,
		const QString &text,
		FullMsgId replyToId,
		Window::SessionController *controller) {
	const auto trimmed = text.trimmed();
	const auto isMute = (trimmed.compare(u"!mute"_q, Qt::CaseInsensitive) == 0)
		|| trimmed.startsWith(u"!mute "_q, Qt::CaseInsensitive)
		|| trimmed.startsWith(u"!mute@"_q, Qt::CaseInsensitive);
	const auto isStats = (trimmed.compare(u"!stats"_q, Qt::CaseInsensitive) == 0)
		|| trimmed.startsWith(u"!stats "_q, Qt::CaseInsensitive)
		|| trimmed.startsWith(u"!stats@"_q, Qt::CaseInsensitive);
	const auto isStop = (trimmed.compare(u"!stop"_q, Qt::CaseInsensitive) == 0)
		|| trimmed.startsWith(u"!stop "_q, Qt::CaseInsensitive)
		|| trimmed.startsWith(u"!stop@"_q, Qt::CaseInsensitive);
	const auto isHelp = (trimmed.compare(u"!help"_q, Qt::CaseInsensitive) == 0)
		|| trimmed.startsWith(u"!help "_q, Qt::CaseInsensitive)
		|| trimmed.startsWith(u"!help@"_q, Qt::CaseInsensitive);
	if (!isMute && !isStats && !isStop && !isHelp) {
		return false;
	}
	if (!EnhancedSettings::Get(EnhancedSettings::Option::BotMode)) {
		if (controller) {
			controller->showToast(QString::fromUtf8(
				"Бот-режим выключен. Включите его в настройках ExtremeGram."));
		}
		return true;
	}
	if (isMute) {
		const auto spaceIdx = trimmed.indexOf(QChar(' '));
		const auto rest = (spaceIdx >= 0) ? trimmed.mid(spaceIdx + 1).trimmed() : QString();
		return handleMuteCommand(history, rest, replyToId, controller);
	}
	if (isStop) {
		if (stopScan(history)) {
			if (controller) {
				controller->showToast(QString::fromUtf8(
					"Подсчёт статистики остановлен. Команда !stats запустит заново."));
			}
		} else if (controller) {
			controller->showToast(QString::fromUtf8(
				"В этом чате нет активного подсчёта статистики."));
		}
		return true;
	}
	if (controller && isHelp) {
		controller->showToast(QString::fromUtf8(
			"Бот-режим ExtremeGram: команды\n"
			"!mute [время] [причина] — ограничить ответом (30с–366д: 90m, 2h, 1d)\n"
			"!stats — подсчёт сообщений чата\n"
			"!stop — остановить подсчёт\n"
			"!help — этот список"));
	}
	if (isHelp) {
		return true;
	}
	if (controller) {
		controller->showToast(QString::fromUtf8(
			"Запущен фоновый подсчёт статистики сообщений..."));
	}
	return handleStatsCommand(history);
}

std::optional<int> BotModeManager::ParseDurationSeconds(const QString &token) {
	static const QRegularExpression kRegex(u"^(\\d{1,8})([smhdwSMHDW]?)$"_q);
	const auto match = kRegex.match(token.trimmed());
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	const auto amount = match.captured(1).toLongLong();
	if (amount <= 0) {
		return std::nullopt;
	}
	const auto unit = match.captured(2).toLower();
	int64 multiplier = 60;
	if (unit == u"s"_q) {
		multiplier = 1;
	} else if (unit == u"m"_q) {
		multiplier = 60;
	} else if (unit == u"h"_q) {
		multiplier = 3600;
	} else if (unit == u"d"_q) {
		multiplier = 86400;
	} else if (unit == u"w"_q) {
		multiplier = 604800;
	}
	const auto total = std::clamp<int64>(
		amount * multiplier,
		30,
		366LL * 86400LL);
	return static_cast<int>(total);
}

int BotModeManager::ExtractWaitSeconds(const QString &errorType) {
	static const QRegularExpression kWaitRe(
		u"(?:FLOOD_WAIT_|SLOWMODE_WAIT_)(\\d+)"_q);
	const auto m = kWaitRe.match(errorType);
	if (m.hasMatch()) {
		return std::max(1, m.captured(1).toInt());
	}
	return 0;
}

bool BotModeManager::handleMuteCommand(
		not_null<History*> history,
		const QString &argsText,
		FullMsgId replyToId,
		Window::SessionController *controller) {
	const auto channel = history->peer->asChannel();
	if (!channel || !channel->isMegagroup()) {
		if (controller) {
			controller->showToast(QString::fromUtf8(
				"Команда !mute доступна только в супергруппах."));
		}
		return true;
	}

	PeerData *targetPeer = nullptr;
	if (replyToId) {
		if (const auto replyItem = _session->data().message(replyToId)) {
			targetPeer = replyItem->from().get();
		}
	}

	const auto parts = argsText.split(
		QRegularExpression(u"\\s+"_q),
		Qt::SkipEmptyParts);
	if (parts.isEmpty() && !targetPeer) {
		if (controller) {
			controller->showToast(QString::fromUtf8(
				"Ответьте на сообщение участника: !mute [время] [причина]"));
		}
		return true;
	}

	int argIndex = 0;
	if (!targetPeer && !parts.isEmpty() && parts[0].startsWith('@')) {
		const auto uname = parts[0].mid(1);
		targetPeer = _session->data().peerByUsername(uname);
		argIndex = 1;
	}

	if (!targetPeer) {
		if (controller) {
			controller->showToast(QString::fromUtf8(
				"Ответьте на сообщение участника: !mute [время] [причина]"));
		}
		return true;
	}

	auto durationSec = std::optional<int>();
	if (argIndex < parts.size()) {
		durationSec = ParseDurationSeconds(parts[argIndex]);
	}
	if (durationSec) {
		++argIndex;
	} else {
		durationSec = 4 * 86400;
	}

	const auto reason = (parts.size() > argIndex)
		? parts.mid(argIndex).join(' ')
		: QString();

	const auto untilDate = base::unixtime::now() + *durationSec;
	using Flag = MTPDchatBannedRights::Flag;
	const auto flags = Flag::f_send_messages
		| Flag::f_send_media
		| Flag::f_send_stickers
		| Flag::f_send_gifs
		| Flag::f_send_games
		| Flag::f_send_inline
		| Flag::f_embed_links
		| Flag::f_send_polls
		| Flag::f_send_photos
		| Flag::f_send_videos
		| Flag::f_send_roundvideos
		| Flag::f_send_audios
		| Flag::f_send_voices
		| Flag::f_send_docs
		| Flag::f_send_plain;

	_api.request(MTPchannels_EditBanned(
		channel->inputChannel(),
		targetPeer->input(),
		MTP_chatBannedRights(MTP_flags(flags), MTP_int(untilDate))
	)).done([=, targetName = targetPeer->name(), peer = history->peer](
			const MTPUpdates &result) {
		_session->updates().applyUpdates(result);
		auto report = QString::fromUtf8("Пользователь %1 заглушен на %2.")
			.arg(targetName, FormatDurationHuman(*durationSec));
		if (!reason.isEmpty()) {
			report += QString::fromUtf8("\nПричина: %1").arg(reason);
		}
		sendWithRetry(peer, report, replyToId);
	}).fail([=, peer = history->peer](const MTP::Error &error) {
		const auto waitSec = ExtractWaitSeconds(error.type());
		if (waitSec > 0) {
			scheduleRetry(peer->id, waitSec, [=] {
				handleMuteCommand(history, argsText, replyToId, nullptr);
			});
			return;
		}
		if (controller) {
			controller->showToast(
				QString::fromUtf8("Ошибка мута: %1").arg(error.type()));
		}
	}).send();

	return true;
}

bool BotModeManager::handleStatsCommand(not_null<History*> history) {
	const auto peer = history->peer;
	auto &jobPtr = _jobs[peer->id];
	if (jobPtr && jobPtr->active) {
		return true;
	}

	jobPtr = std::make_unique<ScanJob>(ScanJob{
		.peer = peer,
		.offsetId = MsgId(0),
		.scannedMessages = 0,
		.estimatedTotal = 0,
		.active = true,
		.liveTracking = false,
	});

	reportProgress(
		*jobPtr,
		QString::fromUtf8("Запуск фонового подсчёта сообщений..."));
	requestBatch(peer->id);
	return true;
}

bool BotModeManager::stopScan(not_null<History*> history) {
	const auto peerId = history->peer->id;
	const auto it = _jobs.find(peerId);
	if (it == _jobs.end() || !it->second) {
		return false;
	}
	auto &job = *it->second;
	if (job.currentRequestId) {
		_api.request(job.currentRequestId).cancel();
		job.currentRequestId = 0;
	}
	if (const auto timerIt = _jobTimers.find(peerId);
			timerIt != _jobTimers.end() && timerIt->second) {
		timerIt->second->cancel();
	}
	_retryCallbacks.erase(peerId);
	job.active = false;
	job.liveTracking = false;
	job.sendingLeaderboard = false;
	job.waitingRateLimit = false;
	job.waitSecondsLeft = 0;
	job.pendingOutgoingChunks.clear();
	job.countedMsgIds.clear();
	_dirtyLivePeers.erase(peerId);
	saveStats(job);
	return true;
}

void BotModeManager::requestBatch(PeerId peerId) {
	const auto it = _jobs.find(peerId);
	if (it == _jobs.end() || !it->second || !it->second->active) {
		return;
	}
	auto &job = *it->second;
	job.waitingRateLimit = false;
	job.waitSecondsLeft = 0;

	job.currentRequestId = _api.request(MTPmessages_GetHistory(
		job.peer->input(),
		MTP_int(job.offsetId.bare),
		MTP_int(0),
		MTP_int(0),
		MTP_int(kBatchSize),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto jobIt = _jobs.find(peerId);
		if (jobIt == _jobs.end() || !jobIt->second) {
			return;
		}
		auto &st = *jobIt->second;

		const QVector<MTPMessage> *messagesList = nullptr;
		int totalCount = st.estimatedTotal;

		result.match([&](const MTPDmessages_messages &d) {
			_session->data().processUsers(d.vusers());
			_session->data().processChats(d.vchats());
			messagesList = &d.vmessages().v;
			totalCount = std::max(totalCount, int(messagesList->size()));
		}, [&](const MTPDmessages_messagesSlice &d) {
			_session->data().processUsers(d.vusers());
			_session->data().processChats(d.vchats());
			messagesList = &d.vmessages().v;
			totalCount = d.vcount().v;
		}, [&](const MTPDmessages_channelMessages &d) {
			_session->data().processUsers(d.vusers());
			_session->data().processChats(d.vchats());
			messagesList = &d.vmessages().v;
			totalCount = d.vcount().v;
		}, [&](const MTPDmessages_messagesNotModified &) {});

		st.estimatedTotal = std::max(st.estimatedTotal, totalCount);

		if (!messagesList || messagesList->isEmpty()) {
			finishScan(peerId);
			return;
		}

		MsgId minId = st.offsetId;
		for (const auto &mtpMsg : *messagesList) {
			mtpMsg.match([&](const MTPDmessage &m) {
				const auto msgId = MsgId(m.vid().v);
				if (!minId || msgId < minId) {
					minId = msgId;
				}
				if (!st.countedMsgIds.emplace(msgId).second) {
					return;
				}
				++st.scannedMessages;

				PeerId authorPeerId = 0;
				if (const auto fromId = m.vfrom_id()) {
					authorPeerId = peerFromMTP(*fromId);
				} else {
					authorPeerId = peerFromMTP(m.vpeer_id());
				}
				if (!authorPeerId) {
					return;
				}
				const auto authorPeer = _session->data().peerLoaded(
					authorPeerId);
				auto &entry = st.counts[authorPeerId];
				entry.peerId = authorPeerId;
				if (authorPeer) {
					entry.name = authorPeer->name();
					entry.username = authorPeer->username();
				} else if (entry.name.isEmpty()) {
					entry.name = u"ID "_q + QString::number(authorPeerId.value);
				}
				++entry.count;
			}, [&](const MTPDmessageService &m) {
				const auto msgId = MsgId(m.vid().v);
				if (!minId || msgId < minId) {
					minId = msgId;
				}
			}, [&](const MTPDmessageEmpty &m) {
				const auto msgId = MsgId(m.vid().v);
				if (!minId || msgId < minId) {
					minId = msgId;
				}
			});
		}

		st.offsetId = minId;
		saveStats(st);
		reportProgress(
			st,
			QString::fromUtf8("Подсчёт: %1 / %2 (участников: %3)")
				.arg(st.scannedMessages)
				.arg(std::max(st.scannedMessages, st.estimatedTotal))
				.arg(st.counts.size()));

		if (messagesList->size() < kBatchSize || minId.bare <= 1) {
			finishScan(peerId);
			return;
		}

		auto &timer = _jobTimers[peerId];
		if (!timer) {
			timer = std::make_unique<base::Timer>([=] {
				requestBatch(peerId);
			});
		}
		timer->callOnce(kBatchDelayMs);
	}).fail([=](const MTP::Error &error) {
		const auto waitSec = ExtractWaitSeconds(error.type());
		scheduleRetry(peerId, (waitSec > 0) ? waitSec : 3, [=] {
			requestBatch(peerId);
		});
	}).send();
}

void BotModeManager::scheduleRetry(
		PeerId peerId,
		int seconds,
		Fn<void()> retryFn) {
	const auto it = _jobs.find(peerId);
	if (it != _jobs.end() && it->second) {
		it->second->waitingRateLimit = true;
		it->second->waitSecondsLeft = seconds + 1;
		reportProgress(
			*it->second,
			QString::fromUtf8("Ожидание лимита API: %1 с...").arg(seconds + 1));
	}

	if (!_countdownTicker.isActive()) {
		_countdownTicker.callEach(1000);
	}

	_retryCallbacks[peerId] = std::move(retryFn);
	auto &timer = _jobTimers[peerId];
	timer = std::make_unique<base::Timer>([=] {
		if (const auto cbIt = _retryCallbacks.find(peerId);
				cbIt != _retryCallbacks.end()) {
			auto fn = std::move(cbIt->second);
			_retryCallbacks.erase(cbIt);
			if (fn) {
				fn();
			}
		}
	});
	timer->callOnce((seconds + 1) * crl::time(1000));
}

void BotModeManager::finishScan(PeerId peerId) {
	const auto it = _jobs.find(peerId);
	if (it == _jobs.end() || !it->second) {
		return;
	}
	auto &job = *it->second;
	job.liveTracking = true;
	job.sendingLeaderboard = true;
	saveStats(job);

	std::vector<UserStatEntry> sorted;
	sorted.reserve(job.counts.size());
	for (const auto &[_, entry] : job.counts) {
		if (entry.count > 0) {
			sorted.push_back(entry);
		}
	}
	std::sort(
		sorted.begin(),
		sorted.end(),
		[](const UserStatEntry &a, const UserStatEntry &b) {
			return (a.count != b.count) ? (a.count > b.count) : (a.name < b.name);
		});

	job.pendingOutgoingChunks.clear();
	auto currentChunk = QString::fromUtf8(
		"Лидерборд чата по сообщениям (всего: %1, участников: %2):\n\n")
		.arg(job.scannedMessages)
		.arg(sorted.size());

	int rank = 1;
	for (const auto &u : sorted) {
		const auto handle = u.username.isEmpty()
			? u.name
			: QString::fromUtf8("%1 (@%2)").arg(u.name, u.username);
		const auto line = QString::fromUtf8("%1. %2 — %3\n")
			.arg(rank++)
			.arg(handle)
			.arg(u.count);

		if (currentChunk.size() + line.size() > kMaxMessageChars) {
			job.pendingOutgoingChunks.push_back(currentChunk);
			currentChunk = QString::fromUtf8("Лидерборд (продолжение):\n\n")
				+ line;
		} else {
			currentChunk += line;
		}
	}
	if (!currentChunk.trimmed().isEmpty()) {
		job.pendingOutgoingChunks.push_back(currentChunk);
	}

	reportProgress(job, QString::fromUtf8("Отправка лидерборда в чат..."));
	sendChunk(peerId);
}

void BotModeManager::sendChunk(PeerId peerId) {
	const auto it = _jobs.find(peerId);
	if (it == _jobs.end() || !it->second) {
		return;
	}
	auto &job = *it->second;
	if (job.pendingOutgoingChunks.empty()) {
		job.active = false;
		job.sendingLeaderboard = false;
		job.waitingRateLimit = false;
		saveStats(job);
		reportProgress(
			job,
			QString::fromUtf8("Подсчёт завершён. Фоновый учёт активен."));
		return;
	}

	const auto nextText = job.pendingOutgoingChunks.front();
	sendWithRetry(job.peer, nextText, FullMsgId(), [=] {
		const auto innerIt = _jobs.find(peerId);
		if (innerIt == _jobs.end() || !innerIt->second) {
			return;
		}
		auto &st = *innerIt->second;
		if (!st.pendingOutgoingChunks.empty()) {
			st.pendingOutgoingChunks.erase(st.pendingOutgoingChunks.begin());
		}
		if (st.pendingOutgoingChunks.empty()) {
			sendChunk(peerId);
		} else {
			scheduleRetry(peerId, 1, [=] {
				sendChunk(peerId);
			});
		}
	});
}

void BotModeManager::sendWithRetry(
		not_null<PeerData*> peer,
		const QString &text,
		FullMsgId replyTo,
		Fn<void()> onDone) {
	using SendFlag = MTPmessages_SendMessage::Flag;
	auto flags = MTPmessages_SendMessage::Flags(0);
	auto replyHeader = MTPInputReplyTo();
	if (replyTo.msg) {
		flags |= SendFlag::f_reply_to;
		replyHeader = MTP_inputReplyToMessage(
			MTP_flags(0),
			MTP_int(replyTo.msg.bare),
			MTPint(),
			MTPInputPeer(),
			MTPstring(),
			MTPVector<MTPMessageEntity>(),
			MTPint(),
			MTPInputPeer(),
			MTPint(),
			MTPbytes(),
			MTPVector<MTPRichText>());
	}

	const auto randomId = base::RandomValue<uint64>();
	_api.request(MTPmessages_SendMessage(
		MTP_flags(flags),
		peer->input(),
		replyHeader,
		MTP_string(text),
		MTP_long(randomId),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTPint(),
		MTPint(),
		MTPInputPeer(),
		MTPInputQuickReplyShortcut(),
		MTPlong(),
		MTPlong(),
		MTPSuggestedPost(),
		MTPInputRichMessage()
	)).done([=](const MTPUpdates &updates) {
		_session->updates().applyUpdates(updates);
		if (onDone) {
			onDone();
		}
	}).fail([=](const MTP::Error &error) {
		const auto waitSec = ExtractWaitSeconds(error.type());
		if (waitSec > 0) {
			scheduleRetry(peer->id, waitSec, [=] {
				sendWithRetry(peer, text, replyTo, onDone);
			});
		} else if (onDone) {
			onDone();
		}
	}).send();
}

void BotModeManager::loadStats(not_null<PeerData*> peer) {
	if (_jobs.contains(peer->id)) {
		return;
	}
	const auto path = statsDirPath() + u"/stats_"_q
		+ QString::number(peer->id.value) + u".json"_q;
	QFile f(path);
	if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		return;
	}
	const auto root = doc.object();
	auto job = std::make_unique<ScanJob>(ScanJob{
		.peer = peer,
		.scannedMessages = root[u"scanned_messages"_q].toInt(),
		.active = false,
		.liveTracking = root[u"live_tracking"_q].toBool(true),
	});
	const auto arr = root[u"leaderboard"_q].toArray();
	for (const auto &val : arr) {
		const auto obj = val.toObject();
		const auto uid = PeerId(obj[u"peer_id"_q].toString().toULongLong());
		if (!uid) {
			continue;
		}
		job->counts[uid] = UserStatEntry{
			.peerId = uid,
			.name = obj[u"name"_q].toString(),
			.username = obj[u"username"_q].toString(),
			.count = obj[u"count"_q].toInteger(),
		};
	}
	_jobs[peer->id] = std::move(job);
}

void BotModeManager::onItemAdded(not_null<HistoryItem*> item) {
	if (!item->isRegular() || !IsServerMsgId(item->id)) {
		return;
	}
	const auto peer = item->history()->peer;
	loadStats(peer);
	const auto peerId = peer->id;
	const auto it = _jobs.find(peerId);
	if (it == _jobs.end() || !it->second || !it->second->liveTracking) {
		return;
	}
	auto &job = *it->second;
	if (!job.countedMsgIds.emplace(item->id).second) {
		return;
	}
	const auto sender = item->displayFrom()
		? not_null<PeerData*>(item->displayFrom())
		: item->from();
	auto &entry = job.counts[sender->id];
	entry.peerId = sender->id;
	entry.name = sender->name();
	entry.username = sender->username();
	++entry.count;
	++job.scannedMessages;

	_dirtyLivePeers.emplace(peerId);
	if (!_periodicFlushTimer.isActive()) {
		_periodicFlushTimer.callOnce(3000);
	}
}

QString BotModeManager::statsDirPath() const {
	const auto dirPath = cWorkingDir() + u"tdata/custom_bot_stats"_q;
	QDir().mkpath(dirPath);
	return dirPath;
}

void BotModeManager::saveStats(const ScanJob &job) const {
	QJsonObject root;
	root[u"peer_id"_q] = QString::number(job.peer->id.value);
	root[u"peer_name"_q] = job.peer->name();
	root[u"scanned_messages"_q] = job.scannedMessages;
	root[u"live_tracking"_q] = job.liveTracking;
	root[u"updated_at"_q] = static_cast<qint64>(base::unixtime::now());

	QJsonArray usersArr;
	for (const auto &[_, entry] : job.counts) {
		QJsonObject u;
		u[u"peer_id"_q] = QString::number(entry.peerId.value);
		u[u"name"_q] = entry.name;
		u[u"username"_q] = entry.username;
		u[u"count"_q] = static_cast<qint64>(entry.count);
		usersArr.append(u);
	}
	root[u"leaderboard"_q] = usersArr;

	const auto path = statsDirPath() + u"/stats_"_q
		+ QString::number(job.peer->id.value) + u".json"_q;
	QFile f(path);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	}
}

void BotModeManager::reportProgress(
		const ScanJob &job,
		const QString &status) {
	_progressStream.fire(StatsProgressSnapshot{
		.peerId = job.peer->id,
		.active = job.active,
		.waitingRateLimit = job.waitingRateLimit,
		.sendingLeaderboard = job.sendingLeaderboard,
		.waitSecondsLeft = job.waitSecondsLeft,
		.scannedMessages = job.scannedMessages,
		.estimatedTotal = job.estimatedTotal,
		.uniqueUsers = static_cast<int>(job.counts.size()),
		.statusText = status,
	});
}

rpl::producer<StatsProgressSnapshot> BotModeManager::progressUpdates(
		PeerId peerId) const {
	return _progressStream.events() | rpl::filter([=](
			const StatsProgressSnapshot &s) {
		return s.peerId == peerId;
	});
}

StatsProgressSnapshot BotModeManager::currentProgress(PeerId peerId) const {
	const auto it = _jobs.find(peerId);
	if (it == _jobs.end() || !it->second) {
		return StatsProgressSnapshot{ .peerId = peerId, .active = false };
	}
	const auto &job = *it->second;
	return StatsProgressSnapshot{
		.peerId = peerId,
		.active = job.active,
		.waitingRateLimit = job.waitingRateLimit,
		.sendingLeaderboard = job.sendingLeaderboard,
		.waitSecondsLeft = job.waitSecondsLeft,
		.scannedMessages = job.scannedMessages,
		.estimatedTotal = job.estimatedTotal,
		.uniqueUsers = static_cast<int>(job.counts.size()),
		.statusText = job.waitingRateLimit
			? QString::fromUtf8("Ожидание лимита API...")
			: QString::fromUtf8("Подсчёт сообщений..."),
	};
}

BotModeManager &BotModeFor(not_null<Main::Session*> session) {
	static base::flat_map<not_null<Main::Session*>, std::unique_ptr<BotModeManager>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<BotModeManager>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

} // namespace Custom
