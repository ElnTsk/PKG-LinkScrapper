#include "trust.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "deadstore.h"
#include "hosts.h"
#include "util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

std::mutex g_mutex;
size_t g_count = 0;

const std::vector<GoodEntry>* g_entries = nullptr;
const std::vector<std::string>* g_hosts = nullptr;
const std::vector<std::string>* g_sources = nullptr;
long long g_freshMs = 7LL * 24 * 3600 * 1000;
bool g_refresh = false;

std::string path() { return stateDirectory() + "\\good.tsv"; }

std::string clean(std::string value) {
  for (char& c : value) {
    if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  }
  return collapseSpaces(trim(value));
}

bool parseInto(const std::string& line, GoodEntry& out) {
  std::vector<std::string> cells;
  size_t pos = 0;
  while (pos <= line.size()) {
    size_t tab = line.find('\t', pos);
    if (tab == std::string::npos) tab = line.size();
    cells.push_back(line.substr(pos, tab - pos));
    pos = tab + 1;
  }
  if (cells.size() < 1 || cells[0].empty()) return false;
  out.url = cells[0];
  if (cells.size() > 1) out.host = cells[1];
  if (cells.size() > 2) out.title = cells[2];
  if (cells.size() > 3) out.sizeBytes = std::strtoll(cells[3].c_str(), nullptr, 10);
  if (cells.size() > 4) out.seenMs = std::strtoll(cells[4].c_str(), nullptr, 10);
  if (cells.size() > 5) out.hits = static_cast<int>(std::strtol(cells[5].c_str(), nullptr, 10));
  return true;
}

bool readAll(std::vector<GoodEntry>& entries) {
  entries.clear();
  std::ifstream stream(path());
  if (!stream) return false;
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  for (const std::string& line : splitLines(buffer.str())) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;
    GoodEntry entry;
    if (parseInto(trimmed, entry)) entries.push_back(entry);
  }
  std::sort(entries.begin(), entries.end(),
            [](const GoodEntry& a, const GoodEntry& b) { return a.url < b.url; });
  entries.erase(std::unique(entries.begin(), entries.end(),
                            [](const GoodEntry& a, const GoodEntry& b) { return a.url == b.url; }),
                entries.end());
  return true;
}

bool writeAll(const std::vector<GoodEntry>& entries, std::string& error) {
  std::string folder = stateDirectory();
  DWORD attributes = GetFileAttributesA(folder.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryA(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
      error = "cannot create " + folder;
      return false;
    }
  }

  std::ostringstream out;
  out << "# url\thost\ttitle\tsize_bytes\tseen_ms\thits\n";
  for (const GoodEntry& entry : entries) {
    out << entry.url << '\t' << clean(entry.host) << '\t' << clean(entry.title) << '\t'
        << entry.sizeBytes << '\t' << entry.seenMs << '\t' << entry.hits << "\r\n";
  }

  std::ofstream stream(path(), std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "cannot write " + path();
    return false;
  }
  stream << out.str();
  stream.close();
  return true;
}

}  // namespace

std::string goodStorePath() { return path(); }

bool loadGoodStore(std::vector<GoodEntry>& entries) {
  std::lock_guard<std::mutex> lock(g_mutex);
  bool found = readAll(entries);
  g_count = entries.size();
  return found;
}

bool clearGoodStore(std::string& error) {
  if (DeleteFileA(path().c_str())) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_count = 0;
    return true;
  }
  DWORD last = GetLastError();
  if (last == ERROR_FILE_NOT_FOUND) return true;
  error = "cannot delete " + path();
  return false;
}

size_t goodStoreCount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_count;
}

bool rememberGood(const std::vector<Hit>& hits, std::string& error) {
  std::vector<Hit> alive;
  for (const Hit& hit : hits) {
    if (hit.state == "alive" && !hit.url.empty()) alive.push_back(hit);
  }
  if (alive.empty()) return true;

  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<GoodEntry> entries;
  readAll(entries);
  std::map<std::string, size_t> index;
  for (size_t i = 0; i < entries.size(); ++i) index[entries[i].url] = i;

  long long now = wallMs();
  for (const Hit& hit : alive) {
    std::string key = normalizeUrl(hit.url);
    auto found = index.find(key);
    if (found == index.end()) {
      GoodEntry entry;
      entry.url = key;
      entry.host = hit.host.empty() ? bareHost(hit.url) : hit.host;
      entry.title = hit.title;
      entry.sizeBytes = hit.sizeBytes;
      entry.seenMs = now;
      entry.hits = 1;
      index[key] = entries.size();
      entries.push_back(entry);
    } else {
      GoodEntry& entry = entries[found->second];
      entry.seenMs = now;
      ++entry.hits;
      if (hit.sizeBytes > 0) entry.sizeBytes = hit.sizeBytes;
      if (entry.title.empty()) entry.title = hit.title;
      if (entry.host.empty()) entry.host = hit.host;
    }
  }

  // a link nobody has seen work in three windows is no longer a recommendation
  long long cutoff = now - 3 * g_freshMs;
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const GoodEntry& entry) { return entry.seenMs < cutoff; }),
                entries.end());
  std::sort(entries.begin(), entries.end(),
            [](const GoodEntry& a, const GoodEntry& b) { return a.url < b.url; });

  if (!writeAll(entries, error)) return false;
  g_count = entries.size();
  return true;
}

void setTrustLists(const std::vector<GoodEntry>* entries, const std::vector<std::string>* hosts,
                   const std::vector<std::string>* sources, long long freshMs, bool refresh) {
  g_entries = entries;
  g_hosts = hosts;
  g_sources = sources;
  if (freshMs > 0) g_freshMs = freshMs;
  g_refresh = refresh;
}

const GoodEntry* lookupGood(const std::string& url) {
  if (!g_entries || g_entries->empty() || g_refresh) return nullptr;
  std::string key = normalizeUrl(url);
  auto found = std::lower_bound(g_entries->begin(), g_entries->end(), key,
                                 [](const GoodEntry& entry, const std::string& value) {
                                   return entry.url < value;
                                 });
  if (found == g_entries->end() || found->url != key) return nullptr;
  if (found->seenMs < wallMs() - g_freshMs) return nullptr;
  return &*found;
}

bool hostTrusted(const std::string& host) {
  return g_hosts && hostInList(host, *g_hosts);
}

bool sourceTrusted(const std::string& name) {
  if (!g_sources || name.empty()) return false;
  std::string lower = toLowerAscii(collapseSpaces(trim(name)));
  for (const std::string& needle : *g_sources) {
    if (toLowerAscii(collapseSpaces(trim(needle))) == lower) return true;
  }
  return false;
}

std::string trustLabel(const std::string& url, const std::string& host, const std::string& source) {
  if (lookupGood(url)) return "recommended";
  if (hostTrusted(host)) return "recommended";
  if (sourceTrusted(source)) return "recommended";
  return std::string();
}

bool forgetGood(const std::string& url, std::string& error) {
  if (url.empty()) return false;
  const std::string key = normalizeUrl(url);

  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<GoodEntry> entries;
  readAll(entries);
  size_t before = entries.size();
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const GoodEntry& entry) { return entry.url == key; }),
                entries.end());
  if (entries.size() == before) return false;
  if (!writeAll(entries, error)) return false;
  g_count = entries.size();
  return true;
}
