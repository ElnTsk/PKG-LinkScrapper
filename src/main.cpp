#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "config.h"
#include "crawl.h"
#include "deadstore.h"
#include "http.h"
#include "output.h"
#include "parts.h"
#include "resolve.h"
#include "trust.h"
#include "util.h"
#include "verify.h"

namespace {

const char* kVersion = "1.0.1";

std::string helpText() {
  std::string text;
  text += "PKG-LinkScrapper " + std::string(kVersion) + " - PS4/PS5 game link hunter (C++)\n";
  text += "\n";
  text += "usage:\n";
  text += "  PKG-LinkScrapper.exe [games...] [options]\n";
  text += "  PKG-LinkScrapper.exe -i games.txt --console ps4 --format json -o found.json\n";
  text += "\n";
  text += "input:\n";
  text += "  <games...>                     game names as positional arguments\n";
  text += "  -i, --input <file>             newline-separated game names (# comments allowed)\n";
  text += "  --stdin                        read game names from stdin\n";
  text += "\n";
  text += "search:\n";
  text += "  --console <ps4|ps5|both>       default both\n";
  text += "  --threshold <0..1>             name match strictness, default from config (0.72)\n";
  text += "  --limit <n>                    max hits per game, default from config (40)\n";
  text += "  --max-fetch <n>                max pages crawled per game, default from config (14)\n";
  text += "  --no-verify                    skip liveness probing\n";
  text += "  --refresh                      probe every link, even known-good ones\n";
  text += "  --trusted-only                 list only recommended links\n";
  text += "  --resolve <url>                walk a share page to the link it hides\n";
  text += "                                 (1fichier). spends the one-time link\n";
  text += "  --pause                        wait for enter before exiting\n";
  text += "\n";
  text += "network:\n";
  text += "  --timeout <ms>                 request timeout, default 15000\n";
  text += "  --concurrency <n>              parallel requests, default 10\n";
  text += "  -v, --verbose                  per-source detail on stderr\n";
  text += "\n";
  text += "output:\n";
  text += "  --format <table|json|csv|tsv>  default table\n";
  text += "  -o, --out <file>               also write the listing to a file\n";
  text += "\n";
  text += "config:\n";
  text += "  --config <file|dir>            extra config layer, merged last\n";
  text += "  --sources                      list loaded sources and their origin files\n";
  text += "  --clear-dead                   forget every link remembered as dead\n";
  text += "  --forget-good                  forget every link remembered as good\n";
  text += "\n";
  text += "  -h, --help\n";
  text += "  --version\n";
  return text;
}

struct Options {
  std::vector<std::string> games;
  std::vector<std::string> files;
  std::string console = "both";
  std::string format = "table";
  std::string out;
  std::string configPath;
  bool haveLimit = false;
  bool haveMaxFetch = false;
  bool haveThreshold = false;
  bool haveTimeout = false;
  bool haveConcurrency = false;
  int limit = 0;
  int maxFetch = 0;
  double threshold = 0;
  int timeout = 0;
  int concurrency = 0;
  bool verify = true;
  bool verbose = false;
  bool pause = false;
  bool listSources = false;
  bool resolve = false;
  std::string resolveUrl;
  bool clearDead = false;
  bool forgetGood = false;
  bool trustedOnly = false;
  bool refresh = false;
  bool help = false;
  bool version = false;
  bool stdinInput = false;
};

bool parseArgs(int argc, char** argv, Options& opts, std::string& error) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    std::string inlineValue;
    bool hasInline = false;

    size_t equals = arg.find("=", 2);
    if (arg.rfind("--", 0) == 0 && equals != std::string::npos) {
      inlineValue = arg.substr(equals + 1);
      arg = arg.substr(0, equals);
      hasInline = true;
    }

    auto value = [&](const std::string& name) -> std::string {
      if (hasInline) return inlineValue;
      if (i + 1 >= argc) {
        error = name + " needs a value";
        return "";
      }
      return argv[++i];
    };

