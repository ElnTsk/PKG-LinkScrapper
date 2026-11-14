#include "download.h"

#include "hosts.h"

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include "deadstore.h"
#include "http.h"
#include "merge.h"
#include "parts.h"
#include "util.h"

namespace {

#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 20
#endif
#ifndef WINHTTP_REDIRECT_POLICY_NEVER
#define WINHTTP_REDIRECT_POLICY_NEVER 0x00000000
#endif

constexpr size_t kChunk = 64 * 1024;
constexpr int kMaxRedirects = 6;
constexpr int kDefaultWorkers = 3;

struct Job {
  DownloadItem item;  // metadata; mutated only under g_mutex
  // live counters stay atomic so a repaint never blocks a transfer
  std::atomic<long long> totalBytes{0};
  std::atomic<long long> doneBytes{0};
  std::atomic<double> speedMBs{0.0};
  std::atomic<bool> abort{false};
  std::atomic<bool> pause{false};
  std::atomic<bool> shownPaused{false};
  // set once the worker is done touching this job; RemoveFinished only deletes
  // settled jobs, so Cancel -> Clear done cannot free a job mid-unwind
  std::atomic<bool> settled{false};
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<std::string> g_ready;
std::map<std::string, Job*> g_jobs;
std::vector<std::string> g_order;
std::vector<std::thread> g_threads;
std::atomic<bool> g_shutdown{false};
std::atomic<bool> g_paused{false};
HINTERNET g_session = nullptr;

// One split-pkg set queued in a single click. ids are in part order; once every
// member is done the pieces concatenate into output and a checksum is written.
struct MergeGroup {
  std::vector<std::string> ids;
  std::string output;
  std::string label;
  bool fired = false;
  bool blocked = false;
};
std::vector<MergeGroup> g_mergeGroups;
std::vector<std::string> g_mergeLog;
std::atomic<int> g_merging{0};

std::string folderFile() { return stateDirectory() + "\\downloads.txt"; }

std::wstring wide(const std::string& value) {
  if (value.empty()) return std::wstring();
  int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
  return out;
}

std::string utf8(const std::wstring& value) {
  if (value.empty()) return std::string();
  int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                  nullptr, nullptr);
  std::string out(static_cast<size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count,
                      nullptr, nullptr);
  return out;
}

std::string sanitize(const std::string& raw) {
  std::string out;
  for (char c : raw) {
    if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '\\' ||
        c == '/') {
      out.push_back('_');
    } else if (static_cast<unsigned char>(c) >= 32) {
      out.push_back(c);
    }
  }
  out = collapseSpaces(trim(out));
  if (out.size() > 160) out = out.substr(0, 160);
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
  return out;
}

std::string nameForUrl(const std::string& url) {
  UrlParts parts = crackUrl(url);
  std::string path = parts.valid ? parts.path : url;
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  name = percentDecode(name);
  size_t query = name.find_first_of("?#");
  if (query != std::string::npos) name = name.substr(0, query);
  name = sanitize(name);
  if (name.empty()) name = "download.bin";
  return name;
}

