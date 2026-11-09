/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/redesign.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/enhanced_settings.h"
#include "custom/smooth_animations.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "styles/palette.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_session_controller.h"

#include <optional>

namespace Custom {
namespace {

using Window::Theme::EmbeddedType;

constexpr auto kSwitchDurationMs = 320;

// The GitHub Actions build is the only compile check, so this module reuses
// only APIs that are already proven by existing callers: the embedded theme
// accent machinery (settings_chat.cpp "apply()") and the Animations::Simple
// idiom (bot_mode_manager.cpp).

[[nodiscard]] std::optional<QColor> ModeAccent(int mode) {
	switch (mode) {
	case 1:
		return QColor(139, 92, 246);
	case 2:
		return QColor(255, 94, 87);
	default:
		return std::nullopt;
	}
}

void AnimateWindowSwitch(
		not_null<::MainWindow*> target,
		Fn<void()> apply) {
	const auto from = target->grab().toImage();
	apply();
	const auto to = target->grab().toImage();
	if (from.isNull() || to.isNull() || from.size() != to.size()) {
		target->update();
		return;
	}
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(target);
	overlay->setGeometry(target->rect());
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->show();
	overlay->raise();

	struct Frame {
		QImage oldShot;
		QImage newShot;
		Ui::Animations::Simple anim;
	};
	const auto frame = overlay->lifetime().make_state<Frame>(
		Frame{ .oldShot = from, .newShot = to });
	frame->anim.start([=] {
		overlay->update();
		if (!frame->anim.animating()) {
			overlay->deleteLater();
		}
	}, 0.0, 1.0, kSwitchDurationMs, Animations::CubicSmoothOut);
	overlay->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(overlay);
		p.drawImage(overlay->rect(), frame->newShot);
		p.setOpacity(1.0 - frame->anim.value(1.0));
		p.drawImage(overlay->rect(), frame->oldShot);
		p.setOpacity(1.0);
	}, overlay->lifetime());
}

} // namespace

void ApplyRedesignDesign(not_null<Window::SessionController*> controller) {
	const auto accent = ModeAccent(EnhancedSettings::Get(
		EnhancedSettings::Option::RedesignMode));
	auto &settings = Core::App().settings();
	auto &colors = settings.themesAccentColors();
	for (const auto type : {
		EmbeddedType::DayBlue,
		EmbeddedType::Default,
		EmbeddedType::Night,
		EmbeddedType::NightGreen,
	}) {
		if (accent) {
			colors.set(type, *accent);
		} else {
			colors.clear(type);
		}
	}
	Local::writeSettings();
	if (Window::Theme::Background()->editingTheme()) {
		return;
	}
	const auto current = Window::Theme::CurrentEmbeddedType();
	if (!current) {
		return;
	}
	for (const auto &scheme : Window::Theme::EmbeddedThemes()) {
		if (scheme.type != *current) {
			continue;
		}
		const auto applySwitch = [path = scheme.path] {
			Window::Theme::ApplyDefaultWithPath(path);
			Window::Theme::KeepApplied();
		};
		const auto target = controller->widget();
		if (EnhancedSettings::Get(
				EnhancedSettings::Option::SmoothInterfaceAnimations)) {
			AnimateWindowSwitch(target, applySwitch);
		} else {
			applySwitch();
			target->update();
		}
		break;
	}
}

} // namespace Custom
