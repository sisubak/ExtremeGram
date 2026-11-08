/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/field_autocomplete.h"

#include "custom/client_badge_detector.h"
#include "custom/mention_pins.h"
#include "dialogs/dialogs_three_state_icon.h"
#include "dialogs/ui/dialogs_layout.h"

#include "api/api_chat_participants.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "boxes/sticker_set_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/message_field.h" // PrepareMentionTag.
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/tabbed_selector.h" // ChatHelpers::FileChosen.
#include "core/application.h"
#include "core/core_settings.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/recent_inline_bots.h"
#include "data/components/top_peers.h"
#include "data/stickers/data_stickers.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "media/clip/media_clip_reader.h"
#include "menu/menu_send.h" // SendMenu::FillSendMenu
#include "storage/storage_account.h"
#include "ui/effects/animations.h"
#include "ui/effects/message_sending_animation_common.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/round_rect.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

#include <algorithm>

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

constexpr auto kEphemeralHintHoverDelay = crl::time(500);

[[nodiscard]] QString PrimaryUsername(not_null<UserData*> user) {
	const auto &usernames = user->usernames();
	return usernames.empty() ? user->username() : usernames.front();
}

[[nodiscard]] Data::BotStatus BotStatusFor(
		ChatData *chat,
		ChannelData *channel) {
	return chat
		? chat->botStatus
		: (channel && channel->isMegagroup())
		? channel->mgInfo->botStatus
		: Data::BotStatus::NoBots;
}

template <typename T, typename U>
[[nodiscard]] int IndexOfInFirstN(const T &list, const U &user, int last) {
	const auto count = std::min(int(list.size()), last);
	for (auto i = 0; i != count; ++i) {
		if (list[i].user == user) {
			return i;
		}
	}
	return -1;
}

} // namespace

class FieldAutocomplete::Inner final : public Ui::RpWidget {
public:
	struct ScrollTo {
		int top;
		int bottom;
	};

	Inner(
		std::shared_ptr<Show> show,
		const style::EmojiPan &st,
		not_null<FieldAutocomplete*> parent,
		not_null<MentionRows*> mrows,
		not_null<HashtagRows*> hrows,
		not_null<BotCommandRows*> brows,
		not_null<StickerRows*> srows);

	void clearSel(bool hidden = false);
	bool moveSel(int key);
	bool chooseSelected(FieldAutocomplete::ChooseMethod method) const;
	bool chooseAtIndex(
		FieldAutocomplete::ChooseMethod method,
		int index,
		Api::SendOptions options = {}) const;
	void startPressDragSelect();

	void setSendMenuDetails(Fn<SendMenu::Details()> &&callback);
	void rowsUpdated();

	[[nodiscard]] auto mentionChosen() const
		-> rpl::producer<FieldAutocomplete::MentionChosen>;
	[[nodiscard]] auto hashtagChosen() const
		-> rpl::producer<FieldAutocomplete::HashtagChosen>;
	[[nodiscard]] auto botCommandChosen() const
		-> rpl::producer<FieldAutocomplete::BotCommandChosen>;
	[[nodiscard]] auto stickerChosen() const
		-> rpl::producer<FieldAutocomplete::StickerChosen>;
	[[nodiscard]] rpl::producer<ScrollTo> scrollToRequested() const;
	[[nodiscard]] rpl::producer<QRect> ephemeralIconHovered() const;

	void onParentGeometryChanged();

private:
	void paintEvent(QPaintEvent *e) override;
	void paintStickers(Painter &p, QRect clip);
	void paintRows(Painter &p, QRect clip);
	void paintMentionRowAt(
		Painter &p,
		int index,
		int rowTop,
		bool selected,
		bool dragged,
		bool isPinned);
	void paintShadow(QPainter &p, int top);
	void resizeEvent(QResizeEvent *e) override;

	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	[[nodiscard]] QRect selectedRect(int index) const;
	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] int pinnedMentionRowsCount() const;
	[[nodiscard]] int visualSlotForPinnedRow(int index) const;
	[[nodiscard]] bool commandsWithUsername() const;
	[[nodiscard]] bool isRemovableMentionRow(int index) const;
	void updateSelectedRow();
	void setSel(int sel, bool scroll = false);
	void showPreview();
	void selectByMouse(QPoint global);
	[[nodiscard]] QRect ephemeralIconRect(int index) const;
	void updateEphemeralIconHover(QPoint position);

	[[nodiscard]] QSize stickerBoundingBox() const;
	void setupLottie(StickerSuggestion &suggestion);
	void setupWebm(StickerSuggestion &suggestion);
	void repaintSticker(not_null<DocumentData*> document);
	void repaintStickerAtIndex(int index);
	[[nodiscard]] std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();
	void clipCallback(
		Media::Clip::Notification notification,
		not_null<DocumentData*> document);

	const std::shared_ptr<Show> _show;
	const not_null<Main::Session*> _session;
	const style::EmojiPan &_st;
	const not_null<FieldAutocomplete*> _parent;
	const not_null<MentionRows*> _mrows;
	const not_null<HashtagRows*> _hrows;
	const not_null<BotCommandRows*> _brows;
	const not_null<StickerRows*> _srows;
	Ui::RoundRect _overBg;
	rpl::lifetime _stickersLifetime;
	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;
	base::unique_qptr<Ui::PopupMenu> _menu;
	int _stickersPerRow = 1;
	int _sel = -1;
	int _down = -1;
	int _ephemeralIconHover = -1;
	std::optional<QPoint> _lastMousePosition;
	bool _mouseSelection = false;
	bool _overDelete = false;
	bool _previewShown = false;
	bool _dragSelect = false;
	bool _adjustShadowLeft = false;

	bool _pinDragCandidate = false;
	bool _pinDragActive = false;
	int _pinDragOriginIndex = -1;
	int _pinDragTargetIndex = -1;
	int _pinDragGrabOffsetY = 0;
	int _pinDragCurrentTop = 0;
	QPoint _pinDragStartPos;
	base::flat_map<UserId, Ui::Animations::Simple> _pinIconAnims;
	base::flat_set<UserId> _pinClosingUsers;
	base::flat_map<UserId, Ui::Animations::Simple> _rowShiftAnims;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	StickerPremiumMark _premiumMark;

	Fn<SendMenu::Details()> _sendMenuDetails;

	rpl::event_stream<FieldAutocomplete::MentionChosen> _mentionChosen;
	rpl::event_stream<FieldAutocomplete::HashtagChosen> _hashtagChosen;
	rpl::event_stream<FieldAutocomplete::BotCommandChosen> _botCommandChosen;
	rpl::event_stream<FieldAutocomplete::StickerChosen> _stickerChosen;
	rpl::event_stream<ScrollTo> _scrollToRequested;
	rpl::event_stream<QRect> _ephemeralIconHovered;

	base::Timer _previewTimer;

};

struct FieldAutocomplete::StickerSuggestion {
	not_null<DocumentData*> document;
	std::shared_ptr<Data::DocumentMedia> documentMedia;
	std::unique_ptr<Lottie::SinglePlayer> lottie;
	Media::Clip::ReaderPointer webm;
	QImage premiumLock;
};

struct FieldAutocomplete::MentionRow {
	enum class Source {
		InlineRecent,
		GuestChatTopPeer,
		MentionCandidate,
	};

	not_null<UserData*> user;
	Source source = Source::MentionCandidate;
	Ui::Text::String name;
	Ui::PeerUserpicView userpic;

	[[nodiscard]] bool removable() const {
		return (source == Source::InlineRecent)
			|| (source == Source::GuestChatTopPeer);
	}
};

struct FieldAutocomplete::BotCommandRow {
	not_null<UserData*> user;
	QString command;
	QString description;
	Ui::PeerUserpicView userpic;
	Ui::Text::String descriptionText;
	bool ephemeral = false;
};

FieldAutocomplete::FieldAutocomplete(
	QWidget *parent,
	std::shared_ptr<Show> show,
	const style::EmojiPan *stOverride)
: RpWidget(parent)
, _show(std::move(show))
, _session(&_show->session())
, _st(stOverride ? *stOverride : st::defaultEmojiPan)
, _scroll(this)
, _ephemeralHintTimer([=] { showPendingEphemeralHint(); }) {
	hide();

	_scroll->setGeometry(rect());

	_inner = _scroll->setOwnedWidget(
		object_ptr<Inner>(
			_show,
			_st,
			this,
			&_mrows,
			&_hrows,
			&_brows,
			&_srows));
	_inner->setGeometry(rect());

	_inner->scrollToRequested(
	) | rpl::on_next([=](Inner::ScrollTo data) {
		_scroll->scrollToY(data.top, data.bottom);
	}, lifetime());

	_scroll->scrollTopValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		hideEphemeralHint();
	}, lifetime());

	_inner->ephemeralIconHovered(
	) | rpl::on_next([=](QRect iconRect) {
		ephemeralIconHovered(iconRect);
	}, lifetime());

	_scroll->show();
	_inner->show();

	hide();

	_scroll->geometryChanged(
	) | rpl::on_next(crl::guard(_inner, [=] {
		_inner->onParentGeometryChanged();
	}), lifetime());

	_session->topGuestChatBots().updates(
	) | rpl::on_next([=] {
		if (_hiding
			|| isHidden()
			|| (_type != Type::Mentions)) {
			return;
		}
		updateFiltered();
	}, lifetime());
}

std::shared_ptr<Show> FieldAutocomplete::uiShow() const {
	return _show;
}

void FieldAutocomplete::requestRefresh() {
	_refreshRequests.fire({});
}

rpl::producer<> FieldAutocomplete::refreshRequests() const {
	return _refreshRequests.events();
}

void FieldAutocomplete::requestStickersUpdate() {
	_stickersUpdateRequests.fire({});
}

rpl::producer<> FieldAutocomplete::stickersUpdateRequests() const {
	return _stickersUpdateRequests.events();
}

auto FieldAutocomplete::mentionChosen() const
-> rpl::producer<FieldAutocomplete::MentionChosen> {
	return _inner->mentionChosen();
}

