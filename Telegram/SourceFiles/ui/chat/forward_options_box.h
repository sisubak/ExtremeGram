/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Ui {

class AbstractCheckView;

enum class ForwardGroupingOption {
	GroupAsIs,
	RegroupAll,
	Separate,
};

struct ForwardOptions {
	int sendersCount = 0;
	int captionsCount = 0;
	bool dropNames = false;
	bool dropCaptions = false;
	bool show = true;
};

struct ForwardGroupingOptions {
	bool show = false;
	ForwardGroupingOption option = ForwardGroupingOption::GroupAsIs;
	Fn<void(ForwardGroupingOption)> optionChanged;
};

struct ForwardSpoilerOptions {
	bool show = false;
	bool add = false;
	Fn<void(bool)> addChanged;
};

[[nodiscard]] bool FillForwardOptions(
	Fn<not_null<AbstractCheckView*>(
		rpl::producer<QString> &&,
		bool)> createView,
	ForwardOptions options,
	Fn<void(ForwardOptions)> optionsChanged,
	ForwardSpoilerOptions spoiler,
	ForwardGroupingOptions grouping,
	Fn<void()> addSeparator,
	rpl::lifetime &lifetime);

} // namespace Ui
