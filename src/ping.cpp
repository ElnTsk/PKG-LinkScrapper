#include "ping.h"

#include <map>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "util.h"

namespace {

std::mutex g_pingMutex;
std::map<std::string, PingResult> g_cache;

std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);

PingResult probe(const std::string& host, int port, int timeoutMs) {
  PingResult result;

  addrinfoW hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfoW* addresses = nullptr;
  int status = GetAddrInfoW(wide(host).c_str(), std::to_wstring(port).c_str(), &hints, &addresses);
  if (status != 0) {
    result.detail = "dns failed (" + std::to_string(status) + ")";
    if (addresses) FreeAddrInfoW(addresses);
    return result;
  }
  if (!addresses) {
    result.detail = "dns returned no addresses";
    return result;
  }

  SOCKET best = INVALID_SOCKET;
  sockaddr* name = nullptr;
  for (addrinfoW* candidate = addresses; candidate; candidate = candidate->ai_next) {
    SOCKET socket = WSASocketW(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol,
                               nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET) continue;

    u_long nonBlocking = 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);

    long connect = ::connect(socket, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen));
    if (connect == 0) {
      best = socket;
      name = candidate->ai_addr;
      break;
    }

    int last = WSAGetLastError();
    if (last != WSAEWOULDBLOCK) {
      closesocket(socket);
      continue;
    }

    fd_set writes;
    FD_ZERO(&writes);
    FD_SET(socket, &writes);
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    long started = nowMs();
    int ready = select(0, nullptr, &writes, nullptr, &timeout);
    if (ready > 0) {
      int error = 0;
      int length = sizeof(error);
      getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
      if (error == 0) {
        result.latencyMs = static_cast<int>(nowMs() - started);
        best = socket;
        name = candidate->ai_addr;
        break;
      }
    }
    closesocket(socket);
  }

  if (best == INVALID_SOCKET) {
    result.detail = "no answer on port " + std::to_string(port);
    FreeAddrInfoW(addresses);
    return result;
  }

  wchar_t endpoint[NI_MAXHOST] = L"";
  GetNameInfoW(name, static_cast<int>(name->sa_family == AF_INET6 ? sizeof(sockaddr_in6)
                                                                 : sizeof(sockaddr_in)),
               endpoint, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
  result.reachable = true;
  if (result.latencyMs == 0) result.latencyMs = 1;
  result.detail = "answered from " + utf8(std::wstring(endpoint));

  // the socket is only there to time the handshake; say goodbye properly
  shutdown(best, SD_BOTH);
  closesocket(best);
  FreeAddrInfoW(addresses);
  return result;
}

std::string winsockError() {
  char buffer[256];
  DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                WSAGetLastError(), 0, buffer, sizeof(buffer), nullptr);
  if (length == 0) return "";
  return collapseSpaces(trim(std::string(buffer, length)));
}

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

bool ensureWinsock() {
  static bool started = [] {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return started;
}

}  // namespace

PingResult pingHost(const std::string& host, int port, int timeoutMs) {
  PingResult result;
  if (host.empty()) {
    result.detail = "no host";
    return result;
  }
  if (!ensureWinsock()) {
    result.detail = "winsock unavailable";
    return result;
  }
  int chosenPort = port > 0 ? port : 443;
  int budget = timeoutMs > 0 ? timeoutMs : 1500;
  result = probe(host, chosenPort, budget);
  if (!result.reachable && chosenPort == 443) {
    // some mirrors serve plain http only, so a refused 443 is not a dead host
    PingResult fallback = probe(host, 80, budget);
    if (fallback.reachable) return fallback;
    if (result.detail.empty()) result.detail = fallback.detail;
  }
  if (!result.reachable && result.detail.empty()) {
    std::string error = winsockError();
    result.detail = error.empty() ? "no route" : error;
  }
  return result;
}

PingResult pingCached(const std::string& host, int port, int timeoutMs) {
  std::string key = toLowerAscii(host) + ":" + std::to_string(port > 0 ? port : 443);
  {
    std::lock_guard<std::mutex> lock(g_pingMutex);
    auto found = g_cache.find(key);
    if (found != g_cache.end()) return found->second;
  }

  PingResult result = pingHost(host, port, timeoutMs);
  std::lock_guard<std::mutex> lock(g_pingMutex);
  if (g_cache.size() < 512) g_cache[key] = result;
  return result;
}

void clearPingCache() {
  std::lock_guard<std::mutex> lock(g_pingMutex);
  g_cache.clear();
}

std::string pingLabel(const PingResult& result) {
  if (result.reachable) return std::to_string(result.latencyMs) + "ms";
  if (result.detail.empty() || result.detail == "no host") return std::string();
  return "no route";
}
