/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "custom/nickname_gradient.h"

#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtGui/QMouseEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Custom {
namespace {

constexpr int kMinColors = 2;
constexpr int kMaxColors = 6;
constexpr int kMagic = 0xE7;
constexpr int kVersion = 1;
constexpr int kBitsPerSymbol = 3;
constexpr int kSymbolsInAlphabet = 8;
constexpr auto kInvisibleSymbols = std::array<ushort, kSymbolsInAlphabet>{
	0x200B,
	0x200C,
	0x200D,
	0x2060,
	0x2061,
	0x2062,
	0x2063,
	0x2064,
};
constexpr auto kLegacyMarker = "\xE2\x80\x8B" "EG";
constexpr int kLegacyEncodedLength = 13;
constexpr std::array<float64, 3> kSpeedPeriods = { 14.0, 8.0, 4.0 };

[[nodiscard]] int ToNibble(int channel) {
	return std::clamp((channel + 8) / 17, 0, 15);
}

[[nodiscard]] int FromNibble(int value) {
	return value * 17;
}

[[nodiscard]] uint8 Crc8(const std::vector<uint8> &bytes) {
	auto crc = uint8(0);
	for (const auto byte : bytes) {
		crc ^= byte;
		for (auto i = 0; i != 8; ++i) {
			crc = (crc & 0x80)
				? uint8((crc << 1) ^ 0x07)
				: uint8(crc << 1);
		}
	}
	return crc;
}

[[nodiscard]] std::vector<uint8> PackBits(
		const std::vector<bool> &bits) {
	auto result = std::vector<uint8>();
	result.reserve((bits.size() + 7) / 8);
	auto value = uint8(0);
	auto count = 0;
	for (const auto bit : bits) {
		value = uint8((value << 1) | (bit ? 1 : 0));
		if (++count == 8) {
			result.push_back(value);
			value = 0;
			count = 0;
		}
	}
	if (count) {
		result.push_back(uint8(value << (8 - count)));
	}
	return result;
}

[[nodiscard]] int SymbolLength(int count) {
	return (24 + count * 12) / kBitsPerSymbol;
}

struct BitWriter {
	std::vector<bool> bits;

	void write(uint32 value, int count) {
		for (auto i = count - 1; i >= 0; --i) {
			bits.push_back(((value >> i) & 1) != 0);
		}
	}
};

struct BitReader {
	const std::vector<bool> &bits;
	int position = 0;

	[[nodiscard]] bool read(uint32 &value, int count) {
		if (position + count > int(bits.size())) {
			return false;
		}
		value = 0;
		for (auto i = 0; i != count; ++i) {
			value = (value << 1) | (bits[position++] ? 1 : 0);
		}
		return true;
	}
};

[[nodiscard]] std::optional<NicknameGradientConfig> DecodeSymbols(
		const QString &symbols,
		int count) {
	if (symbols.size() != SymbolLength(count)) {
		return std::nullopt;
	}
	auto bits = std::vector<bool>();
	bits.reserve(symbols.size() * kBitsPerSymbol);
	for (const auto ch : symbols) {
		auto value = -1;
		for (auto i = 0; i != kSymbolsInAlphabet; ++i) {
			if (ch.unicode() == kInvisibleSymbols[i]) {
				value = i;
				break;
			}
		}
		if (value < 0) {
			return std::nullopt;
		}
		for (auto i = kBitsPerSymbol - 1; i >= 0; --i) {
			bits.push_back(((value >> i) & 1) != 0);
		}
	}
	auto reader = BitReader{ bits };
	auto magic = uint32(0);
	auto version = uint32(0);
	auto storedCount = uint32(0);
	auto speed = uint32(0);
	if (!reader.read(magic, 8)
		|| !reader.read(version, 3)
		|| !reader.read(storedCount, 3)
		|| !reader.read(speed, 2)
		|| magic != kMagic
		|| version != kVersion
		|| int(storedCount) != count
		|| speed > 2) {
		return std::nullopt;
	}
	auto colors = std::vector<QColor>();
	colors.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto r = uint32(0);
		auto g = uint32(0);
		auto b = uint32(0);
		if (!reader.read(r, 4)
			|| !reader.read(g, 4)
			|| !reader.read(b, 4)) {
			return std::nullopt;
		}
		colors.push_back(QColor(FromNibble(r), FromNibble(g), FromNibble(b)));
	}
	const auto crcPosition = reader.position;
	auto crc = uint32(0);
	if (!reader.read(crc, 8)) {
		return std::nullopt;
	}
	const auto payload = std::vector<bool>(
		bits.begin(),
		bits.begin() + crcPosition);
	if (Crc8(PackBits(payload)) != crc) {
		return std::nullopt;
	}
	return NicknameGradientConfig{ std::move(colors), int(speed) };
}

