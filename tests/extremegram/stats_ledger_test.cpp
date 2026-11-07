#include "../../Telegram/SourceFiles/core/extremegram_stats_ledger.h"
#include <cassert>

int main() {
	using namespace ExtremeGram;
	StatsLedger ledger;
	assert(ledger.record({ 101, 7 }));
	assert(!ledger.record({ 101, 7 }));
	assert(!ledger.record({ -1, 7 }));
	assert(ledger.recordPage({{101, 7}, {100, 8}, {99, 7}}, 99, false));
	assert(ledger.counts().at(7) == 2);
	assert(ledger.counts().at(8) == 1);
	assert(!ledger.recordPage({{98, 8}}, 100, false));
	StatsLedger resumed(ledger.checkpoint());
	assert(resumed.recordPage({{98, 8}}, 98, true));
	assert(resumed.checkpoint().historyComplete);
	assert(resumed.record({102, 7}));
	assert(resumed.counts().at(7) == 3);
	assert(resumed.counts().at(8) == 2);
	assert(!resumed.recordPage({}, 0, true));
}