std::string uniquePath(const std::string& folder, const std::string& name) {
  std::string candidate = folder + "\\" + name;
  if (GetFileAttributesA(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
  size_t dot = name.find_last_of('.');
  std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
  std::string suffix = dot == std::string::npos ? std::string() : name.substr(dot);
  for (int n = 2; n < 1000; ++n) {
    std::string trial = folder + "\\" + stem + " (" + std::to_string(n) + ")" + suffix;
    if (GetFileAttributesA(trial.c_str()) == INVALID_FILE_ATTRIBUTES) return trial;
  }
  return candidate;
}

bool ensureSession() {
  if (g_session) return true;
  g_session = WinHttpOpen(
      L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
      L"Chrome/131.0.0.0 Safari/537.36",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!g_session) return false;
  // transfers are slow by nature: generous connect and read windows
  WinHttpSetTimeouts(g_session, 15000, 20000, 30000, 120000);
  return true;
}

std::string fileBaseName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Runs under g_mutex from finish(). When the last piece of a split-pkg set lands,
// snapshot the part paths and merge on a detached thread so the transfer workers
// are never blocked by a multi-gigabyte concatenation.
void advanceMerges(Job* job) {
  for (MergeGroup& group : g_mergeGroups) {
    if (group.fired || group.blocked) continue;
    bool member = false;
    for (const std::string& id : group.ids) {
      if (id == job->item.id) {
        member = true;
        break;
      }
    }
    if (!member) continue;

    if (job->item.state != "done") {
      group.blocked = true;
      g_mergeLog.push_back("merge skipped - " + group.label + ": a part did not finish");
      continue;
    }

    bool allDone = true;
    std::vector<std::string> paths;
    paths.reserve(group.ids.size());
    for (const std::string& id : group.ids) {
      auto found = g_jobs.find(id);
      if (found == g_jobs.end() || found->second->item.state != "done") {
        allDone = false;
        break;
      }
      paths.push_back(found->second->item.path);
    }
    if (!allDone) continue;

    group.fired = true;
    g_merging.fetch_add(1);
    std::string output = group.output;
    std::string label = group.label;
    size_t count = paths.size();
    std::thread([paths, output, label, count]() {
      MergeOutcome merged = mergePkgParts(paths, output);
      std::string line;
      if (merged.ok) {
        line = "merged " + std::to_string(count) + " parts -> " + fileBaseName(output) + " (" +
               formatBytes(merged.bytes) + ") - sha256 written";
      } else {
        line = "merge failed - " + label + ": " + merged.error;
      }
      std::lock_guard<std::mutex> lock(g_mutex);
      g_merging.fetch_sub(1);
      g_mergeLog.push_back(line);
    }).detach();
  }
}

void finish(Job* job, const std::string& state, const std::string& error = std::string()) {
  std::lock_guard<std::mutex> lock(g_mutex);
  job->item.state = state;
  if (!error.empty()) job->item.error = error;
  if (state != "done") job->speedMBs.store(0.0);
  job->shownPaused.store(false);
  advanceMerges(job);
}

// flips the visible state only when it changes, so a pause spin costs no locks
void markPaused(Job* job, bool paused) {
  if (job->shownPaused.exchange(paused) == paused) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  job->item.state = paused ? "paused" : "running";
}

// content-range wins, then content-length plus whatever is already on disk
long long expectedOf(const std::string& rawHeaders, long long already) {
  std::string lower = toLowerAscii(rawHeaders);
  size_t range = lower.find("content-range:");
  if (range != std::string::npos) {
    size_t slash = lower.find('/', range);
    if (slash != std::string::npos) {
      size_t start = slash + 1;
      while (start < lower.size() && lower[start] == ' ') ++start;
      long long total = std::strtoll(lower.c_str() + start, nullptr, 10);
      if (total > 0) return total;
    }
  }
  size_t length = lower.find("content-length:");
  if (length != std::string::npos) {
    size_t start = length + sizeof("content-length:") - 1;
    size_t end = start;
    while (end < lower.size() && lower[end] >= '0' && lower[end] <= '9') ++end;
    long long value = std::strtoll(lower.c_str() + start, nullptr, 10);
    if (value > 0) return value + already;
  }
  return 0;
}

DWORD statusCodeOf(HINTERNET request) {
  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
    return 0;
  }
  return status;
}

std::string headerOf(HINTERNET request, DWORD flag, const std::string& name) {
  wchar_t buffer[4096];
  DWORD size = sizeof(buffer) - sizeof(wchar_t);
  buffer[0] = L'\0';
  std::wstring query = wide(name);
  if (!WinHttpQueryHeaders(request, flag, query.empty() ? WINHTTP_HEADER_NAME_BY_INDEX : query.c_str(),
                           buffer, &size, WINHTTP_NO_HEADER_INDEX)) {
    return std::string();
  }
  return utf8(std::wstring(buffer, size / sizeof(wchar_t)));
}

// one open request per attempt; both handles close on every exit path
struct Attempt {
  HINTERNET connection = nullptr;
  HINTERNET request = nullptr;
  ~Attempt() {
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
  }
};

bool openAttempt(const UrlParts& parts, long long resume, Attempt& attempt, std::string& failure) {
  INTERNET_PORT port = parts.port ? static_cast<INTERNET_PORT>(parts.port)
                                  : (parts.scheme == "https" ? INTERNET_DEFAULT_HTTPS_PORT
                                                             : INTERNET_DEFAULT_HTTP_PORT);
  attempt.connection = WinHttpConnect(g_session, wide(parts.host).c_str(), port, 0);
  if (!attempt.connection) {
    failure = "cannot reach " + parts.host;
    return false;
  }
  std::string path = parts.path.empty() ? "/" : parts.path;
  if (!parts.query.empty()) path += "?" + parts.query;
  DWORD flags = parts.scheme == "https" ? WINHTTP_FLAG_SECURE : 0;
  attempt.request = WinHttpOpenRequest(attempt.connection, L"GET", wide(path).c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!attempt.request) {
    failure = "cannot open a request";
    return false;
  }
  DWORD never = WINHTTP_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(attempt.request, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
  WinHttpSetTimeouts(attempt.request, 15000, 20000, 30000, 120000);
  std::string extra = "Accept: */*\r\nAccept-Encoding: identity\r\n";
  if (resume > 0) extra += "Range: bytes=" + std::to_string(resume) + "-\r\n";
  WinHttpAddRequestHeaders(attempt.request, wide(extra).c_str(), static_cast<DWORD>(-1),
                           WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  return true;
}

// winhttp stalls on machines whose ipv6 route to a cdn edge is dead; curl walks
// through, so a transport failure gets one more attempt through the system curl
bool curlDownload(const std::string& url, const std::string& partPath, long long resume,
                  std::string& error) {
  std::string command = "curl.exe -sS -L --fail -C - -o \"" + partPath + "\" \"" + url + "\"";
  std::string output;
  int code = runHiddenCommand(command, output);
  std::string lastLine;
  for (const std::string& line : splitLines(output)) {
    if (!trim(line).empty()) lastLine = trim(line);
  }
  std::ifstream check(partPath, std::ios::binary | std::ios::ate);
  long long size = check ? static_cast<long long>(check.tellg()) : 0;
  if (code == 0 && size > resume) return true;
  error = lastLine.empty() ? ("curl exited " + std::to_string(code)) : lastLine;
  return false;
}

void transferInner(Job* job) {
  UrlParts start = crackUrl(job->item.url);
  if (!start.valid) {
    finish(job, "failed", "not an http url");
    return;
  }

  long long resume = 0;
  {
    std::ifstream existing(job->item.partPath, std::ios::binary | std::ios::ate);
    if (existing) resume = static_cast<long long>(existing.tellg());
    if (resume < 0) resume = 0;
  }
  job->doneBytes.store(resume);
  const long long beganMs = nowMs();
  const long long beganBytes = resume;

  std::string url = job->item.url;
  int redirects = 0;

  for (;;) {
    if (job->abort.load()) {
      finish(job, "cancelled");
      return;
    }

    UrlParts parts = crackUrl(url);
    if (!parts.valid) {
      finish(job, "failed", "bad url after redirect");
      return;
    }

    Attempt attempt;
    std::string failure;
    if (!openAttempt(parts, resume, attempt, failure)) {
      finish(job, "failed", failure);
      return;
    }

    bool sent = WinHttpSendRequest(attempt.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    bool received = sent && WinHttpReceiveResponse(attempt.request, nullptr);
    DWORD status = received ? statusCodeOf(attempt.request) : 0;

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      std::string location = trim(headerOf(attempt.request, WINHTTP_QUERY_CUSTOM, "Location"));
      if (location.empty() || ++redirects > kMaxRedirects) {
        finish(job, "failed", "redirect loop on " + parts.host);
        return;
      }
      url = resolveUrl(url, location);
      continue;
    }

    if (!received) {
      std::string curlError;
      if (!job->abort.load() && curlDownload(job->item.url, job->item.partPath, resume, curlError)) {
        std::ifstream finalSize(job->item.partPath, std::ios::binary | std::ios::ate);
        if (finalSize) {
          long long bytes = static_cast<long long>(finalSize.tellg());
          job->doneBytes.store(bytes);
          job->item.totalBytes = bytes;
          job->item.doneBytes = bytes;
        }
        finish(job, "done");
        return;
      }
      finish(job, "failed", "no response from " + parts.host +
                                (curlError.empty() ? "" : " (" + curlError + ")"));
      return;
    }

    if (status == 416 && resume > 0) {
      // the host will not resume this one; throw the partial and start at zero
      DeleteFileA(job->item.partPath.c_str());
      resume = 0;
      job->doneBytes.store(0);
      continue;
    }

    if (status < 200 || status >= 300) {
      std::string host = toLowerAscii(parts.host);
      if ((status == 401 || status == 403) &&
          host.find("playstation.net") != std::string::npos) {
        finish(job, "failed",
               "PSN link expired or region-locked (403) - fetch a fresh one");
      } else {
        finish(job, "failed", "http " + std::to_string(status) + " from " + parts.host);
      }
      return;
    }

    std::string rawHeaders = headerOf(attempt.request, WINHTTP_QUERY_RAW_HEADERS_CRLF, std::string());
    if (resume > 0 && status != 206) {
      // it ignored the Range, and a whole body appended to a partial file is corruption
      DeleteFileA(job->item.partPath.c_str());
      resume = 0;
      job->doneBytes.store(0);
      continue;
    }

    long long expected = expectedOf(rawHeaders, resume);
    if (expected > 0) job->totalBytes.store(expected);

    // a share page is not the file. hosts that gate the download behind a
    // challenge answer a plain GET with their own html, and saving that gives a
    // 600 KB "game" with the real thing still on their server
    const bool html = toLowerAscii(rawHeaders).find("content-type: text/html") != std::string::npos;
    const bool small = expected == 0 || expected - resume < (1LL << 20);
    if (html && small) {
      std::string host = registrable(bareHost(job->item.url));
      finish(job, "failed",
             (host.empty() ? std::string("the host") : host) +
                 " served its web page, not the file - that host wants a browser click");
      return;
    }

    std::ofstream out(job->item.partPath, std::ios::binary | std::ios::app);
    if (!out) {
      finish(job, "failed", "cannot write " + job->item.partPath);
      return;
    }

    long long sinceTick = 0;
    long long tickMs = nowMs();
    bool dropped = false;

    for (;;) {
      if (job->abort.load()) {
        out.close();
        finish(job, "cancelled");
        return;
      }

      if (job->pause.load() || g_paused.load()) {
        out.flush();
        markPaused(job, true);
        Sleep(150);
        continue;
      }
      markPaused(job, false);

      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(attempt.request, &available)) {
        dropped = true;
        break;
      }
      if (available == 0) break;

      DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(kChunk));
      std::vector<char> buffer(toRead);
      DWORD read = 0;
      if (!WinHttpReadData(attempt.request, buffer.data(), toRead, &read) || read == 0) break;

      out.write(buffer.data(), read);
      if (!out) {
        out.close();
        finish(job, "failed", "disk write failed on " + job->item.name);
        return;
      }

      sinceTick += read;
      long long elapsed = nowMs() - tickMs;
      if (elapsed >= 250) {
        double instant = (sinceTick / 1048576.0) / (elapsed / 1000.0);
        job->speedMBs.store(job->speedMBs.load() * 0.6 + instant * 0.4);
        sinceTick = 0;
        tickMs = nowMs();
      }
      job->doneBytes.fetch_add(read);
    }

    out.close();
    job->speedMBs.store(0.0);

    if (job->abort.load()) {
      finish(job, "cancelled");
      return;
    }

    if (dropped) {
      finish(job, "failed",
             "connection dropped at " + std::to_string(job->doneBytes.load()) + " bytes");
      return;
    }

    long long total = job->totalBytes.load();
    long long done = job->doneBytes.load();
    if (total > 0 && done < total) {
      finish(job, "failed",
             "incomplete: " + std::to_string(done) + " of " + std::to_string(total) + " bytes");
      return;
    }

    if (!MoveFileExA(job->item.partPath.c_str(), job->item.path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      finish(job, "failed", "could not rename the finished file");
      return;
    }
    // the row keeps a rate after it lands: the average over the whole transfer,
    // so a two second pull still shows what it actually moved
    long long elapsedMs = nowMs() - beganMs;
    long long moved = job->doneBytes.load() - beganBytes;
    if (elapsedMs >= 250 && moved > 0) {
      job->speedMBs.store((moved / 1048576.0) / (elapsedMs / 1000.0));
    } else {
      job->speedMBs.store(0.0);
    }
    finish(job, "done");
    return;
  }
}

void transfer(Job* job) {
  transferInner(job);
  job->settled.store(true);
}

void workerLoop() {
  for (;;) {
    Job* job = nullptr;
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_wake.wait(lock, [] { return g_shutdown.load() || !g_ready.empty(); });
      if (g_shutdown.load()) return;
      std::string id = g_ready.front();
      g_ready.pop_front();
      auto found = g_jobs.find(id);
      if (found == g_jobs.end()) continue;
      job = found->second;
      if (job->abort.load()) {
        job->settled.store(true);
        continue;
      }
      job->shownPaused.store(false);
      job->item.state = "running";
    }
    transfer(job);
  }
}

void pruneThreads() {
  g_threads.erase(std::remove_if(g_threads.begin(), g_threads.end(),
                                 [](std::thread& t) { return !t.joinable(); }),
                  g_threads.end());
}

void ensureThreads(int want) {
  pruneThreads();
  int have = static_cast<int>(g_threads.size());
  for (int i = have; i < want; ++i) g_threads.emplace_back(workerLoop);
}

}  // namespace

