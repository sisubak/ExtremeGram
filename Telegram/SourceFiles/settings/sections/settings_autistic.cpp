/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_autistic.h"

#include "core/enhanced_settings.h"
#include "custom/autistic/autistic_mode.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "settings/sections/settings_main.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

using EnhancedSettings::Option;

void BuildAutisticSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto addToggle = [&](
			QString id,
			rpl::producer<QString> title,
			EnhancedSettings::Key<bool> key,
			QStringList keywords,
			rpl::producer<QString> about) {
		const auto button = builder.addButton({
			.id = std::move(id),
			.title = std::move(title),
			.st = &st::settingsButtonNoIcon,
			.toggled = EnhancedSettings::Watch(key),
			.keywords = std::move(keywords),
		});
		if (button && controller) {
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
		builder.addDividerText(std::move(about));
	};

	const auto masterButton = builder.addButton({
		.id = u"autistic/telegram"_q,
		.title = tr::lng_extremegram_autistic_telegram(),
		.st = &st::settingsButtonNoIcon,
		.toggled = EnhancedSettings::Watch(
			EnhancedSettings::Option::AutisticTelegram),
		.keywords = { u"autistic"_q, u"режим"_q },
	});
	if (masterButton && controller) {
		masterButton->toggledValue(
		) | rpl::filter([=](bool value) {
			return value != EnhancedSettings::Get(
				EnhancedSettings::Option::AutisticTelegram);
		}) | rpl::on_next([=](bool) {
			Autistic::ToggleMaster(
				not_null<Window::SessionController*>(controller));
		}, masterButton->lifetime());
	}
	builder.addDividerText(
		tr::lng_extremegram_autistic_telegram_about());
	addToggle(
		u"autistic/commands"_q,
		tr::lng_extremegram_autistic_commands(),
		EnhancedSettings::Option::AutisticCommands,
		{ u"commands"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_commands_about());
	addToggle(
		u"autistic/ignore"_q,
		tr::lng_extremegram_autistic_ignore(),
		EnhancedSettings::Option::AutisticIgnore,
		{ u"ignore"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_ignore_about());
	addToggle(
		u"autistic/anti-ping"_q,
		tr::lng_extremegram_autistic_anti_ping(),
		EnhancedSettings::Option::AutisticAntiPing,
		{ u"ping"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_anti_ping_about());
	addToggle(
		u"autistic/read-invisible"_q,
		tr::lng_extremegram_autistic_read_invisible(),
		EnhancedSettings::Option::AutisticReadInvisible,
		{ u"read"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_read_invisible_about());
	addToggle(
		u"autistic/ghosts"_q,
		tr::lng_extremegram_autistic_ghosts(),
		EnhancedSettings::Option::AutisticGhosts,
		{ u"ghost"_q, u"deleted"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_ghosts_about());
	addToggle(
		u"autistic/show-orig"_q,
		tr::lng_extremegram_autistic_show_orig(),
		EnhancedSettings::Option::AutisticShowOrig,
		{ u"edit"_q, u"orig"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_show_orig_about());
	addToggle(
		u"autistic/layout-fix"_q,
		tr::lng_extremegram_autistic_layout_fix(),
		EnhancedSettings::Option::AutisticLayoutFix,
		{ u"layout"_q, u"keyboard"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_layout_fix_about());
	addToggle(
		u"autistic/heart"_q,
		tr::lng_extremegram_autistic_heart(),
		EnhancedSettings::Option::AutisticHeart,
		{ u"heart"_q, u"reaction"_q, u"autistic"_q },
		tr::lng_extremegram_autistic_heart_about());

	builder.addButton({
		.id = u"autistic/hide-foreign"_q,
		.title = tr::lng_extremegram_autistic_hide_foreign(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			if (!controller) {
				return;
			}
			Autistic::CycleHideForeign();
			controller->showToast(
				QString::fromUtf8("Скрытие чужих: ")
				+ Autistic::HideForeignLabel(Autistic::hideForeign()));
		},
		.keywords = { u"hide"_q, u"foreign"_q, u"autistic"_q },
	});
	builder.addDividerText(
		tr::lng_extremegram_autistic_hide_foreign_about());
}

class AutisticGramm final : public Section<AutisticGramm> {
public:
	AutisticGramm(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

const auto kMeta = BuildHelper({
	.id = AutisticGramm::Id(),
	.parentId = MainId(),
	.title = &tr::lng_extremegram_autistic_title,
	.icon = &st::menuIconManage,
}, BuildAutisticSection);

const SectionBuildMethod kAutisticSection = kMeta.build;

AutisticGramm::AutisticGramm(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> AutisticGramm::title() {
	return tr::lng_extremegram_autistic_title();
}

void AutisticGramm::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kAutisticSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type AutisticId() {
	return AutisticGramm::Id();
}

} // namespace Settings
