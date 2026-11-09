#pragma once

#include <string>

#include "crawl.h"
#include "util.h"

void verifyHit(Hit& hit);
// deadlineMs 0 waits as long as it takes; otherwise unprobed rows stop at the
// clock and stay unverified
void verifyAll(std::vector<Hit>& hits, ThreadPool& pool, int64_t deadlineMs = 0);

// Forget the in-session probe memo. A new hunt should measure the internet as
// it is now, not as it was ten minutes ago.
void resetProbeCache();