[[nodiscard]] bool IsInvisibleSymbol(QChar ch) {
	for (auto i = 0; i != kSymbolsInAlphabet; ++i) {
		if (ch.unicode() == kInvisibleSymbols[i]) {
			return true;
		}
	}
	return false;
}

struct InvisibleMarker {
	int position = 0;
	int length = 0;
	NicknameGradientConfig config;
};

[[nodiscard]] std::optional<InvisibleMarker> FindInvisible(
		const QString &about) {
	for (auto i = 0; i != about.size();) {
		if (!IsInvisibleSymbol(about[i])) {
			++i;
			continue;
		}
		auto j = i + 1;
		while (j != about.size() && IsInvisibleSymbol(about[j])) {
			++j;
		}
		const auto run = about.mid(i, j - i);
		for (auto count = kMaxColors; count >= kMinColors; --count) {
			const auto length = SymbolLength(count);
			if (run.size() < length) {
				continue;
			}
			if (auto config = DecodeSymbols(run.right(length), count)) {
				return InvisibleMarker{
					int(i + run.size() - length),
					length,
					std::move(*config),
				};
			}
		}
		i = j;
	}
	return std::nullopt;
}

[[nodiscard]] bool IsHexChar(QChar ch) {
	return ch.isDigit()
		|| (ch >= QChar('a') && ch <= QChar('f'))
		|| (ch >= QChar('A') && ch <= QChar('F'));
}

[[nodiscard]] std::optional<QColor> HexToColor(const QString &hex) {
	if (hex.size() != 6) {
		return std::nullopt;
	}
	for (const auto ch : hex) {
		if (!IsHexChar(ch)) {
			return std::nullopt;
		}
	}
	auto ok = false;
	const auto value = hex.toUInt(&ok, 16);
	if (!ok) {
		return std::nullopt;
	}
	return QColor::fromRgb(value & 0xFFFFFFu);
}

[[nodiscard]] std::optional<NicknameGradientConfig> ParseLegacy(
		const QString &about) {
	const auto marker = QString::fromUtf8(kLegacyMarker);
	const auto index = about.indexOf(marker);
	if (index < 0) {
		return std::nullopt;
	}
	const auto rest = about.mid(index + marker.size());
	if (rest.size() < kLegacyEncodedLength) {
		return std::nullopt;
	}
	const auto c1 = HexToColor(rest.left(6));
	const auto c2 = HexToColor(rest.mid(6, 6));
	const auto speedChar = rest.mid(12, 1);
	if (!c1 || !c2) {
		return std::nullopt;
	}
	const auto speed = (speedChar == u"0"_q)
		? 0
		: (speedChar == u"1"_q)
		? 1
		: (speedChar == u"2"_q)
		? 2
		: -1;
	if (speed < 0) {
		return std::nullopt;
	}
	return NicknameGradientConfig{ { *c1, *c2 }, speed };
}