auto FieldAutocomplete::hashtagChosen() const
-> rpl::producer<FieldAutocomplete::HashtagChosen> {
	return _inner->hashtagChosen();
}

auto FieldAutocomplete::botCommandChosen() const
-> rpl::producer<FieldAutocomplete::BotCommandChosen> {
	return _inner->botCommandChosen();
}

auto FieldAutocomplete::stickerChosen() const
-> rpl::producer<FieldAutocomplete::StickerChosen> {
	return _inner->stickerChosen();
}

auto FieldAutocomplete::choosingProcesses() const
-> rpl::producer<FieldAutocomplete::Type> {
	return _scroll->scrollTopChanges(
	) | rpl::filter([](int top) {
		return top != 0;
	}) | rpl::map([=] {
		return !_mrows.empty()
			? Type::Mentions
			: !_hrows.empty()
			? Type::Hashtags
			: !_brows.empty()
			? Type::BotCommands
			: !_srows.empty()
			? Type::Stickers
			: _type;
	});
}

FieldAutocomplete::~FieldAutocomplete() = default;

void FieldAutocomplete::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto opacity = _opacityAnimation.value(_hiding ? 0. : 1.);
	if (opacity < 1.) {
		if (opacity > 0.) {
			p.setOpacity(opacity);
			p.drawPixmap(0, 0, _cache);
		}
		return;
	}

	p.fillRect(rect(), _st.bg);
}

PeerData *FieldAutocomplete::peer() const {
	return _chat
		? static_cast<PeerData*>(_chat)
		: _user
		? static_cast<PeerData*>(_user)
		: static_cast<PeerData*>(_channel);
}

void FieldAutocomplete::showFiltered(
		not_null<PeerData*> peer,
		QString query,
		bool addInlineBots) {
	_chat = peer->asChat();
	_user = peer->asUser();
	_channel = peer->asChannel();
	if (query.isEmpty()) {
		_type = Type::Mentions;
		rowsUpdated(
			MentionRows(),
			HashtagRows(),
			BotCommandRows(),
			base::take(_srows),
			false);
		return;
	}

	_emoji = nullptr;

	query = query.toLower();
	auto type = Type::Stickers;
	auto plainQuery = QStringView(query);
	switch (query.at(0).unicode()) {
	case '@':
		type = Type::Mentions;
		plainQuery = base::StringViewMid(query, 1);
		break;
	case '#':
		type = Type::Hashtags;
		plainQuery = base::StringViewMid(query, 1);
		break;
	case '/':
		type = Type::BotCommands;
		plainQuery = base::StringViewMid(query, 1);
		break;
	}
	const auto resetScroll = (_type != type || _filter != plainQuery);
	if (resetScroll) {
		_type = type;
		_filter = TextUtilities::RemoveAccents(plainQuery.toString());
	}
	_addInlineBots = addInlineBots;

	updateFiltered(resetScroll);
}

void FieldAutocomplete::showStickers(EmojiPtr emoji) {
	const auto resetScroll = (_emoji != emoji);
	if (resetScroll || emoji) {
		_emoji = emoji;
		_type = Type::Stickers;
	} else if (!emoji) {
		rowsUpdated(
			base::take(_mrows),
			base::take(_hrows),
			base::take(_brows),
			StickerRows(),
			false);
		return;
	}

	_chat = nullptr;
	_user = nullptr;
	_channel = nullptr;

	updateFiltered(resetScroll);
}

EmojiPtr FieldAutocomplete::stickersEmoji() const {
	return _emoji;
}

bool FieldAutocomplete::clearFilteredBotCommands() {
	if (_brows.empty()) {
		return false;
	}
	_brows.clear();
	return true;
}

FieldAutocomplete::StickerRows FieldAutocomplete::getStickerSuggestions() {
	const auto data = &_session->data().stickers();
	const auto list = data->getListByEmoji({ _emoji }, _stickersSeed);
	auto result = ranges::views::all(
		list
	) | ranges::views::transform([](not_null<DocumentData*> sticker) {
		return StickerSuggestion{
			sticker,
			sticker->createMediaView()
		};
	}) | ranges::to_vector;
	for (auto &suggestion : _srows) {
		if (!suggestion.lottie && !suggestion.webm) {
			continue;
		}
		const auto i = ranges::find(
			result,
			suggestion.document,
			&StickerSuggestion::document);
		if (i != end(result)) {
			i->lottie = std::move(suggestion.lottie);
			i->webm = std::move(suggestion.webm);
		}
	}
	return result;
}

void FieldAutocomplete::updateFiltered(bool resetScroll) {
	const auto now = base::unixtime::now();
	auto mrows = MentionRows();
	auto hrows = HashtagRows();
	auto brows = BotCommandRows();
	auto srows = StickerRows();
	if (_emoji) {
		srows = getStickerSuggestions();
	} else if (_type == Type::Mentions) {
		using Source = MentionRow::Source;
		const auto guestChatBots = _session->topGuestChatBots().list();
		const auto chatMembers = !_chat
			? 0
			: _chat->participants.empty()
			? int(_chat->lastAuthors.size())
			: int(_chat->participants.size());
		auto maxListSize = int(guestChatBots.size())
			+ (_addInlineBots
				? int(_session->recentInlineBots().list().size())
				: 0);
		if (_chat) {
			maxListSize += chatMembers;
		} else if (_channel && _channel->isMegagroup()) {
			if (!_channel->canViewMembers()) {
				maxListSize += _channel->mgInfo->admins.size();
			} else if (!_channel->lastParticipantsRequestNeeded()) {
				maxListSize += _channel->mgInfo->lastParticipants.size();
			}
		}
		if (maxListSize) {
			mrows.reserve(maxListSize);
		}

		const auto filterNotPassedByUsername = [&](UserData *user) {
			const auto username = PrimaryUsername(user);
			if (username.startsWith(_filter, Qt::CaseInsensitive)) {
				const auto exactUsername
					= (username.size() == _filter.size());
				return exactUsername;
			}
			return true;
		};
		const auto filterNotPassedByName = [&](UserData *user) {
			for (const auto &nameWord : user->nameWords()) {
				if (nameWord.startsWith(_filter, Qt::CaseInsensitive)) {
					const auto exactUsername = !PrimaryUsername(user).compare(
						_filter,
						Qt::CaseInsensitive);
					return exactUsername;
				}
			}
			return filterNotPassedByUsername(user);
		};
		const auto mentionUserIndex = [&](not_null<UserData*> user) {
			return IndexOfInFirstN(mrows, user, int(mrows.size()));
		};
		const auto containsMentionUser = [&](not_null<UserData*> user) {
			return mentionUserIndex(user) >= 0;
		};
		const auto pushMentionRow = [&](
				not_null<UserData*> user,
				MentionRow::Source source) {
			if (containsMentionUser(user)) {
				return;
			}
			mrows.push_back({
				.user = user,
				.source = source,
				.userpic = user->activeUserpicView(),
			});
		};
		const auto markMentionCandidateIfExists = [&](
				not_null<UserData*> user) {
			const auto index = mentionUserIndex(user);
			if (index < 0) {
				return false;
			}
			mrows[index].source = Source::MentionCandidate;
			return true;
		};

		const auto listAllSuggestions = _filter.isEmpty();
		if (_addInlineBots) {
			for (const auto &user : _session->recentInlineBots().list()) {
				if (user->isInaccessible()
					|| (!listAllSuggestions
						&& filterNotPassedByUsername(user))) {
					continue;
				}
				pushMentionRow(user, Source::InlineRecent);
			}
		}
		for (const auto &peer : guestChatBots) {
			const auto user = peer->asUser();
			if (!user
				|| user->isInaccessible()
				|| !user->isBot()
				|| (!listAllSuggestions
					&& filterNotPassedByUsername(user))
				|| containsMentionUser(user)) {
				continue;
			}
			pushMentionRow(user, Source::GuestChatTopPeer);
		}
		const auto skipUser = [&](not_null<UserData*> user) {
			return user->isInaccessible()
				|| (!listAllSuggestions && filterNotPassedByName(user));
		};
		if (_chat) {
			auto sorted = base::flat_multi_map<TimeId, not_null<UserData*>>();
			const auto byOnline = [&](not_null<UserData*> user) {
				return Data::SortByOnlineValue(user, now);
			};
			mrows.reserve(mrows.size() + chatMembers);
			if (_chat->noParticipantInfo()) {
				_chat->session().api().requestFullPeer(_chat);
			} else if (!_chat->participants.empty()) {
				for (const auto &user : _chat->participants) {
					if (skipUser(user)
						|| markMentionCandidateIfExists(user)) {
						continue;
					}
					sorted.emplace(byOnline(user), user);
				}
			}
			for (const auto &user : _chat->lastAuthors) {
				if (skipUser(user)) {
					continue;
				} else if (markMentionCandidateIfExists(user)) {
					sorted.remove(byOnline(user), user);
					continue;
				}
				pushMentionRow(user, Source::MentionCandidate);
				sorted.remove(byOnline(user), user);
			}
			for (const auto &[time, user] : ranges::views::reverse(sorted)) {
				pushMentionRow(user, Source::MentionCandidate);
			}
		} else if (_channel && _channel->isMegagroup()) {
			const auto info = _channel->mgInfo.get();
			const auto owner = &_channel->owner();
			auto &participants = _channel->session().api().chatParticipants();
			if (!_channel->canViewMembers()) {
				if (!info->adminsLoaded) {
					participants.requestAdmins(_channel);
				} else {
					mrows.reserve(mrows.size() + info->admins.size());
					for (const auto &userId : info->admins) {
						const auto user = owner->userLoaded(userId);
						if (!user
							|| skipUser(user)
							|| markMentionCandidateIfExists(user)) {
							continue;
						}
						pushMentionRow(user, Source::MentionCandidate);
					}
				}
			} else if (_channel->lastParticipantsRequestNeeded()) {
				participants.requestLast(_channel);
			} else {
				mrows.reserve(mrows.size() + info->lastParticipants.size());
				for (const auto &user : info->lastParticipants) {
					if (skipUser(user)
						|| markMentionCandidateIfExists(user)) {
						continue;
					}
					pushMentionRow(user, Source::MentionCandidate);
				}
			}
		}
		if (const auto peer = this->peer()) {
			const auto &pins = Custom::MentionPinsFor(_session).list(peer);
			if (!pins.empty()) {
				const auto indexOf = [&](not_null<UserData*> user) {
					const auto i = std::find(
						pins.begin(),
						pins.end(),
						peerToUser(user->id));
					return (i != pins.end())
						? int(i - pins.begin())
						: int(pins.size());
				};
				std::stable_sort(
					mrows.begin(),
					mrows.end(),
					[&](const MentionRow &a, const MentionRow &b) {
						return indexOf(a.user) < indexOf(b.user);
					});
			}
		}
	} else if (_type == Type::Hashtags) {
		const auto listAllSuggestions = _filter.isEmpty();
		const auto &recent = cRecentWriteHashtags();
		hrows.reserve(recent.size());
		for (const auto &item : recent) {
			const auto &tag = item.first;
			if (!listAllSuggestions
				&& (tag.size() == _filter.size()
					|| !TextUtilities::RemoveAccents(tag).startsWith(
						_filter,
						Qt::CaseInsensitive))) {
				continue;
			}
			hrows.push_back(tag);
		}
	} else if (_type == Type::BotCommands) {
		const auto listAllSuggestions = _filter.isEmpty();
		const auto hasUsername = _filter.indexOf('@') > 0;
		using Commands = std::vector<Data::BotCommand>;
		auto bots = base::flat_map<not_null<UserData*>, const Commands*>();
		auto count = 0;
		if (_chat) {
			if (_chat->noParticipantInfo()) {
				_chat->session().api().requestFullPeer(_chat);
			} else if (!_chat->participants.empty()) {
				const auto &commands = _chat->botCommands();
				for (const auto &user : _chat->participants) {
					if (!user->isBot()) {
						continue;
					}
					const auto i = commands.find(peerToUser(user->id));
					if (i != end(commands)) {
						bots.emplace(user, &i->second);
						count += i->second.size();
					}
				}
			}
		} else if (_user && _user->isBot()) {
			if (!_user->botInfo->inited) {
				_user->session().api().requestFullPeer(_user);
			}
			count = _user->botInfo->commands.size();
			bots.emplace(_user, &_user->botInfo->commands);
		} else if (_channel && _channel->isMegagroup()) {
			if (_channel->mgInfo->bots.empty()) {
				if (_channel->mgInfo->botStatus == Data::BotStatus::Unknown) {
					_channel->session().api().chatParticipants().requestBots(
						_channel);
				}
			} else {
				const auto &commands = _channel->mgInfo->botCommands();
				for (const auto &user : _channel->mgInfo->bots) {
					if (!user->isBot()) {
						continue;
					}
					const auto i = commands.find(peerToUser(user->id));
					if (i != end(commands)) {
						bots.emplace(user, &i->second);
						count += i->second.size();
					}
				}
			}
		}
		if (count) {
			const auto make = [&](
					not_null<UserData*> user,
					const Data::BotCommand &command) {
				return BotCommandRow{
					.user = user,
					.command = command.command,
					.description = command.description,
					.userpic = user->activeUserpicView(),
					.ephemeral = command.ephemeral,
				};
			};
			brows.reserve(count);
			const auto botStatus = BotStatusFor(_chat, _channel);
			const auto withUsername = hasUsername
				|| (botStatus != Data::BotStatus::NoBots);
			const auto passes = [&](
					not_null<UserData*> user,
					const Data::BotCommand &command) {
				if (listAllSuggestions) {
					return true;
				}
				const auto toFilter = withUsername
					? command.command + '@' + PrimaryUsername(user)
					: command.command;
				return toFilter.startsWith(_filter, Qt::CaseInsensitive);
			};
			if (_chat) {
				for (const auto &user : _chat->lastAuthors) {
					if (!user->isBot()) {
						continue;
					}
					const auto i = bots.find(user);
					if (i == end(bots)) {
						continue;
					}
					for (const auto &command : *i->second) {
						if (passes(user, command)) {
							brows.push_back(make(user, command));
						}
					}
					bots.erase(i);
				}
			}
			for (const auto &[user, commands] : bots) {
				for (const auto &command : *commands) {
					if (passes(user, command)) {
						brows.push_back(make(user, command));
					}
				}
			}
		}
		const auto shortcuts = (_user && !_user->isBot())
			? _user->owner().shortcutMessages().shortcuts().list
			: base::flat_map<BusinessShortcutId, Data::Shortcut>();
		if (!hasUsername && brows.empty() && !shortcuts.empty()) {
			const auto self = _user->session().user();
			for (const auto &[id, shortcut] : shortcuts) {
				const auto &name = shortcut.name;
				if (shortcut.count < 1
					|| (!listAllSuggestions
						&& !name.startsWith(_filter, Qt::CaseInsensitive))) {
					continue;
				}
				brows.push_back({
					.user = self,
					.command = name,
					.description = tr::lng_forum_messages(
						tr::now,
						lt_count,
						shortcut.count),
					.userpic = self->activeUserpicView(),
				});
			}
			if (!brows.empty()) {
				brows.insert(begin(brows), { .user = self }); // Edit.
			}
		}
	}
	rowsUpdated(
		std::move(mrows),
		std::move(hrows),
		std::move(brows),
		std::move(srows),
		resetScroll);
}

