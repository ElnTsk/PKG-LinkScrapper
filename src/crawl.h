#pragma once

#include <functional>
#include <string>
#include <vector>

#include "config.h"
#include "util.h"

struct Hit {
  std::string game;
  std::string console;
  std::string kind;
  std::string title;
  std::string source;
  std::string url;
  std::string host;
  std::string state = "unverified";
  std::string trust;
  std::string size;
  std::string error;
  long long sizeBytes = 0;
  int http = 0;
  // -1 untested, 0 no route, >0 connect latency in ms
  int pingMs = -1;
  std::string pingNote;
  double matchScore = 0;
  int partCount = 0;
  int partIndex = 0;
  bool isChild = false;
  std::vector<std::string> partUrls;
};

struct HuntResult {
  std::vector<Hit> hits;
  int pagesCrawled = 0;
  long long elapsedMs = 0;
  std::vector<std::string> errors;
};

// A hunt (crawl plus probing) stops accepting new work after this long and
// reports what it has; leftovers stay unverified for Re-check.
constexpr int kHuntBudgetMs = 30000;
// when the whole run started - the gui counts the same clock down on screen
int64_t huntStartedAtMs();

// Links the caller wants to stop seeing: stubs, 404s, hosts that never answer.
// The list must outlive the hunt.
void setDeadStore(const std::vector<std::string>* urls);

// consolePref: "ps4" | "ps5" | "both"
HuntResult huntGame(const std::string& game, const std::string& consolePref, const Config& config,
                    ThreadPool& pool);

// Same, but fires onHit for every accepted link the moment it is recorded, so a
// caller can show rows while the crawl is still running. onHit also fires for
// each link once its liveness probe has come back, so the caller can update it.
HuntResult huntGame(const std::string& game, const std::string& consolePref, const Config& config,
                    ThreadPool& pool, const std::function<void(const Hit&)>& onHit = {});

// Runs every title in parallel (up to three at a time) and verifies each title's
// hits as they land, so results arrive instead of all appearing at the end.
// onGameDone fires from worker threads - callers marshal to their own thread.
void huntMany(const std::vector<std::string>& games, const std::string& consolePref,
              const Config& config, const std::function<void(size_t, std::vector<Hit>)>& onGameDone,
              const std::function<void(const Hit&)>& onHit = {});

std::vector<std::string> consolesFor(const std::string& consolePref);
std::string renderTemplate(const std::string& tpl, const std::string& query, const std::string& consoleTag,
                           int page);