[[nodiscard]] QString EncodeSymbols(
		const NicknameGradientConfig &config) {
	auto writer = BitWriter();
	writer.write(kMagic, 8);
	writer.write(kVersion, 3);
	writer.write(uint32(config.colors.size()), 3);
	writer.write(uint32(std::clamp(config.speed, 0, 2)), 2);
	for (const auto &color : config.colors) {
		writer.write(uint32(ToNibble(color.red())), 4);
		writer.write(uint32(ToNibble(color.green())), 4);
		writer.write(uint32(ToNibble(color.blue())), 4);
	}
	const auto crc = Crc8(PackBits(writer.bits));
	writer.write(crc, 8);
	auto result = QString();
	result.reserve(writer.bits.size() / kBitsPerSymbol);
	for (auto i = 0; i != int(writer.bits.size()); i += kBitsPerSymbol) {
		auto value = 0;
		for (auto j = 0; j != kBitsPerSymbol; ++j) {
			value = (value << 1) | (writer.bits[i + j] ? 1 : 0);
		}
		result.append(QChar(kInvisibleSymbols[value]));
	}
	return result;
}

[[nodiscard]] const std::vector<QColor> &PresetColors() {
	static const auto value = std::vector<QColor>{
		QColor(0xFF, 0x3B, 0x30),
		QColor(0xFF, 0x95, 0x00),
		QColor(0xFF, 0xD6, 0x00),
		QColor(0x34, 0xC7, 0x59),
		QColor(0x00, 0xC7, 0xBE),
		QColor(0x00, 0x9C, 0xFF),
		QColor(0x58, 0x5B, 0xFF),
		QColor(0xAF, 0x52, 0xDE),
		QColor(0xFF, 0x2D, 0x55),
		QColor(0xFF, 0x64, 0x95),
		QColor(0x8B, 0x5C, 0xF6),
		QColor(0x2E, 0xCC, 0x71),
		QColor(0xF2, 0x9B, 0x30),
		QColor(0xE0, 0xE0, 0xE0),
		QColor(0x8A, 0x8A, 0x8A),
		QColor(0x3A, 0x3A, 0x3A),
	};
	return value;
}

class Swatch final : public Ui::RpWidget {
public:
	Swatch(QWidget *parent, QColor color, int size, Fn<void()> clicked)
	: Ui::RpWidget(parent)
	, _color(color)
	, _clicked(std::move(clicked)) {
		resize(size, size);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(this);
			p.setRenderHint(QPainter::Antialiasing, true);
			p.setPen(QPen(QColor(0, 0, 0, 40), 1.0));
			p.setBrush(_color);
			p.drawRoundedRect(rect(), 5, 5);
		}, lifetime());
	}

	void setColor(const QColor &color) {
		if (_color != color) {
			_color = color;
			update();
		}
	}

	[[nodiscard]] const QColor &color() const {
		return _color;
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_clicked();
		}
	}

private:
	QColor _color;
	Fn<void()> _clicked;
};

class RemoveButton final : public Ui::RpWidget {
public:
	RemoveButton(QWidget *parent, Fn<void()> clicked)
	: Ui::RpWidget(parent)
	, _clicked(std::move(clicked)) {
		resize(24, 24);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(this);
			auto pen = QPen(QColor(0x88, 0x88, 0x88));
			pen.setWidthF(1.6);
			p.setPen(pen);
			p.setRenderHint(QPainter::Antialiasing, true);
			p.drawLine(8, 8, 16, 16);
			p.drawLine(16, 8, 8, 16);
		}, lifetime());
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_clicked();
		}
	}

private:
	Fn<void()> _clicked;
};

} // namespace

std::optional<NicknameGradientConfig> ParseBioGradient(
		const QString &about) {
	if (auto marker = FindInvisible(about)) {
		return std::move(marker->config);
	}
	return ParseLegacy(about);
}

QString EncodeBioGradient(const NicknameGradientConfig &config) {
	if (config.colors.size() < kMinColors) {
		return QString();
	}
	auto copy = config;
	if (copy.colors.size() > kMaxColors) {
		copy.colors.resize(kMaxColors);
	}
	return EncodeSymbols(copy);
}