    if (arg == "-h" || arg == "--help") {
      opts.help = true;
    } else if (arg == "--version") {
      opts.version = true;
    } else if (arg == "--sources" || arg == "--list-sources") {
      opts.listSources = true;
    } else if (arg == "--resolve") {
      opts.resolve = true;
      opts.resolveUrl = value(arg);
      if (!error.empty()) return false;
    } else if (arg == "--clear-dead") {
      opts.clearDead = true;
    } else if (arg == "--forget-good") {
      opts.forgetGood = true;
    } else if (arg == "--trusted-only" || arg == "--recommended") {
      opts.trustedOnly = true;
    } else if (arg == "--refresh") {
      opts.refresh = true;
    } else if (arg == "-i" || arg == "--input") {
      std::string v = value(arg);
      if (!error.empty()) return false;
      opts.files.push_back(v);
    } else if (arg == "--stdin") {
      opts.stdinInput = true;
    } else if (arg == "--console") {
      opts.console = toLowerAscii(value(arg));
      if (!error.empty()) return false;
    } else if (arg == "--format") {
      opts.format = toLowerAscii(value(arg));
      if (!error.empty()) return false;
    } else if (arg == "-o" || arg == "--out") {
      opts.out = value(arg);
      if (!error.empty()) return false;
    } else if (arg == "--limit") {
      opts.limit = std::atoi(value(arg).c_str());
      opts.haveLimit = true;
      if (!error.empty()) return false;
    } else if (arg == "--max-fetch") {
      opts.maxFetch = std::atoi(value(arg).c_str());
      opts.haveMaxFetch = true;
      if (!error.empty()) return false;
    } else if (arg == "--threshold") {
      opts.threshold = std::atof(value(arg).c_str());
      opts.haveThreshold = true;
      if (!error.empty()) return false;
    } else if (arg == "--timeout") {
      opts.timeout = std::atoi(value(arg).c_str());
      opts.haveTimeout = true;
      if (!error.empty()) return false;
    } else if (arg == "--concurrency") {
      opts.concurrency = std::atoi(value(arg).c_str());
      opts.haveConcurrency = true;
      if (!error.empty()) return false;
    } else if (arg == "--no-verify") {
      opts.verify = false;
    } else if (arg == "--pause") {
      opts.pause = true;
    } else if (arg == "--config") {
      std::string v = value(arg);
      if (!error.empty()) return false;
      opts.configPath = v;
    } else if (arg == "-v" || arg == "--verbose") {
      opts.verbose = true;
    } else if (!arg.empty() && arg[0] == '-') {
      error = "unknown flag: " + arg;
      return false;
    } else {
      opts.games.push_back(arg);
    }
  }

  if (!error.empty()) return false;

  if (opts.console != "ps4" && opts.console != "ps5" && opts.console != "both") {
    error = "--console must be ps4, ps5, or both (got \"" + opts.console + "\")";
    return false;
  }
  if (opts.format != "table" && opts.format != "json" && opts.format != "csv" && opts.format != "tsv") {
    error = "--format must be table, json, csv, or tsv (got \"" + opts.format + "\")";
    return false;
  }
  if (opts.haveThreshold && (opts.threshold < 0 || opts.threshold > 1)) {
    error = "--threshold must be between 0 and 1";
    return false;
  }
  if ((opts.haveLimit && opts.limit <= 0) || (opts.haveMaxFetch && opts.maxFetch <= 0) ||
      (opts.haveTimeout && opts.timeout <= 0) || (opts.haveConcurrency && opts.concurrency <= 0)) {
    error = "--limit, --max-fetch, --timeout and --concurrency must be positive";
    return false;
  }
  return true;
}

std::string readWholeStream(std::FILE* stream) {
  std::string out;
  char buffer[4096];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), stream)) > 0) out.append(buffer, read);
  return out;
}

bool collectGames(const Options& opts, std::vector<std::string>& games, std::string& error) {
  std::set<std::string> seen;
  auto add = [&](const std::string& raw) {
    std::string name = collapseSpaces(trim(raw));
    if (name.empty() || name[0] == '#') return;
    std::string key = toLowerAscii(name);
    if (seen.count(key)) return;
    seen.insert(key);
    games.push_back(name);
  };

  for (const std::string& game : opts.games) add(game);

  for (const std::string& file : opts.files) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
      error = "file not found: " + file;
      return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    for (const std::string& line : splitLines(buffer.str())) add(line);
  }

  if (opts.stdinInput) {
    for (const std::string& line : splitLines(readWholeStream(stdin))) add(line);
  }
  return true;
}