void FieldAutocomplete::rowsUpdated(
		MentionRows &&mrows,
		HashtagRows &&hrows,
		BotCommandRows &&brows,
		StickerRows &&srows,
		bool resetScroll) {
	if (mrows.empty() && hrows.empty() && brows.empty() && srows.empty()) {
		if (!isHidden()) {
			hideAnimated();
		}
		_scroll->scrollToY(0);
		_mrows.clear();
		_hrows.clear();
		_brows.clear();
		_srows.clear();
	} else {
		_mrows = std::move(mrows);
		_hrows = std::move(hrows);
		_brows = std::move(brows);
		_srows = std::move(srows);

		const auto hidden = _hiding || isHidden();
		if (hidden) {
			show();
			_scroll->show();
		}
		recount(resetScroll);
		update();
		if (hidden) {
			hide();
			showAnimated();
		}
	}
	_inner->rowsUpdated();
}

void FieldAutocomplete::createEphemeralHint(QRect rect) {
	const auto parent = parentWidget();
	_ephemeralHint = base::make_unique_q<Ui::ImportantTooltip>(
		parent,
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			parent,
			Ui::MakeNiceTooltipLabel(
				parent,
				tr::lng_ephemeral_command_tooltip(Ui::Text::WithEntities),
				st::dialogsStoriesTooltipMaxWidth,
				st::ttlMediaImportantTooltipLabel),
			st::defaultImportantTooltip.padding),
		st::dialogsStoriesTooltip);
	_ephemeralHint->pointAt(rect, RectPart::Top);
	_ephemeralHint->toggleAnimated(true);
}

void FieldAutocomplete::ephemeralIconHovered(QRect iconRect) {
	const auto parent = parentWidget();
	if (iconRect.isEmpty() || !parent || isHidden() || _hiding) {
		hideEphemeralHint();
		return;
	}
	_ephemeralHintRect = Ui::MapFrom(parent, _inner.data(), iconRect);
	if (_ephemeralHint && !_ephemeralHint->isHidden()) {
		_ephemeralHint->pointAt(_ephemeralHintRect, RectPart::Top);
		_ephemeralHint->toggleAnimated(true);
	} else {
		_ephemeralHintTimer.callOnce(kEphemeralHintHoverDelay);
	}
}

void FieldAutocomplete::showPendingEphemeralHint() {
	if (_ephemeralHintRect.isEmpty() || isHidden() || _hiding) {
		return;
	}
	if (_ephemeralHint) {
		_ephemeralHint->pointAt(_ephemeralHintRect, RectPart::Top);
		_ephemeralHint->toggleAnimated(true);
	} else {
		createEphemeralHint(_ephemeralHintRect);
	}
}

void FieldAutocomplete::hideEphemeralHint() {
	_ephemeralHintRect = QRect();
	_ephemeralHintTimer.cancel();
	if (_ephemeralHint) {
		_ephemeralHint->toggleAnimated(false);
	}
}

void FieldAutocomplete::setBoundings(QRect boundings) {
	_boundings = boundings;
	recount();
}

void FieldAutocomplete::recount(bool resetScroll) {
	const auto oldScrollTop = _scroll->scrollTop();
	const auto maxHeight = int(4.5 * st::mentionHeight);
	auto height = 0;
	if (!_srows.empty()) {
		const auto stickersPerRow = std::max(
			1,
			(_boundings.width() - 2 * st::stickerPanPadding)
				/ st::stickerPanSize.width());
		const auto rows = rowscount(_srows.size(), stickersPerRow);
		height = st::stickerPanPadding + rows * st::stickerPanSize.height();
	} else if (!_mrows.empty()) {
		height = _mrows.size() * st::mentionHeight;
	} else if (!_hrows.empty()) {
		height = _hrows.size() * st::mentionHeight;
	} else if (!_brows.empty()) {
		height = _brows.size() * st::mentionHeight;
	}
	height += _st.autocompleteBottomSkip;

	if (_inner->width() != _boundings.width()
		|| _inner->height() != height) {
		_inner->resize(_boundings.width(), height);
	}
	height = std::min({ height, _boundings.height(), maxHeight });
	const auto top = _boundings.y() + _boundings.height() - height;
	if (width() != _boundings.width() || RpWidget::height() != height) {
		setGeometry(_boundings.x(), top, _boundings.width(), height);
		_scroll->resize(_boundings.width(), height);
	} else if (x() != _boundings.x() || y() != top) {
		move(_boundings.x(), top);
	}
	const auto scrollTop = resetScroll ? 0 : oldScrollTop;
	if (scrollTop != oldScrollTop) {
		_scroll->scrollToY(scrollTop);
	}
	if (resetScroll) {
		_inner->clearSel();
	}
}

void FieldAutocomplete::hideFast() {
	hideEphemeralHint();
	_ephemeralHint = nullptr;
	_opacityAnimation.stop();
	hideFinish();
}