int GradientPeriodMs(int speed) {
	return int(kSpeedPeriods[std::clamp(speed, 0, 2)] * 1000.0);
}

float64 GradientPhase(int speed) {
	const auto ms = GradientPeriodMs(speed);
	return ms ? std::fmod((crl::now() % ms) / float64(ms), 1.0) : 0.;
}

QString StripBioGradient(const QString &about) {
	auto result = about;
	const auto marker = QString::fromUtf8(kLegacyMarker);
	while (true) {
		auto changed = false;
		if (const auto found = FindInvisible(result)) {
			result.remove(found->position, found->length);
			changed = true;
		}
		const auto index = result.lastIndexOf(marker);
		if (index >= 0) {
			result = result.left(index);
			changed = true;
		}
		while (result.endsWith(' ')) {
			result.chop(1);
		}
		if (!changed) {
			break;
		}
	}
	return result;
}

NicknameGradient::NicknameGradient(not_null<Main::Session*> session)
: _session(session)
, _tickTimer([=] { onTick(); }) {
	_session->changes().peerUpdates(
		Data::PeerUpdate::Flag::About
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		const auto user = update.peer->asUser();
		if (!user) {
			return;
		}
		auto &entry = _cache[user->id];
		entry.about = user->about();
		entry.config = ParseBioGradient(entry.about);
		repaintPeer(user->id);
		fireChanged();
	}, _lifetime);
}

std::optional<NicknameGradientConfig> NicknameGradient::configFor(
		not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	if (!user || user->isBot() || user->isServiceUser()) {
		return std::nullopt;
	}
	auto &entry = _cache[user->id];
	const auto &about = user->about();
	if (entry.about != about) {
		entry.about = about;
		entry.config = ParseBioGradient(about);
	}
	if (entry.config) {
		ensureTimer();
	} else if (about.isEmpty()
		&& crl::now() - entry.lastFullRequest > 30 * crl::time(1000)) {
		entry.lastFullRequest = crl::now();
		user->updateFull();
	}
	return entry.config;
}

QPen NicknameGradient::penFor(
		const NicknameGradientConfig &config,
		float64 width) const {
	const auto count = int(config.colors.size());
	if (count < kMinColors) {
		return QPen();
	}
	const auto offset = GradientPhase(config.speed) * width * 2.0;
	auto gradient = QLinearGradient(
		QPointF(-offset, 0.0),
		QPointF(width * 2.0 - offset, 0.0));
	gradient.setSpread(QGradient::RepeatSpread);
	for (auto i = 0; i != count; ++i) {
		gradient.setColorAt(double(i) / count, config.colors[i]);
	}
	gradient.setColorAt(1.0, config.colors[0]);
	auto pen = QPen(QBrush(gradient), 1.0);
	pen.setCosmetic(true);
	return pen;
}

rpl::producer<> NicknameGradient::ticks() const {
	return _tickEvents.events();
}

void NicknameGradient::repaintPeer(PeerId peerId) {
	const auto history = _session->data().historyLoaded(peerId);
	if (!history) {
		return;
	}
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			_session->data().requestItemRepaint(view->data());
		}
	}
}

void NicknameGradient::ensureTimer() {
	_lastRequest = crl::now();
	if (!_tickTimer.isActive()) {
		_tickTimer.callEach(50);
	}
}

void NicknameGradient::onTick() {
	fireChanged();
	if (crl::now() - _lastRequest > 500) {
		_tickTimer.cancel();
	}
}

void NicknameGradient::fireChanged() {
	if (_firing) {
		return;
	}
	_firing = true;
	_tickEvents.fire({});
	_firing = false;
}