std::vector<std::string> downloadsAdd(const std::vector<std::string>& urls,
                                      const std::string& folder,
                                      const std::vector<std::string>& names,
                                      int expectedParts) {
  std::vector<std::string> ids;
  if (folder.empty() || urls.empty()) return ids;

  std::lock_guard<std::mutex> lock(g_mutex);
  for (size_t position = 0; position < urls.size(); ++position) {
    const std::string& url = urls[position];
    if (url.empty()) continue;
    Job* job = new Job();
    job->item.id = url + "#" + std::to_string(g_order.size() + 1);
    job->item.url = url;
    std::string given = position < names.size() ? sanitize(names[position]) : std::string();
    job->item.name = given.empty() ? nameForUrl(url) : given;
    job->item.path = uniquePath(folder, job->item.name);
    job->item.partPath = job->item.path + ".part";
    job->item.state = "queued";
    job->item.startedMs = wallMs();
    g_jobs[job->item.id] = job;
    g_order.push_back(job->item.id);
    ids.push_back(job->item.id);
  }

  // A complete split-pkg set queued together merges itself when it lands.
  // With expectedParts the caller knows the set size (a row), so the count must
  // match. Pasted links carry no count: accept a run that starts at the natural
  // first index (0 for _0.pkg, 1 for .pkg.001) and is contiguous.
  {
    std::map<std::string, std::vector<std::pair<int, std::string>>> sets;
    std::map<std::string, int> naturalStart;
    for (const std::string& id : ids) {
      Job* job = g_jobs[id];
      std::string base;
      int index = 0;
      if (splitPkgPart(job->item.name, base, index)) {
        std::string key = folder + "|" + base;
        sets[key].emplace_back(index, id);
        std::string lowerName = toLowerAscii(job->item.name);
        naturalStart[key] = (lowerName.size() > 4 &&
                             lowerName.compare(lowerName.size() - 4, 4, ".pkg") == 0)
                                ? 0
                                : 1;
      }
    }
    for (auto& entry : sets) {
      std::vector<std::pair<int, std::string>>& members = entry.second;
      if (expectedParts >= 2) {
        if (static_cast<int>(members.size()) != expectedParts) continue;
      } else if (members.size() < 2) {
        continue;
      }
      std::sort(members.begin(), members.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
      bool complete = members.front().first == naturalStart[entry.first];
      for (size_t i = 0; i < members.size(); ++i) {
        if (i > 0 && members[i].first == members[i - 1].first) complete = false;
        if (members[i].first != members.front().first + static_cast<int>(i)) complete = false;
      }
      if (!complete) continue;
      std::string base = entry.first.substr(entry.first.find('|') + 1);
      MergeGroup group;
      group.label = base;
      group.output = uniquePath(folder, base);
      for (const auto& member : members) group.ids.push_back(member.second);
      g_mergeGroups.push_back(std::move(group));
    }
  }
  return ids;
}

void downloadsStart(int workers) {
  if (!ensureSession()) {
    // a session that cannot open would leave the rows queued forever and the
    // progress strip stuck at zero - fail them so the UI can say why
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& entry : g_jobs) {
      const std::string state = entry.second->item.state;
      if (state == "queued" || state == "paused") {
        entry.second->item.state = "failed";
        entry.second->item.error = "cannot open a network session";
      }
    }
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_paused.store(false);
    g_ready.clear();
    for (const std::string& id : g_order) {
      auto found = g_jobs.find(id);
      if (found == g_jobs.end()) continue;
      const std::string state = found->second->item.state;
      if (state == "queued" || state == "paused") {
        found->second->pause.store(false);
        found->second->shownPaused.store(false);
        g_ready.push_back(id);
      }
    }
  }
  ensureThreads(workers > 0 ? std::max(1, std::min(workers, 8)) : kDefaultWorkers);
  g_wake.notify_all();
}

