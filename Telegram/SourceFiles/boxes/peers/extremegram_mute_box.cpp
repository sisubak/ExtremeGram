/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/extremegram_mute_box.h"

#include "api/api_chat_participants.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/button_busy.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>

#include <algorithm>

namespace {

constexpr auto kSecondsInMinute = 60;
constexpr auto kSecondsInHour = 60 * kSecondsInMinute;
constexpr auto kSecondsInDay = 24 * kSecondsInHour;
constexpr auto kMinRestrictSeconds = 30;
constexpr auto kMaxRestrictSeconds = 366 * kSecondsInDay;

[[nodiscard]] bool ParseDurationSeconds(const QString &text, int &seconds) {
	const auto value = text.trimmed().toLower();
	if (value.isEmpty()) {
		seconds = 0;
		return true;
	}
	static const auto expression = QRegularExpression(
		u"^(\\d+)\\s*(с|сек|секунд|s|sec|secs|"
		u"м|мин|минут|минуты|m|min|mins|"
		u"ч|час|часа|часов|h|hr|hrs|hour|hours|"
		u"д|дн|дня|дней|день|d|day|days|"
		u"н|нед|недели|недель|w|wk|week|weeks|"
		u"г|год|года|лет|y|yr|year|years)?$"_q);
	const auto match = expression.match(value);
	if (!match.hasMatch()) {
		return false;
	}
	bool ok = false;
	const auto number = match.captured(1).toLongLong(&ok);
	if (!ok || number <= 0) {
		return false;
	}
	const auto unit = match.captured(2);
	auto multiplier = qint64(kSecondsInDay);
	if (unit.startsWith(u"с"_q) || unit.startsWith(u"s"_q)) {
		multiplier = 1;
	} else if (unit.startsWith(u"м"_q) || unit.startsWith(u"m"_q)) {
		multiplier = kSecondsInMinute;
	} else if (unit.startsWith(u"ч"_q) || unit.startsWith(u"h"_q)) {
		multiplier = kSecondsInHour;
	} else if (unit.startsWith(u"н"_q) || unit.startsWith(u"w"_q)) {
		multiplier = 7 * qint64(kSecondsInDay);
	} else if (unit.startsWith(u"г"_q)
		|| unit.startsWith(u"лет"_q)
		|| unit.startsWith(u"y"_q)) {
		multiplier = 365 * qint64(kSecondsInDay);
	}
	const auto rawSeconds = (number > kMaxRestrictSeconds / multiplier)
		? qint64(kMaxRestrictSeconds)
		: (number * multiplier);
	seconds = int(std::clamp<qint64>(
		rawSeconds,
		kMinRestrictSeconds,
		kMaxRestrictSeconds));
	return true;
}

[[nodiscard]] ChatRestrictions MuteRestrictions() {
	auto result = ChatRestrictions(0);
	const auto flags = {
		ChatRestriction::SendStickers,
		ChatRestriction::SendGifs,
		ChatRestriction::SendGames,
		ChatRestriction::SendInline,
		ChatRestriction::SendPolls,
		ChatRestriction::SendPhotos,
		ChatRestriction::SendVideos,
		ChatRestriction::SendVideoMessages,
		ChatRestriction::SendMusic,
		ChatRestriction::SendVoiceMessages,
		ChatRestriction::SendFiles,
		ChatRestriction::SendOther,
		ChatRestriction::EmbedLinks,
		ChatRestriction::ChangeInfo,
		ChatRestriction::AddParticipants,
		ChatRestriction::PinMessages,
		ChatRestriction::CreateTopics,
		ChatRestriction::SendReactions,
		ChatRestriction::ManageLinkedPeers,
	};
	for (const auto flag : flags) {
		result |= flag;
	}
	return result;
}

void SendMuteMessage(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const QString &reason,
		const QString &term) {
	const auto session = &channel->session();
	const auto name = user->name();
	const auto text = name
		+ u" Ты был замучен.\nПричина: "_q
		+ (reason.isEmpty() ? QString::fromUtf8("не указана") : reason)
		+ u"\nСрок: "_q
		+ term;
	const auto mention = u"mention://"_q
		+ TextUtilities::MentionNameDataFromFields({
			.selfId = session->userId().bare,
			.userId = peerToUser(user->id).bare,
			.accessHash = user->accessHash(),
		});
	auto message = Api::MessageToSend(Api::SendAction(
		channel->owner().history(channel)));
	message.textWithTags = TextWithTags{
		text,
		{ TextWithTags::Tag{ 0, int(name.size()), mention } },
	};
	session->api().sendMessage(std::move(message));
}

} // namespace

void ShowExtremeGramMuteBox(
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		ChatRestrictionsInfo oldRights) {
	ShowExtremeGramMuteBox(
		controller->uiShow(),
		channel,
		user,
		oldRights);
}

void ShowExtremeGramMuteBox(
		std::shared_ptr<Ui::Show> show,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		ChatRestrictionsInfo oldRights) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_mute_title());
		box->setWidth(st::boxWideWidth);

		const auto duration = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_extremegram_mute_duration()));
		duration->setMaxLength(32);
		const auto hint = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_extremegram_mute_hint(),
				st::defaultFlatLabel),
			st::boxRowPadding + QMargins(0, 0, 0, 8));
		hint->setTextColorOverride(st::windowSubTextFg->c);
		const auto reason = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_extremegram_mute_reason()));
		reason->setMaxLength(256);
		const auto notify = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_extremegram_mute_notify(),
				true,
				st::defaultBoxCheckbox),
			st::boxRowPadding + QMargins(0, 4, 0, 0));

		box->setFocusCallback([=] {
			duration->setFocusFast();
		});

		const auto pending = box->lifetime().make_state<bool>(false);
		const auto save = box->lifetime().make_state<
			QPointer<Ui::RoundButton>>();
		const auto setPending = [=](bool value) {
			*pending = value;
			duration->setEnabled(!value);
			reason->setEnabled(!value);
			notify->setEnabled(!value);
			Ui::SetButtonBusy(save->data(), value);
		};

		const auto submit = [=] {
			if (*pending) {
				return;
			}
			auto seconds = 0;
			if (!ParseDurationSeconds(duration->getLastText(), seconds)) {
				duration->showError();
				return;
			}
			const auto term = seconds > 0
				? duration->getLastText().trimmed()
				: QString::fromUtf8("навсегда");
			const auto reasonText = reason->getLastText().trimmed();
			const auto sendMessage = notify->checked();
			const auto until = seconds > 0
				? base::unixtime::now() + seconds
				: TimeId(0);
			const auto newRights = ChatRestrictionsInfo{
				oldRights.flags | MuteRestrictions(),
				until,
			};
			setPending(true);
			Api::ChatParticipants::Restrict(
				channel,
				user,
				oldRights,
				newRights,
				crl::guard(box, [=] {
					if (sendMessage) {
						SendMuteMessage(channel, user, reasonText, term);
					}
					show->showToast(
						tr::lng_extremegram_mute_done(tr::now));
					box->closeBox();
				}),
				crl::guard(box, [=](const QString &error) {
					setPending(false);
					show->showToast(
						tr::lng_extremegram_mute_failed(tr::now)
						+ u": "_q
						+ error);
				}));
		};

		duration->submits(
		) | rpl::on_next([=] {
			reason->setFocus();
		}, duration->lifetime());

		reason->submits(
		) | rpl::on_next([=] {
			submit();
		}, reason->lifetime());

		*save = box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}
