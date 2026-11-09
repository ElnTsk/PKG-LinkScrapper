#include "deadstore.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "util.h"

namespace {

std::mutex g_mutex;
size_t g_count = 0;

std::string directory() {
  const char* appData = std::getenv("APPDATA");
  if (appData && *appData) return std::string(appData) + "\\PKG-LinkScrapper";
  const char* home = std::getenv("USERPROFILE");
  if (home && *home) return std::string(home) + "\\.config\\PKG-LinkScrapper";
  return "PKG-LinkScrapper";
}

}  // namespace

std::string stateDirectory() { return directory(); }

std::string deadStorePath() { return directory() + "\\dead.tsv"; }

bool loadDeadStore(std::vector<std::string>& urls) {
  urls.clear();
  std::ifstream stream(deadStorePath());
  if (!stream) return false;
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  for (const std::string& line : splitLines(buffer.str())) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;
    size_t tab = trimmed.find('\t');
    std::string url = tab == std::string::npos ? trimmed : trimmed.substr(0, tab);
    if (!url.empty()) urls.push_back(normalizeUrl(url));
  }
  std::sort(urls.begin(), urls.end());
  urls.erase(std::unique(urls.begin(), urls.end()), urls.end());
  std::lock_guard<std::mutex> lock(g_mutex);
  g_count = urls.size();
  return true;
}

bool deadStoreContains(const std::vector<std::string>& urls, const std::string& url) {
  if (urls.empty()) return false;
  std::string needle = normalizeUrl(url);
  return std::binary_search(urls.begin(), urls.end(), needle);
}

bool appendDeadStore(const std::string& url, const std::string& state, const std::string& host,
                     const std::string& title, std::string& error) {
  std::string path = deadStorePath();
  std::string folder = path.substr(0, path.find_last_of("\\/"));
  DWORD attributes = GetFileAttributesA(folder.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryA(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
      error = "cannot create " + folder;
      return false;
    }
  }

  std::ofstream stream(path, std::ios::binary | std::ios::app);
  if (!stream) {
    error = "cannot write " + path;
    return false;
  }

  auto clean = [](std::string value) {
    for (char& c : value) {
      if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }
    return value;
  };

  stream << normalizeUrl(url) << '\t' << clean(state) << '\t' << clean(host) << '\t'
         << clean(title) << '\t' << wallMs() << "\r\n";
  stream.close();

  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_count;
  return true;
}

bool clearDeadStore(std::string& error) {
  if (DeleteFileA(deadStorePath().c_str())) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_count = 0;
    return true;
  }
  DWORD last = GetLastError();
  if (last == ERROR_FILE_NOT_FOUND) return true;
  error = "cannot delete " + deadStorePath();
  return false;
}

size_t deadStoreCount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_count;
}