void printSources(const Config& config, const std::string& exeDir) {
  std::vector<std::string> lines;
  lines.push_back(std::to_string(config.files.size()) + " config file(s):");
  for (const std::string& file : config.files) {
    std::string relative = file;
    if (file.rfind(exeDir, 0) == 0) {
      relative = file.substr(exeDir.size());
      while (!relative.empty() && (relative[0] == '\\' || relative[0] == '/')) {
        relative.erase(relative.begin());
      }
    }
    lines.push_back("  " + (relative.empty() ? file : relative));
  }
  lines.push_back("");

  size_t nameWidth = 4;
  for (const Source& source : config.sources) nameWidth = std::max(nameWidth, source.name.size());

  auto row = [](std::vector<std::string> cells) {
    std::string out;
    for (size_t i = 0; i < cells.size(); ++i) {
      if (i) out += "  ";
      out += cells[i];
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
  };

  lines.push_back(row({"NAME" + std::string(nameWidth - 4, ' '), "TYPE  ", "STATE  ", "ORIGIN"}));

  for (const Source& source : config.sources) {
    std::string name = source.name + std::string(nameWidth - source.name.size(), ' ');
    std::string type = source.type + std::string(std::max<size_t>(0, 6 - source.type.size()), ' ');
    lines.push_back(row({name, type, "enabled", source.origin}));
  }

  for (const std::string& name : config.disabled) {
    lines.push_back("disabled by config: " + name);
  }

  std::string out = joinValues(lines, "\n") + "\n";
  std::fwrite(out.data(), 1, out.size(), stdout);
}

void applyOverrides(const Options& opts, Config& config) {
  if (opts.haveTimeout) config.settings.timeoutMs = opts.timeout;
  if (opts.haveConcurrency) config.settings.concurrency = opts.concurrency;
  if (opts.haveThreshold) config.settings.threshold = opts.threshold;
  if (opts.haveMaxFetch) config.settings.maxFetchPerGame = opts.maxFetch;
  if (opts.haveLimit) config.settings.maxPerGame = opts.limit;
  if (!opts.verify) config.settings.verify = false;
}

int runHunt(const Options& opts, Config& config, const std::vector<std::string>& games) {
  if (games.empty()) return 0;

  std::vector<std::string> dead;
  loadDeadStore(dead);
  setDeadStore(&dead);

  std::vector<GoodEntry> good;
  loadGoodStore(good);
  long long freshMs = static_cast<long long>(config.settings.goodTtlDays) * 24 * 3600 * 1000;
  setTrustLists(&good, &config.settings.trustedHosts, &config.settings.trustedSources, freshMs,
                opts.refresh);
  logInfo("trust: " + std::to_string(config.settings.trustedHosts.size()) + " hosts, " +
          std::to_string(config.settings.trustedSources.size()) + " sources, " +
          std::to_string(good.size()) + " proven links");

  std::mutex mutex;
  std::vector<Hit> hits;
  size_t finished = 0;

  logInfo("PKG-LinkScrapper " + std::string(kVersion) + " . " + std::to_string(config.sources.size()) +
          " sources . " + opts.console + " . " + std::to_string(games.size()) + " game(s)");

  huntMany(games, opts.console, config, [&](size_t index, std::vector<Hit> arrived) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      hits.insert(hits.end(), arrived.begin(), arrived.end());
      ++finished;
      logInfo("[" + std::to_string(finished) + "/" + std::to_string(games.size()) + "] " +
              games[index] + " - " + std::to_string(arrived.size()) + " links");
    }
  });

  sortHits(hits, games);

  // anything the per-title pass left unverified gets a second go, then a
  // reachability answer, so no row ends the run reading unverified
  if (config.settings.verify) {
    std::vector<Hit> leftovers;
    for (const Hit& hit : hits) {
      if (hit.state == "unverified" || hit.state == "skipped") leftovers.push_back(hit);
    }
    if (!leftovers.empty()) {
      logInfo("re-checking " + std::to_string(leftovers.size()) + " leftover links");
      ThreadPool second(config.settings.concurrency);
      verifyAll(leftovers, second);
      for (size_t i = 0; i < hits.size(); ++i) {
        for (const Hit& fixed : leftovers) {
          if (hits[i].url == fixed.url) {
            hits[i] = fixed;
            break;
          }
        }
      }
    }
  }

  size_t remembered = 0;
  for (const Hit& hit : hits) {
    if (hit.state != "dead" && hit.state != "unreachable" && hit.state != "stub") continue;
    std::string needle = normalizeUrl(hit.url);
    if (std::binary_search(dead.begin(), dead.end(), needle)) continue;
    std::string error;
    if (appendDeadStore(hit.url, hit.state, hit.host, hit.title, error)) {
      dead.push_back(needle);
      ++remembered;
    }
  }
  std::sort(dead.begin(), dead.end());
  if (remembered) logInfo("remembered " + std::to_string(remembered) + " dead links in " + deadStorePath());

  // one row per part-set: a 21-part game is one download, not 21 lines
  std::vector<Hit> rows = groupParts(hits);

  std::string storeError;
  if (rememberGood(hits, storeError)) {
    logInfo("good list: " + std::to_string(goodStoreCount()) + " links in " + goodStorePath());
  } else {
    logInfo("good list: " + storeError);
  }

  if (opts.trustedOnly) {
    size_t before = rows.size();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const Hit& row) { return row.trust.empty(); }),
               rows.end());
    logInfo("trusted-only: " + std::to_string(before - rows.size()) + " rows dropped");
  }

  sortHits(rows, games);

  std::string text = renderHits(rows, opts.format) + "\n";
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);

  if (!opts.out.empty()) {
    std::ofstream stream(opts.out, std::ios::binary | std::ios::trunc);
    if (!stream) {
      logInfo("cannot write " + opts.out);
      return 1;
    }
    stream << text;
    stream.close();
    logInfo("wrote " + opts.out);
  }

  logInfo(summarize(rows, games.size()));
  return 0;
}

