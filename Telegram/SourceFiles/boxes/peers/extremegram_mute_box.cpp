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
#include "ui/layers/generic_box.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"

#include <QtCore/QRegularExpression>

#include <algorithm>

namespace {

constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kMaxRestrictDays = 366;

[[nodiscard]] bool ParseDuration(const QString &text, int &days) {
	const auto value = text.trimmed().toLower();
	if (value.isEmpty()) {
		days = 0;
		return true;
	}
	static const auto expression = QRegularExpression(
		u"^(\\d+)\\s*(д|дн|дня|дней|день|d|day|days|г|год|года|лет|y|year|years)?$"_q);
	const auto match = expression.match(value);
	if (!match.hasMatch()) {
		return false;
	}
	auto number = match.captured(1).toInt();
	const auto unit = match.captured(2);
	if (unit.startsWith(u"г"_q)
		|| unit.startsWith(u"лет"_q)
		|| unit.startsWith(u"y"_q)) {
		number *= 365;
	}
	days = std::clamp(number, 0, kMaxRestrictDays);
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
			.userId = user->id.bare,
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
	const auto show = controller->uiShow();
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_mute_title());
		box->setWidth(st::boxWideWidth);

		const auto duration = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_extremegram_mute_duration()));
		duration->setMaxLength(32);
		const auto reason = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_extremegram_mute_reason()));
		reason->setMaxLength(256);

		box->setFocusCallback([=] {
			duration->setFocusFast();
		});

		box->addButton(tr::lng_settings_save(), [=] {
			auto days = 0;
			if (!ParseDuration(duration->getLastText(), days)) {
				duration->showError();
				return;
			}
			const auto term = days > 0
				? duration->getLastText().trimmed()
				: QString::fromUtf8("навсегда");
			const auto reasonText = reason->getLastText().trimmed();
			const auto until = days > 0
				? base::unixtime::now() + days * kSecondsInDay
				: TimeId(0);
			const auto newRights = ChatRestrictionsInfo{
				MuteRestrictions(),
				until,
			};
			box->closeBox();
			Api::ChatParticipants::Restrict(
				channel,
				user,
				oldRights,
				newRights,
				[=] {
					SendMuteMessage(channel, user, reasonText, term);
					show->showToast(tr::lng_extremegram_mute_done(tr::now));
				},
				[=](const QString &error) {
					show->showToast(
						tr::lng_extremegram_mute_failed(tr::now)
						+ u": "_q
						+ error);
				});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}