void FieldAutocomplete::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}
	hideEphemeralHint();

	if (_cache.isNull()) {
		_scroll->show();
		_cache = Ui::GrabWidget(this);
	}
	_scroll->hide();
	_hiding = true;
	_opacityAnimation.start(
		[=] { animationCallback(); },
		1.,
		0.,
		st::emojiPanDuration);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void FieldAutocomplete::hideFinish() {
	hide();
	_hiding = false;
	_filter = u"-"_q;
	_inner->clearSel(true);
}

void FieldAutocomplete::showAnimated() {
	if (!isHidden() && !_hiding) {
		return;
	}
	if (_cache.isNull()) {
		_stickersSeed = base::RandomValue<uint64>();
		_scroll->show();
		_cache = Ui::GrabWidget(this);
	}
	_scroll->hide();
	_hiding = false;
	show();
	_opacityAnimation.start(
		[=] { animationCallback(); },
		0.,
		1.,
		st::emojiPanDuration);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void FieldAutocomplete::animationCallback() {
	update();
	if (!_opacityAnimation.animating()) {
		_cache = QPixmap();
		setAttribute(Qt::WA_OpaquePaintEvent);
		if (_hiding) {
			hideFinish();
		} else {
			_scroll->show();
			_inner->clearSel();
		}
	}
}

const QString &FieldAutocomplete::filter() const {
	return _filter;
}

ChatData *FieldAutocomplete::chat() const {
	return _chat;
}

ChannelData *FieldAutocomplete::channel() const {
	return _channel;
}

UserData *FieldAutocomplete::user() const {
	return _user;
}

int FieldAutocomplete::innerTop() const {
	return _scroll->scrollTop();
}

int FieldAutocomplete::innerBottom() const {
	return _scroll->scrollTop() + _scroll->height();
}

bool FieldAutocomplete::chooseSelected(ChooseMethod method) const {
	return _inner->chooseSelected(method);
}

void FieldAutocomplete::startPressDragSelect() {
	if (_inner && !_inner->isHidden()) {
		_inner->startPressDragSelect();
	}
}

void FieldAutocomplete::setSendMenuDetails(
		Fn<SendMenu::Details()> &&callback) {
	_inner->setSendMenuDetails(std::move(callback));
}

bool FieldAutocomplete::eventFilter(QObject *obj, QEvent *e) {
	const auto hidden = isHidden();
	const auto moderate = Core::App().settings().moderateModeEnabled();
	if ((hidden && !moderate) || e->type() != QEvent::KeyPress) {
		return QWidget::eventFilter(obj, e);
	}
	const auto event = static_cast<QKeyEvent*>(e);
	const auto modifiers = Qt::AltModifier
		| Qt::ControlModifier
		| Qt::ShiftModifier
		| Qt::MetaModifier;
	if (event->modifiers() & modifiers) {
		return QWidget::eventFilter(obj, e);
	}
	const auto key = event->key();
	if (!hidden) {
		const auto vertical = (key == Qt::Key_Up) || (key == Qt::Key_Down);
		const auto horizontal = !_srows.empty()
			&& ((key == Qt::Key_Left) || (key == Qt::Key_Right));
		if (vertical || horizontal) {
			return _inner->moveSel(key);
		} else if (key == Qt::Key_Enter || key == Qt::Key_Return) {
			return _inner->chooseSelected(ChooseMethod::ByEnter);
		}
	}
	if (moderate
		&& ((key >= Qt::Key_1 && key <= Qt::Key_9)
			|| key == Qt::Key_Q
			|| key == Qt::Key_W)) {
		return _moderateKeyActivateCallback
			? _moderateKeyActivateCallback(key)
			: false;
	}
	return QWidget::eventFilter(obj, e);
}

FieldAutocomplete::Inner::Inner(
	std::shared_ptr<Show> show,
	const style::EmojiPan &st,
	not_null<FieldAutocomplete*> parent,
	not_null<MentionRows*> mrows,
	not_null<HashtagRows*> hrows,
	not_null<BotCommandRows*> brows,
	not_null<StickerRows*> srows)
: _show(std::move(show))
, _session(&_show->session())
, _st(st)
, _parent(parent)
, _mrows(mrows)
, _hrows(hrows)
, _brows(brows)
, _srows(srows)
, _overBg(st::roundRadiusSmall, _st.overBg)
, _pathGradient(std::make_unique<Ui::PathShiftGradient>(
	_st.pathBg,
	_st.pathFg,
	[=] { update(); }))
, _premiumMark(_session, st::stickersPremiumLock)
, _previewTimer([=] { showPreview(); }) {
	_session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	_show->adjustShadowLeft(
	) | rpl::on_next([=](bool adjust) {
		_adjustShadowLeft = adjust;
		update();
	}, lifetime());
}

void FieldAutocomplete::Inner::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e->rect();
	if (clip != rect()) {
		p.setClipRect(clip);
	}

	if (!_srows->empty()) {
		paintStickers(p, clip);
	} else {
		paintRows(p, clip);
		paintShadow(p, _parent->innerBottom() - st::lineWidth);
	}
	paintShadow(p, _parent->innerTop());
}

void FieldAutocomplete::Inner::paintShadow(QPainter &p, int top) {
	const auto left = _adjustShadowLeft ? st::lineWidth : 0;
	p.fillRect(left, top, width() - left, st::lineWidth, st::shadowFg);
}

void FieldAutocomplete::Inner::paintStickers(Painter &p, QRect clip) {
	_pathGradient->startFrame(
		0,
		width(),
		std::min(st::msgMaxWidth / 2, width() / 2));

	const auto now = crl::now();
	const auto paused = _show->paused(PauseReason::TabbedPanel);
	const auto padding = st::stickerPanPadding;
	const auto &single = st::stickerPanSize;
	const auto rows = rowscount(_srows->size(), _stickersPerRow);
	const auto [fromRow, tillRow] = Ui::RowsInRange(
		clip.y() - padding,
		clip.y() + clip.height() - padding,
		single.height(),
		rows);
	const auto [fromColumn, tillColumn] = Ui::RowsInRange(
		clip.x() - padding,
		clip.x() + clip.width() - padding,
		single.width(),
		_stickersPerRow);
	for (auto row = fromRow; row != tillRow; ++row) {
		for (auto column = fromColumn; column != tillColumn; ++column) {
			const auto index = row * _stickersPerRow + column;
			if (index >= _srows->size()) {
				break;
			}

			auto &sticker = (*_srows)[index];
			const auto document = sticker.document;
			const auto &media = sticker.documentMedia;
			const auto info = document->sticker();
			if (!info) {
				continue;
			}

			if (media->loaded()) {
				if (info->isLottie() && !sticker.lottie) {
					setupLottie(sticker);
				} else if (info->isWebm()
					&& !sticker.webm
					&& !sticker.webm.isBad()) {
					setupWebm(sticker);
				}
			}

			const auto position = QPoint(
				padding + column * single.width(),
				padding + row * single.height());
			if (_sel == index) {
				_overBg.paint(p, myrtlrect(QRect(position, single)));
			}

			media->checkStickerSmall();
			const auto size = ComputeStickerSize(
				document,
				stickerBoundingBox());
			const auto innerPos = position + QPoint(
				(single.width() - size.width()) / 2,
				(single.height() - size.height()) / 2);
			auto lottieFrame = QImage();
			if (sticker.lottie && sticker.lottie->ready()) {
				lottieFrame = sticker.lottie->frame();
				p.drawImage(
					QRect(
						innerPos,
						lottieFrame.size() / style::DevicePixelRatio()),
					lottieFrame);
				if (!paused) {
					sticker.lottie->markFrameShown();
				}
			} else if (sticker.webm && sticker.webm->started()) {
				p.drawImage(
					innerPos,
					sticker.webm->current(
						{
							.frame = size,
							.keepAlpha = true,
						},
						paused ? 0 : now));
			} else if (const auto image = media->getStickerSmall()) {
				p.drawPixmapLeft(innerPos, width(), image->pix(size));
			} else {
				PaintStickerThumbnailPath(
					p,
					media.get(),
					QRect(innerPos, size),
					_pathGradient.get());
			}

			if (document->isPremiumSticker()) {
				_premiumMark.paint(
					p,
					lottieFrame,
					sticker.premiumLock,
					position,
					single,
					width());
			}
		}
	}
}

int FieldAutocomplete::Inner::pinnedMentionRowsCount() const {
	if (_mrows->empty() || !_srows->empty() || (_parent->_type != Type::Mentions)) {
		return 0;
	}
	const auto peer = _parent->peer();
	if (!peer) {
		return 0;
	}
	const auto &pins = Custom::MentionPinsFor(_session);
	auto count = 0;
	for (const auto &row : *_mrows) {
		if (pins.has(peer, row.user)) {
			++count;
		} else {
			break;
		}
	}
	return count;
}

int FieldAutocomplete::Inner::visualSlotForPinnedRow(int index) const {
	if (!_pinDragActive || index < 0) {
		return index;
	}
	if (index == _pinDragOriginIndex) {
		return _pinDragTargetIndex;
	}
	if (_pinDragOriginIndex < _pinDragTargetIndex) {
		if (index > _pinDragOriginIndex && index <= _pinDragTargetIndex) {
			return index - 1;
		}
	} else if (_pinDragOriginIndex > _pinDragTargetIndex) {
		if (index >= _pinDragTargetIndex && index < _pinDragOriginIndex) {
			return index + 1;
		}
	}
	return index;
}