void pauseHere() {
  std::fputs("\npress enter to close\n", stdout);
  std::fflush(stdout);
  std::string ignored;
  std::getline(std::cin, ignored);
}

void interactiveRun(Options& opts, Config& config) {
  std::string exeDir = exeDirectory();
  std::vector<std::string> batch;
  bool gotInput = false;

  std::printf("PKG-LinkScrapper %s - %d sources\n", kVersion, static_cast<int>(config.sources.size()));
  std::printf("type game names, one per line, then a blank line to hunt.\n");
  std::printf("console: ps4 ps5 both   format: table json csv tsv   sources: list|disable <name>   rec: recommended only   q: quit\n\n");

  auto splitSemicolons = [](const std::string& value) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= value.size()) {
      size_t semis = value.find(';', pos);
      if (semis == std::string::npos) semis = value.size();
      std::string part = collapseSpaces(trim(value.substr(pos, semis - pos)));
      if (!part.empty()) parts.push_back(part);
      pos = semis + 1;
    }
    return parts;
  };

  for (;;) {
    std::string prompt = "names [" + opts.console + " " + opts.format + "]> ";
    std::fputs(prompt.c_str(), stdout);
    std::fflush(stdout);

    std::string line;
    if (!std::getline(std::cin, line)) {
      if (!batch.empty()) runHunt(opts, config, batch);
      else if (!gotInput) {
        std::printf("\n(no console input - double-click PKG-LinkScrapper.exe, or run it inside a cmd window)\n");
      }
      break;
    }
    gotInput = true;

    std::string value = collapseSpaces(trim(line));
    std::string command = toLowerAscii(value);

    if (value.empty()) {
      if (batch.empty()) continue;
      runHunt(opts, config, batch);
      batch.clear();
      std::fputc('\n', stdout);
      std::fflush(stdout);
      continue;
    }

    if (command == "q" || command == "quit" || command == "exit") break;
    if (command == "ps4" || command == "ps5" || command == "both") {
      opts.console = command;
      continue;
    }
    if (command == "table" || command == "json" || command == "csv" || command == "tsv") {
      opts.format = command;
      continue;
    }
    if (command == "list") {
      printSources(config, exeDir);
      continue;
    }
    if (command == "clear") {
      batch.clear();
      std::printf("(queue cleared)\n");
      continue;
    }
    if (command == "rec" || command == "recommended") {
      opts.trustedOnly = !opts.trustedOnly;
      std::printf("recommended only: %s\n", opts.trustedOnly ? "on" : "off");
      continue;
    }
    if (command.rfind("disable ", 0) == 0) {
      std::string name = toLowerAscii(collapseSpaces(trim(value.substr(8))));
      config.sources.erase(std::remove_if(config.sources.begin(), config.sources.end(),
                                          [&](const Source& source) {
                                            return toLowerAscii(source.name) == name;
                                          }),
                           config.sources.end());
      std::printf("disabled %s - %d sources left\n", name.c_str(),
                  static_cast<int>(config.sources.size()));
      continue;
    }

    for (const std::string& part : splitSemicolons(value)) batch.push_back(part);
    std::printf("queued %d\n", static_cast<int>(batch.size()));
  }
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);

  Options opts;
  std::string error;
  if (!parseArgs(argc, argv, opts, error)) {
    logInfo("error: " + error);
    std::string help = helpText() + "\n";
    std::fwrite(help.data(), 1, help.size(), stdout);
    return 2;
  }

  if (opts.help) {
    std::string help = helpText() + "\n";
    std::fwrite(help.data(), 1, help.size(), stdout);
    return 0;
  }
  if (opts.version) {
    std::string text = std::string("PKG-LinkScrapper ") + kVersion + "\n";
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }

  gVerbose = opts.verbose;
  std::string exeDir = exeDirectory();

  Config config;
  if (!loadConfig(config, exeDir, opts.configPath, error)) {
    logInfo("config: " + error);
    return 2;
  }

  if (opts.listSources) {
    printSources(config, exeDir);
    return 0;
  }

  if (opts.resolve) {
    httpInit(config.settings.userAgent, 1, config.settings.timeoutMs, 1, 0);
    if (!resolvableHost(opts.resolveUrl)) {
      std::printf("%s\n  no walk implemented for this host\n", opts.resolveUrl.c_str());
      httpShutdown();
      return 0;
    }
    DirectLink link = resolveDirect(opts.resolveUrl);
    std::printf("%s\n  walked: %s\n  link: %s\n  name: %s\n  note: %s\n", opts.resolveUrl.c_str(),
                link.ok ? "yes" : "no", link.url.c_str(), link.name.c_str(), link.note.c_str());
    if (link.ok) {
      // asking the mirror for one byte spends the link - that is the host's
      // rule, not a bug here, so this flag is a check, not a way to grab a file
      HttpResponse probe =
          httpRequest(link.url, "GET", std::vector<std::string>{"Range: bytes=0-0"}, true, false);
      std::printf("  http %ld  type %s\n  range %s\n  disposition %s\n", probe.status,
                  probe.header("content-type").c_str(), probe.header("content-range").c_str(),
                  probe.header("content-disposition").c_str());
    }
    httpShutdown();
    return link.ok ? 0 : 1;
  }

  std::vector<std::string> games;
  if (!collectGames(opts, games, error)) {
    logInfo("input: " + error);
    return 2;
  }

  if (opts.clearDead) {
    std::string error2;
    if (!clearDeadStore(error2)) {
      logInfo("clear-dead: " + error2);
      return 1;
    }
    logInfo("cleared " + deadStorePath());
    if (games.empty()) return 0;
  }

  if (opts.forgetGood) {
    std::string error2;
    if (!clearGoodStore(error2)) {
      logInfo("forget-good: " + error2);
      return 1;
    }
    logInfo("cleared " + goodStorePath());
    if (games.empty()) return 0;
  }

  const bool interactiveLaunch = argc == 1;
  if (!interactiveLaunch && games.empty()) {
    logInfo("no games given. pass names, -i <file>, or --stdin. double-click with no arguments for interactive mode.");
    std::string help = helpText() + "\n";
    std::fwrite(help.data(), 1, help.size(), stdout);
    return 2;
  }

  applyOverrides(opts, config);
  httpInit(config.settings.userAgent, config.settings.concurrency, config.settings.timeoutMs, 1,
           config.settings.minHostDelayMs);

  int code = 0;
  if (interactiveLaunch) {
    interactiveRun(opts, config);
  } else {
    code = runHunt(opts, config, games);
    if (opts.pause) pauseHere();
  }

  httpShutdown();
  return code;
}
