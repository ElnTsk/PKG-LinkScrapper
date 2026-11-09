#pragma once

#include <string>
#include <vector>

#include "crawl.h"

// Two kinds of "this one is good":
//   - the curated list in the config: hosts and sources that answer every run
//   - what this tool has personally seen serve bytes, kept in good.tsv beside
//     dead.tsv, with the size it saw and when
// A row carrying trust "recommended" sorts first, and a url in the good store
// within its freshness window is reported alive without a second probe.

struct GoodEntry {
  std::string url;
  std::string host;
  std::string title;
  long long sizeBytes = 0;
  long long seenMs = 0;
  int hits = 0;
};

std::string goodStorePath();
bool loadGoodStore(std::vector<GoodEntry>& entries);
bool clearGoodStore(std::string& error);
size_t goodStoreCount();

// Upserts every alive hit: new urls are added, known ones get their size,
// timestamp and hit count bumped. Entries older than three freshness windows
// are dropped on the way out.
bool rememberGood(const std::vector<Hit>& hits, std::string& error);

// drop one url from the proven list - used when a later probe says the link is
// not what it looked like, so the cache cannot keep blessing it
bool forgetGood(const std::string& url, std::string& error);

// Pointers must outlive the hunt, same deal as the dead store. `refresh` makes
// every probe run even for a known-good url.
void setTrustLists(const std::vector<GoodEntry>* entries, const std::vector<std::string>* hosts,
                   const std::vector<std::string>* sources, long long freshMs, bool refresh);

// A proven-alive entry inside its window, or null.
const GoodEntry* lookupGood(const std::string& url);

bool hostTrusted(const std::string& host);
bool sourceTrusted(const std::string& name);

// "recommended" when the host or source is curated, or the url has proven alive
// before. Empty otherwise.
std::string trustLabel(const std::string& url, const std::string& host, const std::string& source);
