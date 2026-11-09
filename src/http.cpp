#include "http.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <memory>
#include <sstream>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include "util.h"

#ifndef WINHTTP_REDIRECT_POLICY_MAXOFFORTOACCESSDENIED
#define WINHTTP_REDIRECT_POLICY_MAXOFFORTOACCESSDENIED 0x00000002
#endif
#ifndef WINHTTP_REDIRECT_POLICY_MAX
#define WINHTTP_REDIRECT_POLICY_MAX 0x00000008
#endif

namespace {

// undocumented in the headers, but supported since WinHTTP 6.1: setting it to
// TRUE makes every pending call on that handle fail immediately
constexpr DWORD kOptionCancel = 0x00000018;

constexpr size_t kMaxBody = 3u << 20;
constexpr size_t kMaxCached = 24;

HINTERNET g_session = nullptr;
std::string g_userAgent;
int g_timeoutMs = 15000;
int g_retries = 2;
int g_minHostDelayMs = 300;
std::unique_ptr<CountingSemaphore> g_semaphore;
std::mutex g_hostMutex;
std::map<std::string, int64_t> g_hostLast;
std::mutex g_cacheMutex;
std::map<std::string, HttpResponse> g_cache;
std::vector<std::string> g_cacheOrder;

std::mutex g_activeMutex;

struct ActiveEntry {
  HINTERNET handle = nullptr;
  bool closed = false;
};

std::vector<ActiveEntry*> g_active;

void awaitBackoff(int64_t ms) {
  static std::mt19937 rng(static_cast<unsigned>(nowMs()));
  int64_t total = ms + static_cast<int>(rng() % 250);
  while (total > 0 && !abortRequested()) {
    const int64_t slice = total > 100 ? 100 : total;
    std::this_thread::sleep_for(std::chrono::milliseconds(slice));
    total -= slice;
  }
}

struct ActiveGuard {
  explicit ActiveGuard(HINTERNET handle) : entry_(new ActiveEntry{handle, false}) {
    std::lock_guard<std::mutex> lock(g_activeMutex);
    g_active.push_back(entry_);
  }
  ~ActiveGuard() {
    std::lock_guard<std::mutex> lock(g_activeMutex);
    if (!entry_->closed) {
      WinHttpCloseHandle(entry_->handle);
      entry_->closed = true;
    }
    g_active.erase(std::remove(g_active.begin(), g_active.end(), entry_), g_active.end());
    delete entry_;
  }
  ActiveEntry* entry_;
};

std::wstring toWide(const std::string& value) {
  if (value.empty()) return std::wstring();
  int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
  return out;
}

std::string toUtf8(const std::wstring& value) {
  if (value.empty()) return std::string();
  int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                  nullptr, nullptr);
  std::string out(static_cast<size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count,
                      nullptr, nullptr);
  return out;
}

std::string errorText(DWORD code) {
  char buffer[256];
  DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                code, 0, buffer, sizeof(buffer), nullptr);
  std::string message(buffer, length ? buffer + length : buffer);
  message = trim(message);
  if (message.empty()) message = "winhttp error";
  return message + " (" + std::to_string(code) + ")";
}

void throttleHost(const std::string& host) {
  if (abortRequested()) return;
  int64_t wait = 0;
  {
    std::lock_guard<std::mutex> lock(g_hostMutex);
    int64_t now = nowMs();
    int64_t& last = g_hostLast[host];
    if (last + g_minHostDelayMs > now) wait = last + g_minHostDelayMs - now;
    last = now + wait;
  }
  while (wait > 0 && !abortRequested()) {
    const int64_t slice = wait > 120 ? 120 : wait;
    std::this_thread::sleep_for(std::chrono::milliseconds(slice));
    wait -= slice;
  }
}

void cacheStore(const std::string& key, const HttpResponse& response) {
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  if (g_cache.size() >= kMaxCached) {
    std::string oldest = g_cacheOrder.front();
    g_cacheOrder.erase(g_cacheOrder.begin());
    g_cache.erase(oldest);
  }
  g_cache[key] = response;
  g_cacheOrder.push_back(key);
}