void ApplyNicknameGradient(
		not_null<Main::Session*> session,
		std::optional<NicknameGradientConfig> config,
		Fn<void(bool ok, QString message)> done) {
	const auto user = session->user();
	auto updated = StripBioGradient(user->about());
	if (config && config->colors.size() >= kMinColors) {
		if (!updated.isEmpty()) {
			updated += ' ';
		}
		updated += EncodeBioGradient(*config);
	}
	const auto limit = user->isPremium() ? 140 : 70;
	if (updated.size() > limit) {
		if (done) {
			done(
				false,
				tr::lng_extremegram_nickname_gradient_too_long(tr::now));
		}
		return;
	}
	if (updated != user->about()) {
		session->api().saveSelfBio(updated);
	}
	if (done) {
		done(true, tr::lng_box_done(tr::now));
	}
}

NicknameGradient &NicknameGradientFor(not_null<Main::Session*> session) {
	static base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<NicknameGradient>> map;
	auto &holder = map[session];
	if (!holder) {
		holder = std::make_unique<NicknameGradient>(session);
		session->lifetime().add([session] {
			map.remove(session);
		});
	}
	return *holder;
}

void ShowNicknameGradientBox(
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto user = session->user();
	auto current = ParseBioGradient(user->about());
	const auto colors = std::make_shared<std::vector<QColor>>(
		(current && current->colors.size() >= kMinColors)
			? current->colors
			: std::vector<QColor>{
				QColor(0x00, 0xC7, 0xBE),
				QColor(0xFF, 0x2D, 0x55) });
	const auto speed = std::make_shared<int>(
		current ? current->speed : 1);

	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extremegram_nickname_gradient());
		const auto layout = box->verticalLayout();

		const auto preview = layout->add(
			object_ptr<Ui::RpWidget>(layout),
			QMargins(
				st::boxPadding.left(),
				8,
				st::boxPadding.right(),
				0));
		preview->setFixedHeight(18);
		preview->paintRequest(
		) | rpl::on_next([=] {
			const auto count = int(colors->size());
			if (count < kMinColors) {
				return;
			}
			auto p = Painter(preview);
			p.setPen(Qt::NoPen);
			const auto width = preview->width();
			const auto ms = int(
				kSpeedPeriods[std::clamp(*speed, 0, 2)] * 1000.0);
			const auto phase = std::fmod(
				(crl::now() % ms) / float64(ms),
				1.0);
			const auto offset = phase * width * 2.0;
			auto gradient = QLinearGradient(
				QPointF(-offset, 0.0),
				QPointF(width * 2.0 - offset, 0.0));
			gradient.setSpread(QGradient::RepeatSpread);
			for (auto i = 0; i != count; ++i) {
				gradient.setColorAt(
					double(i) / count,
					(*colors)[i]);
			}
			gradient.setColorAt(1.0, (*colors)[0]);
			p.setBrush(gradient);
			p.drawRoundedRect(preview->rect(), 6, 6);
		}, preview->lifetime());

		const auto previewTimer = std::make_shared<base::Timer>([=] {
			preview->update();
		});
		previewTimer->callEach(50);
		preview->lifetime().add([previewTimer] {
			previewTimer->cancel();
		});

		auto colorsWrap = object_ptr<Ui::VerticalLayout>(layout);
		const auto colorsList = colorsWrap.data();
		layout->add(std::move(colorsWrap), QMargins(0, 0, 0, 0));

		const auto add = layout->add(
			object_ptr<Ui::SettingsButton>(
				layout,
				tr::lng_extremegram_nickname_gradient_add_color(),
				st::settingsButtonNoIcon),
			QMargins(
				st::boxPadding.left(),
				4,
				st::boxPadding.right(),
				4));

		const auto rebuild = std::make_shared<Fn<void()>>();
		const auto openEditor = [=](int index) {
			controller->show(Box([=](not_null<Ui::GenericBox*> editorBox) {
				editorBox->setTitle(
					tr::lng_extremegram_nickname_gradient_custom_color());
				const auto editor = editorBox->addRow(object_ptr<ColorEditor>(
					editorBox,
					ColorEditor::Mode::HSL,
					colors->at(index)));
				editorBox->addButton(tr::lng_box_ok(), [=] {
					(*colors)[index] = editor->color();
					(*rebuild)();
					editorBox->closeBox();
				});
				editorBox->addButton(tr::lng_cancel(), [=] {
					editorBox->closeBox();
				});
				editorBox->setFocusCallback([=] {
					editor->setInnerFocus();
				});
			}));
		};

		*rebuild = [=] {
			colorsList->clear();
			for (auto i = 0; i != int(colors->size()); ++i) {
				const auto row = Ui::CreateChild<Ui::RpWidget>(colorsList);
				row->resize(320, 76);
				const auto currentSwatch = Ui::CreateChild<Swatch>(
					row,
					(*colors)[i],
					44,
					[=] { openEditor(i); });
				currentSwatch->moveToLeft(0, 16);
				const auto &presets = PresetColors();
				for (auto j = 0; j != int(presets.size()); ++j) {
					const auto column = j % 8;
					const auto line = j / 8;
					const auto swatch = Ui::CreateChild<Swatch>(
						row,
						presets[j],
						24,
						[=, color = presets[j]] {
							(*colors)[i] = color;
							currentSwatch->setColor(color);
							preview->update();
						});
					swatch->moveToLeft(52 + column * 28, 6 + line * 28);
				}
				if (colors->size() > kMinColors) {
					const auto remove = Ui::CreateChild<RemoveButton>(
						row,
						[=] {
							colors->erase(colors->begin() + i);
							(*rebuild)();
						});
					remove->moveToLeft(280, 26);
				}
				colorsList->add(
					object_ptr<Ui::RpWidget>::fromRaw(row),
					QMargins(
						st::boxPadding.left(),
						2,
						st::boxPadding.right(),
						2));
			}
			add->setVisible(colors->size() < kMaxColors);
		};
		(*rebuild)();

		add->setClickedCallback([=] {
			if (colors->size() >= kMaxColors) {
				return;
			}
			colors->push_back(
				PresetColors()[colors->size() % PresetColors().size()]);
			(*rebuild)();
		});

		const auto group = std::make_shared<Ui::RadiobuttonGroup>(*speed);
		group->setChangedCallback([=](int value) {
			*speed = value;
		});
		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				tr::lng_extremegram_nickname_gradient_speed(),
				st::defaultFlatLabel),
			QMargins(st::boxPadding.left(), 4, 0, 0));
		for (auto i = 0; i != 3; ++i) {
			const auto text = (i == 0)
				? tr::lng_extremegram_nickname_gradient_slow(tr::now)
				: (i == 1)
				? tr::lng_extremegram_nickname_gradient_medium(tr::now)
				: tr::lng_extremegram_nickname_gradient_fast(tr::now);
			layout->add(
				object_ptr<Ui::Radiobutton>(
					layout,
					group,
					i,
					text,
					st::defaultBoxCheckbox,
					st::defaultRadio),
				QMargins(
					st::boxPadding.left() + st::boxOptionListPadding.left(),
					0,
					st::boxPadding.right(),
					0));
		}

		const auto off = layout->add(
			object_ptr<Ui::SettingsButton>(
				layout,
				tr::lng_extremegram_nickname_gradient_off(),
				st::settingsButtonNoIcon),
			QMargins(
				st::boxPadding.left(),
				12,
				st::boxPadding.right(),
				4));
		off->setClickedCallback([=] {
			ApplyNicknameGradient(
				session,
				std::nullopt,
				[=](bool, QString message) {
					controller->showToast(std::move(message));
				});
			box->closeBox();
		});

		box->addButton(tr::lng_box_ok(), [=] {
			if (colors->size() < kMinColors) {
				return;
			}
			ApplyNicknameGradient(
				session,
				NicknameGradientConfig{ *colors, *speed },
				[=](bool, QString message) {
					controller->showToast(std::move(message));
				});
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Custom
