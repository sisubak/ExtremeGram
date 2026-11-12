/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/unread_badge.h"

namespace Ui {
class ChatStyle;
} // namespace Ui

class ParticipantsBoxController;

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Info {
namespace Profile {

class MemberListRow final : public PeerListRow {
public:
	enum class Rights {
		Normal,
		Admin,
		Creator,
	};
	struct Type {
		Rights rights;
		QString rank;
		QString removeText;
		not_null<const Ui::ChatStyle*> chatStyle;
		not_null<base::flat_map<QRgb, QImage>*> circleCache;
		bool canAddTag = false;
		bool canEditTag = false;
		bool canRemove = false;
		bool restricted = false;
		TimeId restrictedUntil = 0;
	};

	static constexpr auto kTagElement = 1;
	static constexpr auto kRemoveElement = 2;

	MemberListRow(not_null<PeerData*> peer, Type type);
	~MemberListRow();

	void setType(Type type);
	[[nodiscard]] Type type() const;
	void setRefreshCallback(Fn<void()> callback);
	void refreshStatus() override;

	[[nodiscard]] UserData *user() const;

	[[nodiscard]] bool clientBadgeAt(QPoint point) const override;

	int elementsCount() const override;
	QRect elementGeometry(int element, int outerWidth) const override;
	bool elementDisabled(int element) const override;
	bool elementOnlySelect(int element) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	int paintNameIconGetLeadingWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int outerWidth,
		bool selected) override;
	int paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;
	bool elementsAnimating() const override;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;

private:
	enum class TagMode {
		None,
		AdminPill,
		NormalText,
		AddTag,
	};

	[[nodiscard]] bool tagInteractive() const;
	[[nodiscard]] int pillHeight() const;
	[[nodiscard]] const QImage &ensurePillCircle(const QColor &color) const;
	void paintPill(
		Painter &p,
		int x,
		int y,
		int width,
		const QColor &bgColor) const;
	void paintColoredPill(
		Painter &p,
		int x,
		int y,
		int w,
		int textWidth,
		const QString &text,
		const QColor &color,
		bool over,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth);
	void paintTag(
		Painter &p,
		QRect geometry,
		int outerWidth,
		bool over);
	void paintRemove(
		Painter &p,
		QRect geometry,
		int outerWidth,
		bool over);
	[[nodiscard]] QSize tagSize() const;
	[[nodiscard]] QSize removeSize() const;
	void checkHoverChanged(bool hovered);

	Type _type;
	TagMode _tagMode = TagMode::None;
	QString _tagText;
	int _tagTextWidth = 0;
	QString _removeText;
	int _removeTextWidth = 0;
	Ui::Animations::Simple _hoverAnimation;
	std::unique_ptr<Ui::RippleAnimation> _tagRipple;
	std::unique_ptr<Ui::RippleAnimation> _removeRipple;
	Fn<void()> _refreshCallback;
	base::Timer _restrictionTimer;
	QRect _clientBadgeRect;
	bool _wasHovered = false;

};

std::unique_ptr<ParticipantsBoxController> CreateMembersController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
