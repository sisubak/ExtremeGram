/*
A checkpointable in-memory accumulator for ExtremeGram history statistics.
The caller supplies Telegram message IDs and stable author IDs.
*/
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ExtremeGram {

struct StatsMessage {
	std::int64_t id = 0;
	std::uint64_t author = 0;
};

struct StatsCheckpoint {
	// The cursor is exclusive: the next history page starts before this ID.
	std::int64_t cursor = 0;
	bool historyComplete = false;
	std::map<std::int64_t, std::uint64_t> messages;
};

class StatsLedger {
public:
	explicit StatsLedger(StatsCheckpoint checkpoint = {})
	: _state(std::move(checkpoint)) {
	}

	// Existing IDs are ignored, so overlapping pages and live updates are safe.
	bool record(StatsMessage message) {
		if (message.id <= 0 || message.author == 0) return false;
		return _state.messages.emplace(message.id, message.author).second;
	}

	// Advance only after the whole page has been recorded. A caller should
	// persist the resulting checkpoint atomically before requesting another page.
	bool recordPage(const std::vector<StatsMessage> &messages,
			std::int64_t oldestId, bool endOfHistory) {
		if (_state.historyComplete || oldestId < 0) return false;
		if (!endOfHistory && (oldestId == 0
			|| (_state.cursor != 0 && oldestId >= _state.cursor))) return false;
		for (const auto &message : messages) record(message);
		if (oldestId) _state.cursor = oldestId;
		_state.historyComplete = endOfHistory;
		return true;
	}

	[[nodiscard]] std::map<std::uint64_t, std::uint64_t> counts() const {
		auto result = std::map<std::uint64_t, std::uint64_t>();
		for (const auto &[id, author] : _state.messages) ++result[author];
		return result;
	}

	[[nodiscard]] const StatsCheckpoint &checkpoint() const { return _state; }

private:
	StatsCheckpoint _state;
};

} // namespace ExtremeGram
