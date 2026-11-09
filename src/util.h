#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

std::string trim(const std::string& value);
std::string toLowerAscii(std::string value);
std::string toUpperAscii(std::string value);
bool iequals(const std::string& a, const std::string& b);
bool startsWithI(const std::string& value, const std::string& prefix);
std::string replaceAll(std::string value, const std::string& from, const std::string& to);
std::vector<std::string> splitLines(const std::string& value);
std::string joinValues(const std::vector<std::string>& parts, const std::string& sep);

std::string transliterate(const std::string& value);
std::string normalizeText(const std::string& value);
std::vector<std::string> tokensOf(const std::string& normalized);
std::string collapseSpaces(const std::string& value);

extern bool gVerbose;
void logInfo(const std::string& line);
void logDebug(const std::string& line);

// level 0 = progress line, 1 = verbose detail
typedef void (*LogSink)(const char* line, int level);
void setLogSink(LogSink sink);

bool abortRequested();
void requestAbort();
void resetAbort();

std::string percentEncode(const std::string& value);
std::string percentDecode(const std::string& value);
std::string decodeEntities(const std::string& value);

struct UrlParts {
  bool valid = false;
  std::string scheme;
  std::string host;
  int port = 0;
  std::string path;
  std::string query;
  std::string fragment;
};

UrlParts crackUrl(const std::string& url);
std::string resolveUrl(const std::string& base, const std::string& reference);
std::string normalizeUrl(const std::string& url);
std::string hostOf(const std::string& url);
std::string registrable(const std::string& host);

std::string formatBytes(int64_t bytes);
std::string truncateTo(const std::string& value, size_t width);
std::string htmlUnescapeForUrl(const std::string& value);

std::string queryParam(const std::string& query, const std::string& name);
std::string base64UrlDecode(const std::string& input);
// accepts both alphabets (A-Za-z0-9+/= and -_) and ignores whitespace
std::string base64DecodeLoose(const std::string& input);
bool looksBase64(const std::string& value);
std::string unwrapBingHref(const std::string& href);
std::string unwrapDuckDuckGoHref(const std::string& href);

class CountingSemaphore {
 public:
  explicit CountingSemaphore(int permits);
  void acquire();
  void release();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int permits_;
};

class ThreadPool {
 public:
  explicit ThreadPool(int threads);
  ~ThreadPool();
  void submit(std::function<void()> job);
  // deadlineMs <= 0 waits until everything really is idle
  void waitForIdle(long long deadlineMs = 0);
  bool timedOut() const { return timedOut_; }

 private:
  struct JobDone {
    ThreadPool* pool;
    ~JobDone();
  };

  void workerLoop();

  std::vector<std::thread> workers_;
  std::deque<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable workReady_;
  std::condition_variable allIdle_;
  bool stop_ = false;
  bool timedOut_ = false;
  int outstanding_ = 0;
};

int64_t nowMs();

// Wall-clock milliseconds since the unix epoch. Use this for anything written
// to disk: nowMs is monotonic and restarts at zero with every boot.
int64_t wallMs();