bool cacheFind(const std::string& key, HttpResponse& out) {
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  auto found = g_cache.find(key);
  if (found == g_cache.end()) return false;
  out = found->second;
  return true;
}

HttpResponse perform(const UrlParts& parts,
                     const std::string& url,
                     const std::string& verb,
                     const std::vector<std::string>& extraHeaders,
                     bool followRedirects,
                     int timeoutMs,
                     const std::string& body = std::string(),
                     size_t maxBodyBytes = 0) {
  const size_t bodyCap = maxBodyBytes ? maxBodyBytes : kMaxBody;
  HttpResponse response;
  response.finalUrl = url;

  HINTERNET connection = g_session;
  if (!connection) {
    response.error = "http not initialised";
    return response;
  }

  INTERNET_PORT port = static_cast<INTERNET_PORT>(parts.port);
  HINTERNET connect = WinHttpConnect(connection, toWide(parts.host).c_str(), port, 0);
  if (!connect) {
    response.error = errorText(GetLastError());
    return response;
  }

  std::wstring path = toWide(parts.path.empty() ? "/" : parts.path);
  if (!parts.query.empty()) path += L"?" + toWide(parts.query);

  bool secure = parts.scheme == "https";
  HINTERNET request = WinHttpOpenRequest(connect, toWide(verb).c_str(), path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request) {
    response.error = errorText(GetLastError());
    WinHttpCloseHandle(connect);
    return response;
  }

  WinHttpSetTimeouts(request, 4000, 5000, 6000, timeoutMs);
  ActiveGuard active(request);

  DWORD policy = followRedirects ? WINHTTP_REDIRECT_POLICY_MAX
                                 : WINHTTP_REDIRECT_POLICY_MAXOFFORTOACCESSDENIED;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

  std::string headers = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                        "Accept-Language: en-US,en;q=0.9\r\n";
  for (const std::string& header : extraHeaders) headers += header + "\r\n";
  WinHttpAddRequestHeaders(request, toWide(headers).c_str(), static_cast<DWORD>(-1),
                           WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

  // a form body is sent as the request entity; WinHTTP needs the length twice
  BOOL sent = FALSE;
  if (body.empty()) {
    sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                              0);
  } else {
    sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                              static_cast<DWORD>(body.size()), 0);
  }

  if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
    response.error = errorText(GetLastError());
    WinHttpCloseHandle(connect);
    return response;
  }

  DWORD status = 0;
  DWORD size = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  response.status = status;

  DWORD urlSize = 0;
  WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &urlSize);
  if (urlSize > 1) {
    std::wstring wide(urlSize / sizeof(wchar_t) + 1, L'\0');
    if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, wide.data(), &urlSize)) {
      // the size winhttp reports back counts bytes, characters or neither
      // depending on the option; measure the string instead of trusting it
      wide.resize(wcsnlen(wide.c_str(), wide.size()));
      response.finalUrl = toUtf8(wide);
    }
  }

  std::wstring headerBlock;
  DWORD headerSize = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                      &headerSize, WINHTTP_NO_HEADER_INDEX);
  if (headerSize > 2) {
    headerBlock.resize(headerSize / sizeof(wchar_t));
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            headerBlock.data(), &headerSize, WINHTTP_NO_HEADER_INDEX)) {
      headerBlock.resize(headerSize / sizeof(wchar_t) - 1);
      for (const std::string& line : splitLines(toUtf8(headerBlock))) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = toLowerAscii(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (!name.empty()) response.headers[name] = value;
      }
    }
  }

  if (verb != "HEAD") {
    for (;;) {
      if (abortRequested()) break;
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available)) break;
      if (available == 0) break;
      DWORD chunk = available;
      if (response.body.size() + chunk > bodyCap) {
        chunk = static_cast<DWORD>(bodyCap - response.body.size());
        if (chunk == 0) break;
      }
      std::string buffer(chunk, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, buffer.data(), chunk, &read)) break;
      response.body.append(buffer.data(), read);
      if (response.body.size() >= bodyCap) break;
    }
  }

  WinHttpCloseHandle(connect);
  return response;
}

}  // namespace