void FieldAutocomplete::Inner::paintMentionRowAt(
		Painter &p,
		int index,
		int rowTop,
		bool selected,
		bool dragged,
		bool isPinned) {
	if (index < 0 || index >= int(_mrows->size())) {
		return;
	}
	auto &row = _mrows->at(index);
	const auto user = row.user;
	const auto uid = peerToUser(user->id);

	if (dragged) {
		p.fillRect(0, rowTop, width(), st::mentionHeight, st::mentionBgOver);
		p.fillRect(0, rowTop, width(), st::lineWidth, st::shadowFg);
		p.fillRect(
			0,
			rowTop + st::mentionHeight - st::lineWidth,
			width(),
			st::lineWidth,
			st::shadowFg);
	} else if (selected) {
		p.fillRect(0, rowTop, width(), st::mentionHeight, st::mentionBgOver);
	}

	auto pinRatio = isPinned ? 1.0 : 0.0;
	if (const auto it = _pinIconAnims.find(uid); it != _pinIconAnims.end()) {
		if (it->second.animating()) {
			pinRatio = it->second.value(isPinned ? 1.0 : 0.0);
		} else if (_pinClosingUsers.contains(uid)) {
			_pinClosingUsers.remove(uid);
			_pinIconAnims.erase(it);
		} else {
			_pinIconAnims.erase(it);
		}
	}

	const auto removable = !isPinned && isRemovableMentionRow(index);
	const auto &pinIcon = Dialogs::ThreeStateIcon(
		st::dialogsPinnedIcon,
		false,
		selected || dragged);
	const auto rightReserved = (pinRatio > 0.001)
		? (pinIcon.width() + st::mentionPadding.right())
		: (selected && removable)
		? (st::smallCloseIconOver.width() + st::mentionPadding.right())
		: 0;

	if (selected && removable && pinRatio <= 0.001) {
		const auto &closeIcon = st::smallCloseIconOver;
		const auto skip = (st::mentionHeight - closeIcon.height()) / 2;
		closeIcon.paint(
			p,
			QPoint(width() - closeIcon.width() - skip, rowTop + skip),
			width());
	}

	const auto mentionLeft = 2 * st::mentionPadding.left()
		+ st::mentionPhotoSize;
	auto &badgeDetector = Custom::ClientBadgeFor(_session);
	const auto badgeWidth = badgeDetector.badgeWidth(user);
	const auto mentionWidth = std::max(
		0,
		width()
			- mentionLeft
			- 2 * st::mentionPadding.right()
			- rightReserved
			- badgeWidth);

	const auto filter = _parent->filter();
	const auto filterSize = int(filter.size());
	const auto filterIsEmpty = filter.isEmpty();
	const auto &font = st::mentionFont;
	const auto username = PrimaryUsername(user);
	auto first = (!filterIsEmpty
		&& username.startsWith(filter, Qt::CaseInsensitive))
		? ('@' + username.mid(0, filterSize))
		: QString();
	auto second = first.isEmpty()
		? (username.isEmpty() ? QString() : ('@' + username))
		: username.mid(filterSize);
	const auto firstWidth = font->width(first);
	const auto secondWidth = font->width(second);
	auto usernameWidth = firstWidth + secondWidth;
	if (row.name.isEmpty()) {
		row.name.setText(
			st::msgNameStyle,
			user->name(),
			Ui::NameTextOptions());
	}
	auto nameWidth = row.name.maxWidth();
	const auto gapWidth = username.isEmpty() ? 0 : st::mentionPadding.right();
	if (mentionWidth < usernameWidth + gapWidth + nameWidth) {
		const auto availText = std::max(0, mentionWidth - gapWidth);
		nameWidth = (usernameWidth + nameWidth > 0)
			? ((availText * nameWidth) / (nameWidth + usernameWidth))
			: 0;
		usernameWidth = std::max(0, availText - nameWidth);
		if (firstWidth < usernameWidth + font->elidew) {
			if (firstWidth < usernameWidth) {
				first = font->elided(first, usernameWidth);
			} else if (!second.isEmpty()) {
				first = font->elided(first + second, usernameWidth);
				second = QString();
			}
		} else {
			second = font->elided(second, usernameWidth - firstWidth);
		}
	}

	const auto photoTop = rowTop
		+ (st::mentionHeight - st::mentionPhotoSize) / 2;
	user->loadUserpic();
	user->paintUserpicLeft(
		p,
		row.userpic,
		st::mentionPadding.left(),
		photoTop,
		width(),
		st::mentionPhotoSize);

	const auto nameFontHeight = st::msgNameStyle.font->height;
	const auto nameTop = rowTop + (st::mentionHeight - nameFontHeight) / 2;
	p.setPen((selected || dragged) ? st::mentionNameFgOver : st::mentionNameFg);
	row.name.drawElided(p, mentionLeft, nameTop, nameWidth);

	auto cursorX = mentionLeft + std::min(nameWidth, row.name.maxWidth());
	if (badgeWidth > 0) {
		const auto surfaceBg = (selected || dragged)
			? st::mentionBgOver->c
			: st::mentionBg->c;
		const auto badgeTint = (selected || dragged)
			? st::mentionNameFgOver->c
			: st::dialogsVerifiedIconBg->c;
		cursorX += badgeDetector.drawBadgeGetWidth(
			p,
			cursorX,
			nameTop,
			nameFontHeight,
			user,
			surfaceBg,
			badgeTint);
	}

	if (!first.isEmpty() || !second.isEmpty()) {
		const auto usernameLeft = cursorX + gapWidth;
		const auto baselineY = nameTop + st::msgNameStyle.font->ascent;
		p.setFont(font);
		if (!first.isEmpty()) {
			p.setPen((selected || dragged)
				? st::mentionFgOverActive
				: st::mentionFgActive);
			p.drawText(usernameLeft, baselineY, first);
		}
		if (!second.isEmpty()) {
			p.setPen((selected || dragged)
				? st::mentionFgOver
				: st::mentionFg);
			p.drawText(usernameLeft + firstWidth, baselineY, second);
		}
	}

	if (pinRatio > 0.001) {
		const auto pinLeft = width()
			- st::mentionPadding.right()
			- pinIcon.width();
		const auto pinTop = rowTop
			+ (st::mentionHeight - pinIcon.height()) / 2;
		const auto center = QPointF(
			pinLeft + pinIcon.width() / 2.0,
			pinTop + pinIcon.height() / 2.0);
		p.save();
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(p.opacity() * std::clamp(pinRatio, 0.0, 1.0));
		p.translate(center);
		const auto scale = 0.35 + 0.65 * pinRatio;
		p.scale(scale, scale);
		p.rotate((1.0 - pinRatio) * -35.0);
		p.translate(-center);
		pinIcon.paint(p, pinLeft, pinTop, width());
		p.restore();
	}
}

void FieldAutocomplete::Inner::paintRows(Painter &p, QRect clip) {
	if (!_mrows->empty()) {
		const auto last = int(_mrows->size());
		const auto pinnedCount = pinnedMentionRowsCount();
		for (auto i = 0; i != last; ++i) {
			if (_pinDragActive && i == _pinDragOriginIndex) {
				continue;
			}
			const auto isPinned = (i < pinnedCount);
			const auto slot = isPinned ? visualSlotForPinnedRow(i) : i;
			const auto uid = peerToUser(_mrows->at(i).user->id);
			auto shift = 0.0;
			if (const auto it = _rowShiftAnims.find(uid)
				; it != _rowShiftAnims.end()) {
				if (it->second.animating()) {
					shift = it->second.value(0.0);
				} else {
					_rowShiftAnims.erase(it);
				}
			}
			const auto rowTop = slot * st::mentionHeight
				+ int(base::SafeRound(shift));
			if (rowTop + st::mentionHeight <= clip.top()
				|| rowTop > clip.bottom()) {
				continue;
			}
			const auto selected = !_pinDragActive && (i == _sel);
			paintMentionRowAt(p, i, rowTop, selected, false, isPinned);
		}
		if (_pinDragActive
			&& _pinDragOriginIndex >= 0
			&& _pinDragOriginIndex < last) {
			paintMentionRowAt(
				p,
				_pinDragOriginIndex,
				_pinDragCurrentTop,
				true,
				true,
				true);
		}
		return;
	}

	const auto mentionLeft = 2 * st::mentionPadding.left()
		+ st::mentionPhotoSize;
	const auto mentionWidth = width()
		- mentionLeft
		- 2 * st::mentionPadding.right();
	const auto hashtagLeft = st::historyAttach.width
		+ st::historyComposeField.textMargins.left()
		- st::lineWidth;
	const auto hashtagWidth = width()
		- st::mentionPadding.right()
		- hashtagLeft
		- st::defaultScrollArea.width;

	const auto from = clip.top() / st::mentionHeight;
	const auto till = clip.bottom() / st::mentionHeight + 1;
	const auto last = rowsCount();
	const auto filter = _parent->filter();
	const auto withUsername = commandsWithUsername();
	const auto filterSize = int(filter.size());
	const auto filterIsEmpty = filter.isEmpty();
	const auto &font = st::mentionFont;
	for (auto i = from; i < till; ++i) {
		if (i >= last) {
			break;
		}
		const auto top = i * st::mentionHeight;
		const auto textTop = top + st::mentionTop;
		const auto selected = (i == _sel);
		if (selected) {
			p.fillRect(
				0,
				top,
				width(),
				st::mentionHeight,
				st::mentionBgOver);
			if (!_hrows->empty() || isRemovableMentionRow(i)) {
				const auto &icon = st::smallCloseIconOver;
				const auto skip = (st::mentionHeight - icon.height()) / 2;
				icon.paint(
					p,
					QPoint(width() - icon.width() - skip, top + skip),
					width());
			}
		}
		if (!_hrows->empty()) {
			const auto &hashtag = _hrows->at(i);
			auto first = filterIsEmpty
				? QString()
				: ('#' + hashtag.mid(0, filterSize));
			auto second = filterIsEmpty
				? ('#' + hashtag)
				: hashtag.mid(filterSize);
			const auto firstWidth = font->width(first);
			const auto secondWidth = font->width(second);
			if (hashtagWidth < firstWidth + secondWidth) {
				if (hashtagWidth < firstWidth + font->elidew) {
					first = font->elided(first + second, hashtagWidth);
					second = QString();
				} else {
					second = font->elided(second, hashtagWidth - firstWidth);
				}
			}

			p.setFont(font);
			if (!first.isEmpty()) {
				p.setPen(selected
					? st::mentionFgOverActive
					: st::mentionFgActive);
				p.drawText(hashtagLeft, textTop + font->ascent, first);
			}
			if (!second.isEmpty()) {
				p.setPen(selected ? st::mentionFgOver : st::mentionFg);
				p.drawText(
					hashtagLeft + firstWidth,
					textTop + font->ascent,
					second);
			}
		} else {
			auto &row = _brows->at(i);
			const auto user = row.user;
			if (user->isSelf() && row.command.isEmpty()) {
				p.setPen(st::windowActiveTextFg);
				p.setFont(st::semiboldFont);
				p.drawText(
					QRect(0, top, width(), st::mentionHeight),
					tr::lng_replies_edit_button(tr::now),
					style::al_center);
				continue;
			}

			auto toHighlight = row.command;
			if (withUsername) {
				toHighlight += '@' + PrimaryUsername(user);
			}
			user->loadUserpic();
			user->paintUserpicLeft(
				p,
				row.userpic,
				st::mentionPadding.left(),
				top + st::mentionPadding.top(),
				width(),
				st::mentionPhotoSize);

			const auto commandText = '/' + toHighlight;

			p.setPen(selected ? st::mentionNameFgOver : st::mentionNameFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				mentionLeft,
				textTop + st::semiboldFont->ascent,
				commandText);

			auto addLeft = st::semiboldFont->width(commandText)
				+ st::mentionPadding.left();
			auto widthLeft = mentionWidth - addLeft;

			if (row.ephemeral) {
				const auto &icon = selected
					? st::mentionEphemeralIconOver
					: st::mentionEphemeralIcon;
				icon.paint(
					p,
					mentionLeft + addLeft,
					top + (st::mentionHeight - icon.height()) / 2,
					width());
				addLeft += icon.width() + st::mentionEphemeralIconSkip;
				widthLeft -= icon.width() + st::mentionEphemeralIconSkip;
			}
			if (!row.description.isEmpty()
				&& row.descriptionText.isEmpty()) {
				row.descriptionText.setText(
					st::defaultTextStyle,
					row.description,
					Ui::NameTextOptions());
			}
			if (widthLeft > font->elidew && !row.descriptionText.isEmpty()) {
				p.setPen(selected ? st::mentionFgOver : st::mentionFg);
				row.descriptionText.drawElided(
					p,
					mentionLeft + addLeft,
					textTop,
					widthLeft);
			}
		}
	}
}

