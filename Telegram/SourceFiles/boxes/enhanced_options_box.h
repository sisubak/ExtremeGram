#pragma once
/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
	class RadiobuttonGroup;

	class Radiobutton;

	class Checkbox;

	class FlatLabel;

	class InputField;

	class MediaSliderWheelless;
} // namespace Ui

class ExtraContextMenuBox : public Ui::BoxContent {
public:
	ExtraContextMenuBox(QWidget *parent);

protected:
	void prepare() override;

};

class AlwaysDeleteBox : public Ui::BoxContent {
public:
	AlwaysDeleteBox(QWidget *parent);

	static QString DeleteLabel(int option);

protected:
	void prepare() override;

private:
	void save();

	object_ptr<Ui::FlatLabel> _description = {nullptr};
	std::shared_ptr<Ui::RadiobuttonGroup> _optionGroup;

};

class RadioController : public Ui::BoxContent {
public:
	RadioController(QWidget *parent);

protected:
	void prepare() override;

	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void save();

	object_ptr<Ui::InputField> _url = {nullptr};

};

class BitrateController : public Ui::BoxContent {
public:
	BitrateController(QWidget *parent);

	static QString BitrateLabel(int boost);

protected:
	void prepare() override;

private:
	void save();

	object_ptr<Ui::FlatLabel> _description = {nullptr};
	std::shared_ptr<Ui::RadiobuttonGroup> _bitrateGroup;

};

class RichMessagePreviewBlocksBox : public Ui::BoxContent {
public:
	RichMessagePreviewBlocksBox(QWidget *parent);

protected:
	void prepare() override;

private:
	void updateCurrentLabel();
	void save();

	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _current = { nullptr };
	object_ptr<Ui::MediaSliderWheelless> _slider = { nullptr };
	int _labelsTop = 0;
	int _limit = 0;

};