std::string HttpResponse::header(const std::string& lowerName) const {
  auto found = headers.find(lowerName);
  return found == headers.end() ? std::string() : found->second;
}

void httpInit(const std::string& userAgent, int concurrency, int timeoutMs, int retries,
              int minHostDelayMs) {
  g_userAgent = userAgent;
  g_timeoutMs = timeoutMs;
  g_retries = retries;
  g_minHostDelayMs = minHostDelayMs;
  g_semaphore = std::make_unique<CountingSemaphore>(std::max(1, concurrency));
  if (!g_session) {
    g_session = WinHttpOpen(toWide(g_userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
}

bool httpReady() { return g_session != nullptr && g_semaphore != nullptr; }

void httpShutdown() {
  g_semaphore.reset();
  if (g_session) {
    WinHttpCloseHandle(g_session);
    g_session = nullptr;
  }
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  g_cache.clear();
  g_cacheOrder.clear();
}

void httpCancelAll() {
  DWORD value = 1;
  {
    std::lock_guard<std::mutex> lock(g_activeMutex);
    for (ActiveEntry* entry : g_active) {
      if (entry->closed) continue;
      // closing a handle another thread is using is not supported; this option is
      // what actually interrupts a synchronous send or read
      WinHttpSetOption(entry->handle, kOptionCancel, &value, sizeof(value));
    }
  }
  // the session covers anything that had not registered a request yet
  if (g_session) WinHttpSetOption(g_session, kOptionCancel, &value, sizeof(value));
}

HttpResponse httpRequest(const std::string& url,
                         const std::string& verb,
                         const std::vector<std::string>& extraHeaders,
                         bool followRedirects,
                         bool useCache,
                         int timeoutOverrideMs,
                         const std::string& body,
                         size_t maxBodyBytes) {
  if (!httpReady()) {
    HttpResponse response;
    response.error = "http not initialised";
    response.finalUrl = url;
    return response;
  }

  // a request that starts after Stop must not sit out its timeout
  if (abortRequested()) {
    HttpResponse response;
    response.error = "aborted";
    response.finalUrl = url;
    return response;
  }

  std::string cacheKey = verb + " " + url + (followRedirects ? " f" : " m");
  HttpResponse cached;
  if (useCache && verb == "GET" && body.empty() && cacheFind(cacheKey, cached)) return cached;

  UrlParts parts = crackUrl(url);
  if (!parts.valid) {
    HttpResponse response;
    response.error = "not an http url";
    response.finalUrl = url;
    return response;
  }

  std::vector<std::string> headers = extraHeaders;
  headers.push_back("User-Agent: " + g_userAgent);

  int timeoutMs = timeoutOverrideMs > 0 ? timeoutOverrideMs : g_timeoutMs;
  std::string lastError;

  g_semaphore->acquire();
  struct Guard {
    ~Guard() { g_semaphore->release(); }
  } guard;

  for (int attempt = 0; attempt <= g_retries; ++attempt) {
    throttleHost(parts.host);
    HttpResponse response = perform(parts, url, verb, headers, followRedirects, timeoutMs, body,
                                    maxBodyBytes);

    if (response.status == 0) {
      lastError = response.error.empty() ? "request failed" : response.error;
    } else if (response.status == 429 || response.status >= 500) {
      lastError = "http " + std::to_string(response.status);
    } else if (response.status == 202) {
      // duckduckgo bot-check page: retrying only wastes the budget
      return response;
    } else {
      if (useCache && verb == "GET" && response.status == 200) cacheStore(cacheKey, response);
      return response;
    }

    if (abortRequested() || attempt >= g_retries) break;
    awaitBackoff(400 * (1 << attempt));
  }

  HttpResponse failure;
  failure.error = lastError.empty() ? "request failed" : lastError;
  failure.finalUrl = url;
  return failure;
}

HttpResponse httpGet(const std::string& url, bool useCache) {
  return httpRequest(url, "GET", std::vector<std::string>{}, true, useCache);
}

HttpResponse httpGetCatalog(const std::string& url) {
  HttpResponse response =
      httpRequest(url, "GET", std::vector<std::string>{}, true, true, 12000, std::string(),
                  12u << 20);
  if (response.status != 0) return response;

  // a machine with a half-broken IPv6 route makes winhttp time out on cloudflare
  // fronts while curl walks straight through; the system curl is the second way
  // in for the one download that matters most
  unsigned int hash = 2166136261u;
  for (unsigned char ch : url) {
    hash ^= ch;
    hash *= 16777619u;
  }
  char tempDir[MAX_PATH] = "";
  GetTempPathA(MAX_PATH, tempDir);
  std::string outPath = std::string(tempDir) + "pkg-ls-" +
                        std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) + "-" +
                        std::to_string(hash & 0xffffffu) + ".tmp";
  std::string command = "curl.exe -sS -L --max-time 60 -A \"" + g_userAgent + "\" -w \"%{http_code}\" -o \"" +
                        outPath + "\" \"" + url + "\"";
  std::string curlOut;
  int curlCode = runHiddenCommand(command, curlOut);
  if (curlCode == 0) response.status = std::strtol(trim(curlOut).c_str(), nullptr, 10);

  std::ifstream stream(outPath, std::ios::binary);
  if (stream) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    response.body = buffer.str();
    if (response.body.size() > (12u << 20)) response.body.resize(12u << 20);
    stream.close();
    DeleteFileA(outPath.c_str());
  }
  if (response.status == 200 && !response.body.empty()) {
    response.finalUrl = url;
    logDebug("catalog fetched through curl " + url);
  } else if (response.status == 0) {
    response.error = "curl fallback failed";
  }
  return response;
}

HttpResponse httpHeadWithRange(const std::string& url) {
  return httpRequest(url, "HEAD", std::vector<std::string>{"Range: bytes=0-0"}, true, false);
}

HttpResponse httpGetRangeNoFollow(const std::string& url) {
  return httpRequest(url, "GET", std::vector<std::string>{"Range: bytes=0-0"}, false, false);
}

HttpResponse httpPostForm(const std::string& url, const std::string& form, const std::string& referer) {
  std::vector<std::string> headers;
  headers.push_back("Content-Type: application/x-www-form-urlencoded");
  UrlParts parts = crackUrl(url);
  if (parts.valid) {
    // the origin a browser would send, which several hosts insist on
    std::string origin = parts.scheme + "://" + parts.host;
    if (parts.port) origin += ":" + std::to_string(parts.port);
    headers.push_back("Origin: " + origin);
  }
  headers.push_back("Referer: " + (referer.empty() ? url : referer));
  return httpRequest(url, "POST", headers, true, false, 0, form);
}

int runHiddenCommand(const std::string& commandLine, std::string& output) {
  output.clear();
  SECURITY_ATTRIBUTES attrs{};
  attrs.nLength = sizeof(attrs);
  attrs.bInheritHandle = TRUE;
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &attrs, 0)) return -1;
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA info{};
  info.cb = sizeof(info);
  info.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  info.wShowWindow = SW_HIDE;
  info.hStdOutput = writePipe;
  info.hStdError = writePipe;
  info.hStdInput = nullptr;

  PROCESS_INFORMATION child{};
  std::string mutableCommand = commandLine;
  // the console window the shell would otherwise flash lives and dies here
  BOOL started = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &info, &child);
  CloseHandle(writePipe);
  if (!started) {
    CloseHandle(readPipe);
    return -1;
  }
  char buffer[1024];
  DWORD got = 0;
  while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &got, nullptr) && got > 0) {
    buffer[got] = '\0';
    output += buffer;
  }
  WaitForSingleObject(child.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(child.hProcess, &code);
  CloseHandle(child.hProcess);
  CloseHandle(child.hThread);
  CloseHandle(readPipe);
  return static_cast<int>(code);
}

HttpResponse httpPostJson(const std::string& url, const std::string& json) {
  std::vector<std::string> headers;
  headers.push_back("Content-Type: application/json");
  return httpRequest(url, "POST", headers, true, false, 0, json);
}