void FieldAutocomplete::Inner::resizeEvent(QResizeEvent *e) {
	_stickersPerRow = std::max(
		1,
		(width() - 2 * st::stickerPanPadding) / st::stickerPanSize.width());
}

void FieldAutocomplete::Inner::mouseMoveEvent(QMouseEvent *e) {
	if (_pinDragCandidate && (e->buttons() & Qt::LeftButton)) {
		const auto pinnedCount = pinnedMentionRowsCount();
		if (!_pinDragActive
			&& pinnedCount > 1
			&& _down >= 0
			&& _down < pinnedCount
			&& (std::abs(e->pos().y() - _pinDragStartPos.y())
				>= style::ConvertScale(5))) {
			_pinDragActive = true;
			_pinDragOriginIndex = _down;
			_pinDragTargetIndex = _down;
			setCursor(Qt::ClosedHandCursor);
		}
		if (_pinDragActive && pinnedCount > 1) {
			_pinDragCurrentTop = std::clamp(
				e->pos().y() - _pinDragGrabOffsetY,
				0,
				(pinnedCount - 1) * st::mentionHeight);
			const auto newTarget = std::clamp(
				(_pinDragCurrentTop + st::mentionHeight / 2)
					/ st::mentionHeight,
				0,
				pinnedCount - 1);
			if (newTarget != _pinDragTargetIndex) {
				auto oldVisual = std::vector<int>(pinnedCount, 0);
				for (auto k = 0; k != pinnedCount; ++k) {
					oldVisual[k] = visualSlotForPinnedRow(k);
				}
				_pinDragTargetIndex = newTarget;
				for (auto k = 0; k != pinnedCount; ++k) {
					if (k == _pinDragOriginIndex) {
						continue;
					}
					const auto nowSlot = visualSlotForPinnedRow(k);
					if (nowSlot != oldVisual[k]) {
						const auto uid = peerToUser(_mrows->at(k).user->id);
						const auto prevDelta = _rowShiftAnims[uid].value(0.0);
						const auto startOffset = prevDelta
							+ (oldVisual[k] - nowSlot) * st::mentionHeight;
						_rowShiftAnims[uid].start(
							[this] { update(); },
							startOffset,
							0.0,
							170,
							anim::easeOutCubic);
					}
				}
			}
			update();
			return;
		}
	}

	const auto globalPosition = e->globalPos();
	updateEphemeralIconHover(e->pos());
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	selectByMouse(globalPosition);
}

void FieldAutocomplete::Inner::updateEphemeralIconHover(QPoint position) {
	const auto inCommands = !_brows->empty()
		&& _srows->empty()
		&& _mrows->empty()
		&& _hrows->empty();
	const auto index = inCommands ? (position.y() / st::mentionHeight) : -1;
	const auto good = (index >= 0)
		&& (index < int(_brows->size()))
		&& _brows->at(index).ephemeral
		&& ephemeralIconRect(index).contains(position);
	const auto hovered = good ? index : -1;
	if (_ephemeralIconHover == hovered) {
		return;
	}
	_ephemeralIconHover = hovered;
	_ephemeralIconHovered.fire(good
		? ephemeralIconRect(hovered)
		: QRect());
}

QRect FieldAutocomplete::Inner::ephemeralIconRect(int index) const {
	const auto &row = _brows->at(index);
	if (!row.ephemeral) {
		return QRect();
	}
	auto toHighlight = row.command;
	if (commandsWithUsername()) {
		toHighlight += '@' + PrimaryUsername(row.user);
	}
	const auto mentionLeft = 2 * st::mentionPadding.left()
		+ st::mentionPhotoSize;
	const auto left = mentionLeft
		+ st::semiboldFont->width('/' + toHighlight)
		+ st::mentionPadding.left();
	const auto top = index * st::mentionHeight
		+ (st::mentionHeight - st::mentionEphemeralIcon.height()) / 2;
	return QRect(
		left,
		top,
		st::mentionEphemeralIcon.width(),
		st::mentionEphemeralIcon.height());
}

void FieldAutocomplete::Inner::clearSel(bool hidden) {
	_overDelete = false;
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	if (hidden) {
		_pinDragCandidate = false;
		_pinDragActive = false;
		_pinDragOriginIndex = -1;
		_pinDragTargetIndex = -1;
		unsetCursor();
	}
	setSel((_mrows->empty() && _brows->empty() && _hrows->empty())
		? -1
		: (_brows->size() > 1
			&& _brows->front().user->isSelf()
			&& _brows->front().command.isEmpty())
		? 1
		: 0);
	if (hidden) {
		_down = -1;
		_previewShown = false;
	}
}

bool FieldAutocomplete::Inner::moveSel(int key) {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;

	const auto maxSel = rowsCount();
	auto direction = (key == Qt::Key_Up)
		? -1
		: (key == Qt::Key_Down)
		? 1
		: 0;
	if (!_srows->empty()) {
		if (key == Qt::Key_Left) {
			direction = -1;
		} else if (key == Qt::Key_Right) {
			direction = 1;
		} else {
			direction *= _stickersPerRow;
		}
	}
	if (_sel >= maxSel || _sel < 0) {
		if (direction < -1) {
			setSel(((maxSel - 1) / _stickersPerRow) * _stickersPerRow, true);
		} else if (direction < 0) {
			setSel(maxSel - 1, true);
		} else {
			setSel(0, true);
		}
		return (_sel >= 0 && _sel < maxSel);
	}
	const auto next = _sel + direction;
	setSel((next >= maxSel || next < 0) ? -1 : next, true);
	return true;
}

bool FieldAutocomplete::Inner::chooseSelected(
		FieldAutocomplete::ChooseMethod method) const {
	return chooseAtIndex(method, _sel);
}

bool FieldAutocomplete::Inner::chooseAtIndex(
		FieldAutocomplete::ChooseMethod method,
		int index,
		Api::SendOptions options) const {
	if (index < 0 || (method == ChooseMethod::ByEnter && _mouseSelection)) {
		return false;
	}
	if (!_srows->empty()) {
		if (index < _srows->size()) {
			const auto document = (*_srows)[index].document;

			const auto from = [&]() -> Ui::MessageSendingAnimationFrom {
				if (options.scheduled) {
					return {};
				}
				const auto bounding = selectedRect(index);
				auto contentRect = Rect(
					ComputeStickerSize(document, stickerBoundingBox()));
				contentRect.moveCenter(rect::center(bounding));
				return {
					.type = Ui::MessageSendingAnimationFrom::Type::Sticker,
					.localId = _show->session().data().nextLocalMessageId(),
					.globalStartGeometry = mapToGlobal(contentRect),
				};
			};

			_stickerChosen.fire({ document, options, from() });
			return true;
		}
	} else if (!_mrows->empty()) {
		if (index < _mrows->size()) {
			const auto user = _mrows->at(index).user;
			_mentionChosen.fire({ user, PrimaryUsername(user), method });
			return true;
		}
	} else if (!_hrows->empty()) {
		if (index < _hrows->size()) {
			_hashtagChosen.fire({ '#' + _hrows->at(index), method });
			return true;
		}
	} else if (!_brows->empty()) {
		if (index < _brows->size()) {
			const auto user = _brows->at(index).user;
			const auto &command = _brows->at(index).command;
			const auto commandString = u"/%1%2"_q.arg(
				command,
				commandsWithUsername()
					? ('@' + PrimaryUsername(user))
					: QString());
			_botCommandChosen.fire({ user, commandString, method });
			return true;
		}
	}
	return false;
}

bool FieldAutocomplete::Inner::isRemovableMentionRow(int index) const {
	return (index >= 0)
		&& (index < _mrows->size())
		&& _mrows->at(index).removable();
}