void downloadsPause() {
  g_paused.store(true);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_ready.clear();
  for (auto& entry : g_jobs) {
    entry.second->pause.store(true);
    const std::string state = entry.second->item.state;
    if (state == "queued" || state == "running") entry.second->item.state = "paused";
  }
}

void downloadsResume() {
  g_paused.store(false);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& entry : g_jobs) {
      entry.second->pause.store(false);
      entry.second->shownPaused.store(false);
    }
  }
  downloadsStart(kDefaultWorkers);
}

void downloadsCancel(const std::string& id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto found = g_jobs.find(id);
  if (found == g_jobs.end()) return;
  found->second->abort.store(true);
  found->second->pause.store(false);
  // a running job is settled by its worker; anything else is safe to release
  if (found->second->item.state != "running") {
    found->second->item.state = "cancelled";
    found->second->settled.store(true);
  }
  for (size_t i = 0; i < g_ready.size(); ++i) {
    if (g_ready[i] == id) {
      g_ready.erase(g_ready.begin() + static_cast<long>(i));
      break;
    }
  }
}

void downloadsRemoveFinished() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<std::string> keep;
  for (const std::string& id : g_order) {
    auto found = g_jobs.find(id);
    if (found == g_jobs.end()) continue;
    const std::string state = found->second->item.state;
    if ((state == "done" || state == "cancelled" || state == "failed") &&
        found->second->settled.load()) {
      delete found->second;
      g_jobs.erase(found);
      continue;
    }
    keep.push_back(id);
  }
  g_order = keep;
}

