/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_extremegram.h"

#include "settings/settings_common_session.h"

#include "core/enhanced_settings.h"
#include "custom/client_badge_detector.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/sections/settings_main.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/ui_utility.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

using namespace Builder;

void BuildExtremeGramToggle(
		SectionBuilder &builder,
		Window::SessionController *controller,
		QString id,
		rpl::producer<QString> title,
		EnhancedSettings::Key<bool> key,
		QStringList keywords) {
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.toggled = EnhancedSettings::Watch(key),
		.keywords = std::move(keywords),
	});
	if (!button || !controller) {
		return;
	}
	button->toggledValue(
	) | rpl::filter([=](bool value) {
		return value != EnhancedSettings::Get(key);
	}) | rpl::on_next([=](bool value) {
		EnhancedSettings::ApplyOption(
			not_null<Window::SessionController*>(controller),
			key,
			value);
	}, button->lifetime());
}

void BuildExtremeGramSection(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"extremegram/interface"_q,
		.title = tr::lng_settings_interface(),
		.keywords = { u"animation"_q, u"interface"_q, u"online"_q },
	});
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/smooth-animations"_q,
		tr::lng_settings_smooth_interface_animations(),
		EnhancedSettings::Option::SmoothInterfaceAnimations,
		{ u"animation"_q, u"smooth"_q });
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/online-spam"_q,
		tr::lng_settings_online_spam(),
		EnhancedSettings::Option::OnlineSpam,
		{ u"online"_q, u"status"_q });

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"extremegram/bot"_q,
		.title = tr::lng_settings_behavior(),
		.keywords = { u"bot"_q, u"mute"_q, u"stats"_q },
	});
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/bot-mode"_q,
		tr::lng_extremegram_bot_mode(),
		EnhancedSettings::Option::BotMode,
		{ u"bot"_q, u"mute"_q, u"stats"_q });
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/auto-stamp-avatar"_q,
		tr::lng_extremegram_auto_stamp_avatar(),
		EnhancedSettings::Option::AutoStampAvatar,
		{ u"avatar"_q, u"badge"_q, u"stamp"_q });

	builder.addButton({
		.id = u"extremegram/reveal-hidden-chats"_q,
		.title = tr::lng_extremegram_reveal_chats(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			const auto count = EnhancedSettings::HiddenChatsCount(
				controller->session());
			EnhancedSettings::RevealHiddenChats(controller->session());
			controller->showToast(count > 0
				? tr::lng_extremegram_revealed(tr::now)
				: QString::fromUtf8("Скрытых групп и каналов сейчас нет."));
		},
		.keywords = { u"hidden"_q, u"chats"_q, u"reveal"_q },
	});
	builder.addButton({
		.id = u"extremegram/stamp-avatar"_q,
		.title = tr::lng_extremegram_stamp_avatar(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			Custom::ClientBadgeFor(
				&controller->session()
			).stampAndReuploadSelfAvatar(
				crl::guard(controller, [=](bool, const QString &msg) {
					controller->showToast(msg);
				}));
		},
		.keywords = { u"avatar"_q, u"badge"_q, u"stamp"_q, u"online"_q },
	});
}

class ExtremeGram : public Section<ExtremeGram> {
public:
	ExtremeGram(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

const auto kMeta = BuildHelper({
	.id = ExtremeGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_extremegram_settings,
	.icon = &st::menuIconManage,
}, BuildExtremeGramSection);

const SectionBuildMethod kExtremeGramSection = kMeta.build;

ExtremeGram::ExtremeGram(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> ExtremeGram::title() {
	return tr::lng_extremegram_settings();
}

void ExtremeGram::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kExtremeGramSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type ExtremeGramId() {
	return ExtremeGram::Id();
}

} // namespace Settings