void FieldAutocomplete::Inner::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());
	if (e->button() == Qt::LeftButton) {
		if (_overDelete
			&& (_mrows->empty()
				? (_sel >= 0 && _sel < _hrows->size())
				: isRemovableMentionRow(_sel))) {
			auto writeRecent = false;
			if (_mrows->empty()) {
				const auto toRemove = _hrows->at(_sel);
				auto &recent = cRefRecentWriteHashtags();
				for (auto i = recent.begin(); i != recent.cend();) {
					if (i->first == toRemove) {
						i = recent.erase(i);
						writeRecent = true;
					} else {
						++i;
					}
				}
			} else {
				const auto &row = _mrows->at(_sel);
				switch (row.source) {
				case MentionRow::Source::InlineRecent:
					_session->recentInlineBots().remove(row.user);
					break;
				case MentionRow::Source::GuestChatTopPeer:
					_session->topGuestChatBots().remove(row.user);
					break;
				case MentionRow::Source::MentionCandidate:
					break;
				}
			}
			if (writeRecent) {
				_show->session().local().writeRecentHashtagsAndBots();
			}
			_parent->updateFiltered();

			selectByMouse(e->globalPos());
		} else if (_srows->empty()) {
			const auto pinnedCount = pinnedMentionRowsCount();
			if (!_mrows->empty() && _sel >= 0 && _sel < pinnedCount) {
				_down = _sel;
				_pinDragCandidate = true;
				_pinDragActive = false;
				_pinDragStartPos = e->pos();
				_pinDragGrabOffsetY = e->pos().y() - (_sel * st::mentionHeight);
				_pinDragCurrentTop = _sel * st::mentionHeight;
			} else {
				chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
			}
		} else {
			_down = _sel;
			_previewTimer.callOnce(QApplication::startDragTime());
		}
	}
}

void FieldAutocomplete::Inner::startPressDragSelect() {
	if (_dragSelect || isHidden()) {
		return;
	}
	_dragSelect = true;
	selectByMouse(QCursor::pos());
	grabMouse();
}

void FieldAutocomplete::Inner::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	const auto pressed = std::exchange(_down, -1);

	if (_pinDragCandidate) {
		_pinDragCandidate = false;
		unsetCursor();
		if (_pinDragActive) {
			const auto wasOrigin = _pinDragOriginIndex;
			const auto wasTarget = _pinDragTargetIndex;
			const auto dropTop = _pinDragCurrentTop;
			_pinDragActive = false;
			_pinDragOriginIndex = -1;
			_pinDragTargetIndex = -1;

			const auto peer = _parent->peer();
			const auto pinnedCount = pinnedMentionRowsCount();
			if (peer
				&& wasOrigin >= 0
				&& wasOrigin < pinnedCount
				&& wasTarget >= 0
				&& wasTarget < pinnedCount) {
				const auto draggedUid = peerToUser(
					_mrows->at(wasOrigin).user->id);
				if (wasOrigin != wasTarget) {
					auto subset = std::vector<UserId>();
					subset.reserve(pinnedCount);
					for (auto k = 0; k != pinnedCount; ++k) {
						if (k != wasOrigin) {
							subset.push_back(peerToUser(_mrows->at(k).user->id));
						}
					}
					subset.insert(subset.begin() + wasTarget, draggedUid);
					Custom::MentionPinsFor(_session).reorderPinned(peer, subset);
					_parent->updateFiltered();
				}
				const auto settleOffset = dropTop
					- (wasTarget * st::mentionHeight);
				if (std::abs(settleOffset) > 0) {
					_rowShiftAnims[draggedUid].start(
						[this] { update(); },
						double(settleOffset),
						0.0,
						150,
						anim::easeOutCubic);
				}
				setSel(wasTarget);
			}
			update();
			return;
		}
		selectByMouse(e->globalPos());
		if (_sel >= 0 && _sel == pressed && _srows->empty()) {
			chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
		}
		return;
	}

	selectByMouse(e->globalPos());

	if (_dragSelect) {
		_dragSelect = false;
		if (mouseGrabber() == this) {
			releaseMouse();
		}
		if (_sel >= 0 && _srows->empty()) {
			chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
		}
		return;
	}

	if (_previewShown) {
		_previewShown = false;
		return;
	} else if (_sel < 0 || _sel != pressed || _srows->empty()) {
		return;
	}
	chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
}

void FieldAutocomplete::Inner::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		selectByMouse(e->globalPos());
	}
	if (_srows->empty()
		&& _parent->_type == FieldAutocomplete::Type::Mentions
		&& _sel >= 0
		&& _sel < int(_mrows->size())) {
		const auto peer = _parent->peer();
		if (!peer) {
			return;
		}
		const auto user = _mrows->at(_sel).user;
		auto &pins = Custom::MentionPinsFor(_session);
		const auto pinned = pins.has(peer, user);
		_menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		_menu->addAction(
			pinned ? tr::lng_pinned_unpin(tr::now) : tr::lng_pinned_pin(tr::now),
			crl::guard(this, [=] {
				auto oldRowIndex = base::flat_map<UserId, int>();
				for (auto k = 0; k != int(_mrows->size()); ++k) {
					oldRowIndex[peerToUser(_mrows->at(k).user->id)] = k;
				}
				const auto uid = peerToUser(user->id);
				auto &manager = Custom::MentionPinsFor(_session);
				const auto wasPinned = manager.has(peer, user);
				if (wasPinned) {
					manager.unpin(peer, user);
					_pinClosingUsers.emplace(uid);
					_pinIconAnims[uid].start(
						[this] { update(); },
						1.0,
						0.0,
						190,
						anim::easeOutCubic);
				} else {
					manager.pin(peer, user);
					_pinClosingUsers.remove(uid);
					_pinIconAnims[uid].start(
						[this] { update(); },
						0.0,
						1.0,
						230,
						anim::easeOutBack);
				}
				_parent->updateFiltered();
				for (auto k = 0; k != int(_mrows->size()); ++k) {
					const auto rowUid = peerToUser(_mrows->at(k).user->id);
					if (const auto it = oldRowIndex.find(rowUid)
						; it != oldRowIndex.end() && it->second != k) {
						_rowShiftAnims[rowUid].start(
							[this] { update(); },
							double((it->second - k) * st::mentionHeight),
							0.0,
							220,
							anim::easeOutCubic);
					}
				}
				update();
			}));
		_menu->popup(e->globalPos());
		return;
	}
	if (_sel < 0 || _srows->empty() || _down >= 0) {
		return;
	}
	const auto index = _sel;
	const auto details = _sendMenuDetails
		? _sendMenuDetails()
		: SendMenu::Details();
	const auto method = FieldAutocomplete::ChooseMethod::ByClick;
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);

	const auto send = crl::guard(this, [=](Api::SendOptions options) {
		chooseAtIndex(method, index, options);
	});
	SendMenu::FillSendMenu(
		_menu,
		_show,
		details,
		SendMenu::DefaultCallback(_show, send));
	if (!_menu->empty()) {
		_menu->popup(QCursor::pos());
	}
}

void FieldAutocomplete::Inner::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void FieldAutocomplete::Inner::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	if (_ephemeralIconHover >= 0) {
		_ephemeralIconHover = -1;
		_ephemeralIconHovered.fire(QRect());
	}
	if (_mouseSelection) {
		setSel(-1);
		_mouseSelection = false;
		_lastMousePosition = std::nullopt;
	}
}

QRect FieldAutocomplete::Inner::selectedRect(int index) const {
	if (index < 0) {
		return QRect();
	}
	if (_srows->empty()) {
		return { 0, index * st::mentionHeight, width(), st::mentionHeight };
	}
	const auto row = index / _stickersPerRow;
	const auto column = index % _stickersPerRow;
	return QRect(
		st::stickerPanPadding + column * st::stickerPanSize.width(),
		st::stickerPanPadding + row * st::stickerPanSize.height(),
		st::stickerPanSize.width(),
		st::stickerPanSize.height());
}

int FieldAutocomplete::Inner::rowsCount() const {
	return !_mrows->empty()
		? int(_mrows->size())
		: !_hrows->empty()
		? int(_hrows->size())
		: !_brows->empty()
		? int(_brows->size())
		: int(_srows->size());
}

bool FieldAutocomplete::Inner::commandsWithUsername() const {
	const auto botStatus = BotStatusFor(_parent->chat(), _parent->channel());
	return (botStatus != Data::BotStatus::NoBots)
		|| (_parent->filter().indexOf('@') > 0);
}

void FieldAutocomplete::Inner::updateSelectedRow() {
	const auto rect = selectedRect(_sel);
	if (rect.isValid()) {
		update(rect);
	}
}

void FieldAutocomplete::Inner::setSel(int sel, bool scroll) {
	updateSelectedRow();
	_sel = sel;
	updateSelectedRow();

	if (scroll && _sel >= 0) {
		if (_srows->empty()) {
			_scrollToRequested.fire({
				_sel * st::mentionHeight,
				(_sel + 1) * st::mentionHeight });
		} else {
			const auto row = _sel / _stickersPerRow;
			const auto padding = st::stickerPanPadding;
			_scrollToRequested.fire({
				(row ? padding : 0) + row * st::stickerPanSize.height(),
				(padding
					+ (row + 1) * st::stickerPanSize.height()
					+ _st.autocompleteBottomSkip) });
		}
	}
}

void FieldAutocomplete::Inner::rowsUpdated() {
	if (_srows->empty()) {
		_stickersLifetime.destroy();
	}
	if (_ephemeralIconHover >= 0) {
		_ephemeralIconHover = -1;
		_ephemeralIconHovered.fire(QRect());
	}
}

auto FieldAutocomplete::Inner::getLottieRenderer()
-> std::shared_ptr<Lottie::FrameRenderer> {
	if (auto result = _lottieRenderer.lock()) {
		return result;
	}
	auto result = Lottie::MakeFrameRenderer();
	_lottieRenderer = result;
	return result;
}

void FieldAutocomplete::Inner::setupLottie(StickerSuggestion &suggestion) {
	const auto document = suggestion.document;
	suggestion.lottie = LottiePlayerFromDocument(
		suggestion.documentMedia.get(),
		StickerLottieSize::InlineResults,
		stickerBoundingBox() * style::DevicePixelRatio(),
		Lottie::Quality::Default,
		getLottieRenderer());

	suggestion.lottie->updates(
	) | rpl::on_next([=] {
		repaintSticker(document);
	}, _stickersLifetime);
}