void downloadsAbort() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& entry : g_jobs) {
      entry.second->abort.store(true);
      entry.second->pause.store(false);
    }
    g_shutdown.store(true);
    g_ready.clear();
    g_mergeGroups.clear();
    g_mergeLog.clear();
  }
  g_wake.notify_all();
  std::vector<std::thread> threads;
  threads.swap(g_threads);
  for (std::thread& thread : threads) {
    if (thread.joinable()) thread.join();
  }
  g_shutdown.store(false);
  if (g_session) {
    WinHttpCloseHandle(g_session);
    g_session = nullptr;
  }
}

std::vector<DownloadItem> downloadsSnapshot() {
  std::vector<Job*> jobs;
  std::vector<DownloadItem> out;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string& id : g_order) {
      auto found = g_jobs.find(id);
      if (found == g_jobs.end()) continue;
      jobs.push_back(found->second);
      out.push_back(found->second->item);
    }
  }
  // counters live in atomics rather than under g_mutex, so they are filled after unlocking
  for (size_t i = 0; i < out.size(); ++i) {
    out[i].totalBytes = jobs[i]->totalBytes.load();
    out[i].doneBytes = jobs[i]->doneBytes.load();
    out[i].speedMBs = jobs[i]->speedMBs.load();
  }
  return out;
}

