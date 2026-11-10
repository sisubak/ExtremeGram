/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_extremegram.h"

#include "settings/settings_common_session.h"

#include "core/enhanced_settings.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/sections/settings_main.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
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

void BuildOnlineSpamInterval(
		SectionBuilder &builder,
		Window::SessionController *controller) {
	const auto key = EnhancedSettings::Option::OnlineSpamInterval;
	const auto formatMs = [](int value) {
		return tr::lng_extremegram_online_spam_interval_ms(
			tr::now,
			lt_amount,
			QString::number(value));
	};
	const auto label = [formatMs](int value) {
		return formatMs(value);
	};
	const auto valueLabel = builder.container()->lifetime(
	).make_state<rpl::event_stream<QString>>();
	const auto button = builder.addButton({
		.id = u"extremegram/online-spam-interval"_q,
		.title = tr::lng_extremegram_online_spam_interval(),
		.st = &st::settingsButtonNoIcon,
		.label = valueLabel->events_starting_with(
			label(EnhancedSettings::Get(key))),
		.keywords = { u"online"_q, u"ms"_q, u"interval"_q },
	});
	if (!button || !controller) {
		return;
	}
	const auto slider = builder.container()->add(
		object_ptr<Ui::MediaSliderWheelless>(
			builder.container(),
			st::settingsScale),
		st::settingsBigScalePadding);
	slider->resize(slider->width(), st::settingsScale.seekSize.height());
	slider->setAccessibleName(EnhancedSettings::OptionTitle(key.id));

	constexpr auto kMsStep = 100;
	constexpr auto kMaximumMs = 600000;
	const auto sections = kMaximumMs / kMsStep;
	slider->setPseudoDiscrete(
		sections + 1,
		[](int index) { return index; },
		EnhancedSettings::Get(key) / kMsStep,
		[=](int index) { valueLabel->fire(label(index * kMsStep)); },
		[=](int index) {
			EnhancedSettings::ApplyOption(
				not_null<Window::SessionController*>(controller),
				key,
				index * kMsStep);
		});
	EnhancedSettings::Changes(key) | rpl::on_next([=] {
		const auto value = EnhancedSettings::Get(key);
		slider->setValue(value / float64(kMaximumMs));
		valueLabel->fire(label(value));
	}, builder.container()->lifetime());
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
	BuildOnlineSpamInterval(builder, controller);
	builder.addDividerText(
		rpl::single(tr::lng_extremegram_online_spam_about(tr::now)));

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
	builder.addDividerText(tr::lng_extremegram_bot_mode_about());
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/mute-dialog"_q,
		tr::lng_extremegram_mute_dialog(),
		EnhancedSettings::Option::ExtremeMuteDialog,
		{ u"mute"_q, u"restrict"_q, u"reason"_q, u"duration"_q });
	builder.addDividerText(tr::lng_extremegram_mute_dialog_about());
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/markdown-paste"_q,
		tr::lng_extremegram_markdown_paste(),
		EnhancedSettings::Option::MarkdownPaste,
		{ u"markdown"_q, u"paste"_q, u"formatting"_q });
	builder.addDividerText(tr::lng_extremegram_markdown_paste_about());
	BuildExtremeGramToggle(
		builder,
		controller,
		u"extremegram/mention-as-name"_q,
		tr::lng_extremegram_mention_as_name(),
		EnhancedSettings::Option::MentionAsName,
		{ u"mention"_q, u"username"_q, u"name"_q });
	builder.addDividerText(tr::lng_extremegram_mention_as_name_about());

	builder.addButton({
		.id = u"extremegram/redesign"_q,
		.title = tr::lng_extremegram_redesign(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			if (!controller) {
				return;
			}
			controller->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::lng_extremegram_redesign());
				const auto group = std::make_shared<Ui::RadioenumGroup<int>>(
					EnhancedSettings::Get(EnhancedSettings::Option::RedesignMode));
				const auto addMode = [&](int value, tr::phrase<> label) {
					box->verticalLayout()->add(
						object_ptr<Ui::Radioenum<int>>(
							box->verticalLayout(),
							group,
							value,
							label(tr::now),
							st::settingsSendType),
						st::settingsSendTypePadding);
				};
				addMode(
					0,
					tr::lng_extremegram_redesign_classic);
				addMode(
					1,
					tr::lng_extremegram_redesign_modern);
				addMode(
					2,
					tr::lng_extremegram_redesign_ultra);
				group->setChangedCallback([=, raw = group.get()](int value) {
					EnhancedSettings::ApplyOption(
						not_null<Window::SessionController*>(controller),
						EnhancedSettings::Option::RedesignMode,
						value);
					raw->setValue(value);
					box->closeBox();
				});
				box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
			}));
		},
		.keywords = { u"redesign"_q, u"design"_q, u"theme"_q, u"colors"_q },
	});
	builder.addDividerText(tr::lng_extremegram_redesign_about());

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