void FieldAutocomplete::Inner::setupWebm(StickerSuggestion &suggestion) {
	const auto document = suggestion.document;
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification, document);
	};
	suggestion.webm = Media::Clip::MakeReader(
		suggestion.documentMedia->owner()->location(),
		suggestion.documentMedia->bytes(),
		std::move(callback));
}

QSize FieldAutocomplete::Inner::stickerBoundingBox() const {
	return QSize(
		st::stickerPanSize.width() - st::roundRadiusSmall * 2,
		st::stickerPanSize.height() - st::roundRadiusSmall * 2);
}

void FieldAutocomplete::Inner::repaintSticker(
		not_null<DocumentData*> document) {
	const auto i = ranges::find(
		*_srows,
		document,
		&StickerSuggestion::document);
	if (i == end(*_srows)) {
		return;
	}
	repaintStickerAtIndex(i - begin(*_srows));
}

void FieldAutocomplete::Inner::repaintStickerAtIndex(int index) {
	update(selectedRect(index));
}

void FieldAutocomplete::Inner::clipCallback(
		Media::Clip::Notification notification,
		not_null<DocumentData*> document) {
	const auto i = ranges::find(
		*_srows,
		document,
		&StickerSuggestion::document);
	if (i == end(*_srows)) {
		return;
	}
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		if (!i->webm) {
			break;
		} else if (i->webm->state() == State::Error) {
			i->webm.setBad();
		} else if (i->webm->ready() && !i->webm->started()) {
			const auto size = ComputeStickerSize(
				i->document,
				stickerBoundingBox());
			i->webm->start({ .frame = size, .keepAlpha = true });
		}
	} break;

	case Notification::Repaint: break;
	}
	repaintStickerAtIndex(i - begin(*_srows));
}

void FieldAutocomplete::Inner::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	const auto mouse = mapFromGlobal(globalPosition);

	if (_down >= 0 && !_previewShown) {
		return;
	}

	auto sel = -1;
	auto maxSel = 0;
	if (!_srows->empty()) {
		const auto padding = st::stickerPanPadding;
		const auto row = (mouse.y() >= padding)
			? (mouse.y() - padding) / st::stickerPanSize.height()
			: -1;
		const auto column = (mouse.x() >= padding)
			? (mouse.x() - padding) / st::stickerPanSize.width()
			: -1;
		if (row >= 0 && column >= 0) {
			sel = row * _stickersPerRow + column;
		}
		maxSel = _srows->size();
		_overDelete = false;
	} else {
		sel = mouse.y() / st::mentionHeight;
		maxSel = rowsCount();
		_overDelete = (!_hrows->empty() || isRemovableMentionRow(sel))
			? (mouse.x() >= width() - st::mentionHeight)
			: false;
	}
	if (sel < 0 || sel >= maxSel) {
		sel = -1;
	}
	if (sel != _sel) {
		setSel(sel);
		if (_down >= 0 && _sel >= 0 && _down != _sel) {
			_down = _sel;
			showPreview();
		}
	}
}

void FieldAutocomplete::Inner::onParentGeometryChanged() {
	const auto globalPosition = QCursor::pos();
	if (rect().contains(mapFromGlobal(globalPosition))) {
		setMouseTracking(true);
		if (_mouseSelection) {
			selectByMouse(globalPosition);
		}
	}
}

void FieldAutocomplete::Inner::showPreview() {
	if (_down >= 0 && _down < _srows->size()) {
		const auto document = (*_srows)[_down].document;
		_show->showMediaPreview(document->stickerSetOrigin(), document);
		_previewShown = true;
	}
}

void FieldAutocomplete::Inner::setSendMenuDetails(
		Fn<SendMenu::Details()> &&callback) {
	_sendMenuDetails = std::move(callback);
}

auto FieldAutocomplete::Inner::mentionChosen() const
-> rpl::producer<FieldAutocomplete::MentionChosen> {
	return _mentionChosen.events();
}

auto FieldAutocomplete::Inner::hashtagChosen() const
-> rpl::producer<FieldAutocomplete::HashtagChosen> {
	return _hashtagChosen.events();
}

auto FieldAutocomplete::Inner::botCommandChosen() const
-> rpl::producer<FieldAutocomplete::BotCommandChosen> {
	return _botCommandChosen.events();
}

auto FieldAutocomplete::Inner::stickerChosen() const
-> rpl::producer<FieldAutocomplete::StickerChosen> {
	return _stickerChosen.events();
}

auto FieldAutocomplete::Inner::scrollToRequested() const
-> rpl::producer<ScrollTo> {
	return _scrollToRequested.events();
}

rpl::producer<QRect> FieldAutocomplete::Inner::ephemeralIconHovered() const {
	return _ephemeralIconHovered.events();
}

void InitFieldAutocomplete(
		std::unique_ptr<FieldAutocomplete> &autocomplete,
		FieldAutocompleteDescriptor &&descriptor) {
	Expects(!autocomplete);

	autocomplete = std::make_unique<FieldAutocomplete>(
		descriptor.parent,
		descriptor.show,
		descriptor.stOverride);
	const auto raw = autocomplete.get();
	const auto field = descriptor.field;

	field->rawTextEdit()->installEventFilter(raw);

	raw->mentionChosen(
	) | rpl::on_next([=](FieldAutocomplete::MentionChosen data) {
		const auto user = data.user;
		const auto ctrlClick = base::IsCtrlPressed()
			&& data.method == FieldAutocomplete::ChooseMethod::ByClick;
		if (data.mention.isEmpty() || ctrlClick) {
			field->insertTag(
				user->firstName.isEmpty() ? user->name() : user->firstName,
				PrepareMentionTag(user));
		} else {
			field->insertTag('@' + data.mention);
		}
	}, raw->lifetime());

	const auto sendCommand = descriptor.sendBotCommand;
	const auto setText = descriptor.setText;

	raw->hashtagChosen(
	) | rpl::on_next([=](FieldAutocomplete::HashtagChosen data) {
		field->insertTag(data.hashtag);
	}, raw->lifetime());

	const auto peer = descriptor.peer;
	const auto features = descriptor.features;
	const auto processShortcut = descriptor.processShortcut;
	const auto shortcutMessages = (processShortcut != nullptr)
		? &peer->owner().shortcutMessages()
		: nullptr;
	raw->botCommandChosen(
	) | rpl::on_next([=](FieldAutocomplete::BotCommandChosen data) {
		if (!features().autocompleteCommands) {
			return;
		}
		using Method = FieldAutocompleteChooseMethod;
		const auto byTab = (data.method == Method::ByTab);
		const auto shortcut = data.user->isSelf();

		// Send bot command at once, if it was not inserted by pressing Tab.
		if (byTab && data.command.size() > 1) {
			field->insertTag(data.command);
		} else if (!shortcut) {
			sendCommand(data.command);
			setText(
				field->getTextWithTagsPart(field->textCursor().position()));
		} else if (processShortcut) {
			processShortcut(data.command.mid(1));
		}
	}, raw->lifetime());

	raw->setModerateKeyActivateCallback(
		std::move(descriptor.moderateKeyActivateCallback));

	if (const auto stickerChoosing = descriptor.stickerChoosing) {
		raw->choosingProcesses(
		) | rpl::on_next([=](FieldAutocomplete::Type type) {
			if (type == FieldAutocomplete::Type::Stickers) {
				stickerChoosing();
			}
		}, raw->lifetime());
	}
	if (const auto chosen = descriptor.stickerChosen) {
		raw->stickerChosen(
		) | rpl::on_next(chosen, raw->lifetime());
	}

	field->tabbed(
	) | rpl::on_next([=](not_null<Ui::InputField::TabbedRequest*> request) {
		if (!raw->isHidden()) {
			raw->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
			request->handled = true;
		}
	}, raw->lifetime());

	const auto check = [=] {
		auto parsed = ParseMentionHashtagBotCommandQuery(field, features());
		if (parsed.query.isEmpty()) {
		} else if (parsed.query[0] == '#'
			&& cRecentWriteHashtags().isEmpty()
			&& cRecentSearchHashtags().isEmpty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (parsed.query[0] == '@'
			&& peer->session().recentInlineBots().list().empty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (parsed.query[0] == '/'
			&& peer->isUser()
			&& !peer->asUser()->isBot()
			&& (!shortcutMessages
				|| shortcutMessages->shortcuts().list.empty()
				|| peer->starsPerMessageChecked() != 0)) {
			parsed = {};
		}
		if (!parsed.query.isEmpty() && parsed.query[0] == '@') {
			peer->session().topGuestChatBots().reload();
		}
		raw->showFiltered(peer, parsed.query, parsed.fromStart);
	};

	const auto updateStickersByEmoji = [=] {
		const auto errorForStickers = Data::RestrictionError(
			peer,
			ChatRestriction::SendStickers);
		if (features().suggestStickersByEmoji && !errorForStickers) {
			const auto &text = field->getTextWithTags().text;
			auto length = 0;
			if (const auto emoji = Ui::Emoji::Find(text, &length)) {
				if (text.size() <= length) {
					raw->showStickers(emoji);
					return;
				}
			}
		}
		raw->showStickers(nullptr);
	};

	raw->refreshRequests(
	) | rpl::on_next(check, raw->lifetime());

	raw->stickersUpdateRequests(
	) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	peer->owner().botCommandsChanges(
	) | rpl::filter([=](not_null<PeerData*> changed) {
		return (peer == changed);
	}) | rpl::on_next([=] {
		if (raw->clearFilteredBotCommands()) {
			check();
		}
	}, raw->lifetime());

	peer->owner().stickers().updated(
		Data::StickersType::Stickers
	) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	QObject::connect(
		field->rawTextEdit(),
		&QTextEdit::cursorPositionChanged,
		raw,
		check,
		Qt::QueuedConnection);

	field->changes(
	) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	peer->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer == peer);
	}) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	if (shortcutMessages) {
		shortcutMessages->shortcutsChanged(
		) | rpl::on_next(check, raw->lifetime());
	}

	raw->setSendMenuDetails(std::move(descriptor.sendMenuDetails));
	raw->hideFast();
}

} // namespace ChatHelpers