double downloadsAggregateMBs() {
  std::lock_guard<std::mutex> lock(g_mutex);
  double total = 0;
  for (auto& entry : g_jobs) total += entry.second->speedMBs.load();
  return total;
}

size_t downloadsActive() {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t active = 0;
  for (auto& entry : g_jobs) {
    const std::string state = entry.second->item.state;
    if (state == "running" || state == "queued" || state == "paused") ++active;
  }
  return active;
}

bool downloadsBusy() { return downloadsActive() > 0 || g_merging.load() > 0; }

std::vector<std::string> downloadsTakeMergeLog() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<std::string> out;
  out.swap(g_mergeLog);
  return out;
}

std::string downloadsFolderDefault() {
  std::ifstream stream(folderFile());
  std::string line;
  if (stream && std::getline(stream, line)) {
    std::string value = trim(line);
    if (!value.empty() && GetFileAttributesA(value.c_str()) != INVALID_FILE_ATTRIBUTES) return value;
  }
  const char* profile = std::getenv("USERPROFILE");
  return profile && *profile ? std::string(profile) + "\\Downloads" : std::string(".");
}

void downloadsFolderRemember(const std::string& folder) {
  std::ofstream stream(folderFile(), std::ios::binary | std::ios::trunc);
  if (stream) stream << folder << "\n";
}
