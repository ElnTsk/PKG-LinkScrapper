#include "verify.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

#include "deadstore.h"
#include "http.h"
#include "hosts.h"
#include "match.h"
#include "ping.h"
#include "trust.h"

namespace {

struct Probe {
  std::string state;
  std::string error;
  long status = 0;
  long long sizeBytes = 0;
  long long takenMs = 0;
};

std::mutex g_probeMutex;
std::map<std::string, Probe> g_probes;
constexpr long long kProbeFreshMs = 10LL * 60 * 1000;

std::string stateFor(long status) {
  if (status == 0) return "unreachable";
  if (status >= 200 && status < 400) return "alive";
  if (status == 401 || status == 403 || status == 451) return "blocked";
  if (status == 404 || status == 410) return "dead";
  if (status == 429) return "rate-limited";
  return "http-" + std::to_string(status);
}

struct FrontPage {
  long long length = 0;
  unsigned long long hash = 0;
};

unsigned long long fingerprint(const std::string& text) {
  unsigned long long hash = 1469598103934665603ULL;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::mutex g_frontMutex;
std::map<std::string, FrontPage> g_fronts;

// javascript-only hosts fetch the file list after the page loads, so every
// share url - live or long deleted - returns the same bytes as the homepage
FrontPage frontPageOf(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return FrontPage{};
  const std::string key = parts.scheme + "://" + parts.host + "/";
  {
    std::lock_guard<std::mutex> lock(g_frontMutex);
    auto found = g_fronts.find(key);
    if (found != g_fronts.end()) return found->second;
  }

  FrontPage page;
  HttpResponse root = httpRequest(key, "GET", std::vector<std::string>{}, true, false);
  if (root.status >= 200 && root.status < 400 && !root.body.empty()) {
    page.length = static_cast<long long>(root.body.size());
    page.hash = fingerprint(root.body);
  }
  std::lock_guard<std::mutex> lock(g_frontMutex);
  g_fronts[key] = page;
  return page;
}

bool servesOnlyFrontPage(const std::string& url, const std::string& body) {
  if (body.empty() || body.size() > 65536) return false;
  FrontPage page = frontPageOf(url);
  return page.length == static_cast<long long>(body.size()) && page.hash == fingerprint(body);
}

// Some hosts name the file and its size on the share page, then hide the real
// url behind a challenge that only a browser can answer. The file is there;
// the link to it is not something a probe can collect.
bool carriesCaptchaMarker(const std::string& body) {
  std::string lower = toLowerAscii(body);
  return lower.find("turnstile") != std::string::npos ||
         lower.find("challenges.cloudflare") != std::string::npos ||
         lower.find("recaptcha") != std::string::npos ||
         lower.find("hcaptcha") != std::string::npos;
}

// A page that carries the host's own download form is one step from the file.
// Walking it costs a one-time link, so it happens on a click, not on a probe.
bool offersDownloadForm(const std::string& body) {
  std::string lower = toLowerAscii(body);
  if (lower.find("<form") == std::string::npos) return false;
  if (lower.find("method=\"post\"") == std::string::npos &&
      lower.find("method='post'") == std::string::npos) {
    return false;
  }
  return lower.find("download") != std::string::npos;
}

std::mutex g_gateMutex;
std::map<std::string, bool> g_gates;

// the marker sits at the foot of the document, past the 32 KB the size read
// already took, so ask the host for its last bytes once per domain
bool gatedByCaptcha(const std::string& url, const std::string& head) {
  if (carriesCaptchaMarker(head)) return true;
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return false;
  const std::string host = registrable(parts.host);
  {
    std::lock_guard<std::mutex> lock(g_gateMutex);
    auto found = g_gates.find(host);
    if (found != g_gates.end()) return found->second;
  }

  bool gated = false;
  if (!abortRequested()) {
    HttpResponse tail = httpRequest(url, "GET", std::vector<std::string>{"Range: bytes=-16384"}, true,
                                    false);
    if (tail.status > 0 && !tail.body.empty()) gated = carriesCaptchaMarker(tail.body);
  }
  std::lock_guard<std::mutex> lock(g_gateMutex);
  g_gates[host] = gated;
  return gated;
}

long long contentLengthOf(const HttpResponse& response) {
  std::string range = response.header("content-range");
  if (!range.empty()) {
    size_t slash = range.find_last_of('/');
    if (slash != std::string::npos) {
      long long total = std::strtoll(range.c_str() + slash + 1, nullptr, 10);
      if (total > 0) return total;
    }
  }
  std::string length = response.header("content-length");
  if (!length.empty()) {
    for (char c : length) {
      if (c < '0' || c > '9') return 0;
    }
    long long value = std::strtoll(length.c_str(), nullptr, 10);
    return value > 1 ? value : 0;
  }
  return 0;
}

// share pages that refuse to answer a ranged probe still print the size in the
// html, so read it out of the body instead of leaving the column blank
long long sizeFromHtml(const std::string& html) {
  static const struct {
    const char* unit;
    long long multiplier;
  } units[] = {{"tb", 1024LL * 1024 * 1024 * 1024}, {"gb", 1024LL * 1024 * 1024},
               {"mb", 1024LL * 1024},               {"kb", 1024LL},
               {"giB", 0},                          {"miB", 0}};

  std::string lower = toLowerAscii(html);
  size_t cursor = 0;
  while ((cursor = lower.find("size", cursor)) != std::string::npos) {
    size_t windowEnd = std::min(lower.size(), cursor + 80);
    std::string window = lower.substr(cursor, windowEnd - cursor);
    size_t scan = 0;
    while (scan < window.size()) {
      char c = window[scan];
      bool digit = (c >= '0' && c <= '9') || c == '.';
      if (!digit) {
        ++scan;
        continue;
      }
      size_t start = scan;
      bool dot = false;
      while (scan < window.size() && ((window[scan] >= '0' && window[scan] <= '9') ||
                                      (window[scan] == '.' && !dot))) {
        if (window[scan] == '.') dot = true;
        ++scan;
      }
      std::string number = window.substr(start, scan - start);
      size_t after = scan;
      while (after < window.size() && window[after] == ' ') ++after;
      for (const auto& entry : units) {
        if (entry.multiplier == 0) continue;
        size_t length = std::strlen(entry.unit);
        if (window.compare(after, length, entry.unit) != 0) continue;
        double value = std::strtod(number.c_str(), nullptr);
        // "size 1.000.000" style garbage and page furniture both get dropped
        if (value <= 0 || value > 100000) break;
        return static_cast<long long>(value * static_cast<double>(entry.multiplier));
      }
      ++scan;
    }
    cursor += 4;
  }
  return 0;
}

bool goneFromHtml(const std::string& html) {
  static const char* markers[] = {"file not found",        "file does not exist",
                                  "no longer available",   "link has expired",
                                  "link is expired",       "this link is invalid",
                                  "file was deleted",      "file no longer exists",
                                  "the file you are looking for", "mirror is down",
                                  "archive has been removed"};
  std::string lower = toLowerAscii(html);
  for (const char* marker : markers) {
    if (lower.find(marker) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

void verifyHit(Hit& hit) {
  if (abortRequested()) {
    hit.state = "skipped";
    return;
  }
  if (hit.url.empty()) {
    hit.state = "unreachable";
    return;
  }

  // a url this tool has already seen serve bytes, inside its window, is not
  // worth probing twice - but "bytes" means the file, not a page that claims a
  // size, and the hosts that hand out pages are never taken on trust
  const bool pageStyle = shareServesPage(bareHost(hit.url));
  const GoodEntry* known = lookupGood(hit.url);
  if (known && !pageStyle && known->sizeBytes >= (1LL << 20)) {
    hit.state = "alive";
    hit.http = 200;
    hit.trust = "recommended";
    if (known->sizeBytes > 0) {
      hit.sizeBytes = known->sizeBytes;
      hit.size = formatBytes(known->sizeBytes);
    }
    logDebug("verify: cached alive " + hit.url);
    return;
  }

  HttpResponse response = httpHeadWithRange(hit.url);
  if (response.status == 0 || response.status == 405 || response.status == 501 ||
      (response.status == 403 && hit.kind != "page")) {
    // several hosts answer a HEAD with 403 and serve the bytes to a ranged GET
    HttpResponse fallback =
        httpRequest(hit.url, "GET", std::vector<std::string>{"Range: bytes=0-1"}, true, false);
    if (fallback.status) response = fallback;
  }

  hit.http = static_cast<int>(response.status);
  std::string transportError = response.error;
  if (!transportError.empty()) hit.error = transportError;

  if (hit.kind != "page" && hit.kind != "container") {
    long long bytes = contentLengthOf(response);
    if (bytes > 0) {
      hit.sizeBytes = bytes;
      hit.size = formatBytes(bytes);
    }
  }

  if (hit.kind == "container" && response.status >= 200 && response.status < 400) {
    hit.state = "container";
  } else {
    hit.state = stateFor(response.status);
  }

  // a url that names a file must actually serve bytes. page-style hosts
  // (1fichier, mega, gofile) legitimately answer with html, so they are not
  // judged here
  const bool html =
      toLowerAscii(response.header("content-type")).find("text/html") != std::string::npos;
  const bool tiny = hit.sizeBytes == 0 || hit.sizeBytes < (1LL << 20);
  const bool sharePage = hit.state == "alive" && hit.kind != "page" && hit.kind != "container";

  if (sharePage && html && tiny && hasFileExtension(crackUrl(hit.url).path)) {
    hit.state = "stub";
    hit.error = transportError.empty() ? "url names a file but the host serves a page" : transportError;
  }

  // a share that answers with html has to be read whatever it claims about its
  // length: vikingfile heads the page with the file's 162 GB and answers a GET
  // with 600 KB of its own markup. The size, the gone notice, the front-page
  // shell and the captcha gate all live in that body.
  std::string body;
  if (sharePage && (html || tiny || pageStyle) && !abortRequested()) {
    HttpResponse page =
        httpRequest(hit.url, "GET", std::vector<std::string>{"Range: bytes=0-32767"}, true, false);
    if (page.ok()) body = page.body;
  }

  if (hit.state == "alive" && !body.empty()) {
    long long bytes = sizeFromHtml(body);
    if (bytes > 0) {
      hit.sizeBytes = bytes;
      hit.size = formatBytes(bytes);
    }
    if (goneFromHtml(body)) {
      hit.state = "dead";
      hit.error = "the page says the file is gone";
    } else if (gatedByCaptcha(hit.url, body)) {
      // the host knows the file and its size, and will not hand over the real
      // url until a human solves a challenge - the page is not the download
      hit.state = "gated";
      hit.error =
          "the file sits behind a captcha - open the link and solve it; this page is not the download";
      std::string ignored;
      forgetGood(hit.url, ignored);
    } else if (offersDownloadForm(body)) {
      // one more step of the host's own making. the link is single use and the
      // free tier queues it, so it is walked when Download is pressed, not here
      hit.state = "gated";
      hit.error = "the host hands the file through its own download page - Download walks that step";
    } else if (servesOnlyFrontPage(hit.url, body)) {
      hit.state = "shell";
      hit.error =
          "the host serves its front page for every link - the file list is built by javascript";
      std::string ignored;
      forgetGood(hit.url, ignored);
    }
  }

  // whatever just served bytes is a recommendation from now on; a url that
  // turns out to be a stub, a 404, a front page or a captcha gate loses it
  if (hit.state == "alive") hit.trust = "recommended";
  else if (hit.state == "stub" || hit.state == "gated" || hit.state == "shell" ||
           hit.state == "dead" || hit.state == "unreachable")
    hit.trust.clear();

  // a row that the http probe could not settle gets a reachability answer: the
  // host being up while the file is not is a different problem from a dead
  // mirror, and it is the one that used to leave rows reading unverified
  const bool undecided = hit.state == "unverified" || hit.state == "unreachable" ||
                         hit.state == "skipped" || hit.state == "stub" ||
                         (hit.state == "alive" && hit.sizeBytes == 0);
  if (undecided && !abortRequested() && !hit.host.empty()) {
    int port = 443;
    UrlParts parts = crackUrl(hit.url);
    if (parts.valid && parts.scheme == "http") port = 80;
    PingResult ping = pingCached(hit.host, port);
    hit.pingMs = ping.reachable ? ping.latencyMs : 0;
    hit.pingNote = ping.detail;
    if (!ping.reachable && hit.state != "stub") {
      hit.state = "unreachable";
      if (hit.error.empty()) hit.error = "host does not answer on port " + std::to_string(port);
    } else if (ping.reachable && (hit.state == "unverified" || hit.state == "skipped")) {
      hit.error = "host answers in " + std::to_string(ping.latencyMs) +
                  "ms but the file probe did not complete";
    }
  }
}

void verifyAll(std::vector<Hit>& hits, ThreadPool& pool) {
  for (Hit& hit : hits) {
    Hit* pointer = &hit;
    pool.submit([pointer] {
      std::string key = normalizeUrl(pointer->url) + "|" + pointer->kind;
      {
        std::lock_guard<std::mutex> lock(g_probeMutex);
        auto found = g_probes.find(key);
        // the same file host answering for two titles is one probe, not two
        if (found != g_probes.end() && found->second.takenMs > wallMs() - kProbeFreshMs) {
          const Probe& cached = found->second;
          pointer->state = cached.state;
          pointer->http = static_cast<int>(cached.status);
          pointer->sizeBytes = cached.sizeBytes;
          pointer->size = cached.sizeBytes > 0 ? formatBytes(cached.sizeBytes) : pointer->size;
          if (!cached.error.empty() && pointer->error.empty()) pointer->error = cached.error;
          pointer->trust = cached.state == "alive" ? "recommended" : pointer->trust;
          if (cached.state == "stub" || cached.state == "dead" || cached.state == "unreachable") {
            pointer->trust.clear();
          }
          logDebug("verify: memo " + pointer->url);
          return;
        }
      }

      verifyHit(*pointer);

      std::lock_guard<std::mutex> lock(g_probeMutex);
      if (g_probes.size() < 8192 && pointer->state != "skipped") {
        Probe entry;
        entry.state = pointer->state;
        entry.error = pointer->error;
        entry.status = pointer->http;
        entry.sizeBytes = pointer->sizeBytes;
        entry.takenMs = wallMs();
        g_probes[key] = entry;
      }
    });
  }
  pool.waitForIdle(60000);
}

void resetProbeCache() {
  std::lock_guard<std::mutex> lock(g_probeMutex);
  g_probes.clear();
}
