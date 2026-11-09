#pragma once

#include <string>

#include "crawl.h"
#include "util.h"

void verifyHit(Hit& hit);
void verifyAll(std::vector<Hit>& hits, ThreadPool& pool);

// Forget the in-session probe memo. A new hunt should measure the internet as
// it is now, not as it was ten minutes ago.
void resetProbeCache();
