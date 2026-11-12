/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Pure std::u16string keyboard-layout converter (QWERTY <-> ЙЦУКЕН),
// no Qt: usable from local unit tests and from send pipelines.
// The mapping is 1:1 — the output length always equals the input length,
// so TextWithTags offsets stay valid.

#include <string>
#include <string_view>

namespace Autistic {

// Single key mapping, English -> Russian. Non-convertible chars unchanged.
[[nodiscard]] inline char16_t LayoutKeyToRu(char16_t c) {
	switch (c) {
	case u'q': return u'\u0439'; // й
	case u'w': return u'\u0446'; // ц
	case u'e': return u'\u0443'; // у
	case u'r': return u'\u043A'; // к
	case u't': return u'\u0435'; // е
	case u'y': return u'\u043D'; // н
	case u'u': return u'\u0433'; // г
	case u'i': return u'\u0448'; // ш
	case u'o': return u'\u0449'; // щ
	case u'p': return u'\u0437'; // з
	case u'[': return u'\u0445'; // х
	case u']': return u'\u044A'; // ъ
	case u'a': return u'\u0444'; // ф
	case u's': return u'\u044B'; // ы
	case u'd': return u'\u0432'; // в
	case u'f': return u'\u0430'; // а
	case u'g': return u'\u043F'; // п
	case u'h': return u'\u0440'; // р
	case u'j': return u'\u043E'; // о
	case u'k': return u'\u043B'; // л
	case u'l': return u'\u0434'; // д
	case u';': return u'\u0436'; // ж
	case u'\'': return u'\u044D'; // э
	case u'z': return u'\u044F'; // я
	case u'x': return u'\u0447'; // ч
	case u'c': return u'\u0441'; // с
	case u'v': return u'\u043C'; // м
	case u'b': return u'\u0438'; // и
	case u'n': return u'\u0442'; // т
	case u'm': return u'\u044C'; // ь
	case u',': return u'\u0431'; // б
	case u'.': return u'\u044E'; // ю
	case u'`': return u'\u0451'; // ё
	case u'Q': return u'\u0419'; // Й
	case u'W': return u'\u0426'; // Ц
	case u'E': return u'\u0423'; // У
	case u'R': return u'\u041A'; // К
	case u'T': return u'\u0415'; // Е
	case u'Y': return u'\u041D'; // Н
	case u'U': return u'\u0413'; // Г
	case u'I': return u'\u0428'; // Ш
	case u'O': return u'\u0429'; // Щ
	case u'P': return u'\u0417'; // З
	case u'{': return u'\u0425'; // Х
	case u'}': return u'\u042A'; // Ъ
	case u'A': return u'\u0424'; // Ф
	case u'S': return u'\u042B'; // Ы
	case u'D': return u'\u0412'; // В
	case u'F': return u'\u0410'; // А
	case u'G': return u'\u041F'; // П
	case u'H': return u'\u0420'; // Р
	case u'J': return u'\u041E'; // О
	case u'K': return u'\u041B'; // Л
	case u'L': return u'\u0414'; // Д
	case u':': return u'\u0416'; // Ж
	case u'"': return u'\u042D'; // Э
	case u'Z': return u'\u042F'; // Я
	case u'X': return u'\u0427'; // Ч
	case u'C': return u'\u0421'; // С
	case u'V': return u'\u041C'; // М
	case u'B': return u'\u0418'; // И
	case u'N': return u'\u0422'; // Т
	case u'M': return u'\u042C'; // Ь
	case u'<': return u'\u0411'; // Б
	case u'>': return u'\u042E'; // Ю
	case u'~': return u'\u0401'; // Ё
	default: return c;
	}
}

// Every English letter converted, length preserved.
[[nodiscard]] inline std::u16string FixKeyboardLayout(
		std::u16string_view text) {
	auto result = std::u16string();
	result.reserve(text.size());
	for (const auto c : text) {
		result.push_back(LayoutKeyToRu(c));
	}
	return result;
}

// True when the text looks like a wrong-layout Russian phrase: it has no
// Cyrillic at all, has at least one Latin letter, and is not a command
// ("/..." or "!...") or a mention ("@...").
[[nodiscard]] inline bool NeedsLayoutFix(std::u16string_view text) {
	if (text.empty()) {
		return false;
	}
	if (text[0] == u'/' || text[0] == u'!' || text[0] == u'@') {
		return false;
	}
	auto hasLatin = false;
	auto hasCyrillic = false;
	for (const auto c : text) {
		if ((c >= u'\u0410' && c <= u'\u044F')
			|| c == u'\u0451'
			|| c == u'\u0401') {
			hasCyrillic = true;
		} else if ((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z')) {
			hasLatin = true;
		}
	}
	return hasLatin && !hasCyrillic;
}

} // namespace Autistic
