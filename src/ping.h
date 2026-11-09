#pragma once

#include <string>

// A ping that needs no admin rights: open a TCP socket to the host's port and
// time how long connect takes. ICMP is firewalled on most file hosts, so a
// refused-or-timed-out echo proves nothing while a TCP answer proves the host
// is alive. Results are cached per host, so a 21-part set costs one probe.
struct PingResult {
  bool reachable = false;
  int latencyMs = 0;
  std::string detail;
};

// port 0 picks 443 for https and 80 for http. timeoutMs <= 0 uses 1500.
PingResult pingHost(const std::string& host, int port = 0, int timeoutMs = 0);

// the same call through the per-host cache
PingResult pingCached(const std::string& host, int port, int timeoutMs = 0);

void clearPingCache();

// text for the grid: "23ms", "no route", or "" when untested
std::string pingLabel(const PingResult& result);
