#include "util.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {

const std::map<std::string, std::string>& accentTable() {
  static const std::map<std::string, std::string> table = {
      {"\xC3\x81", "A"}, {"\xC3\x80", "A"}, {"\xC3\x82", "A"}, {"\xC3\x83", "A"},
      {"\xC3\x84", "A"}, {"\xC3\x85", "A"}, {"\xC3\x86", "AE"}, {"\xC3\x87", "C"},
      {"\xC3\x88", "E"}, {"\xC3\x89", "E"}, {"\xC3\x8A", "E"}, {"\xC3\x8B", "E"},
      {"\xC3\x8C", "I"}, {"\xC3\x8D", "I"}, {"\xC3\x8E", "I"}, {"\xC3\x8F", "I"},
      {"\xC3\x90", "D"}, {"\xC3\x91", "N"}, {"\xC3\x92", "O"}, {"\xC3\x93", "O"},
      {"\xC3\x94", "O"}, {"\xC3\x95", "O"}, {"\xC3\x96", "O"}, {"\xC3\x97", "x"},
      {"\xC3\x98", "O"}, {"\xC3\x99", "Y"}, {"\xC3\x9A", "U"}, {"\xC3\x9B", "U"},
      {"\xC3\x9C", "U"}, {"\xC3\x9D", "Y"}, {"\xC3\x9F", "ss"},
      {"\xC3\xA1", "a"}, {"\xC3\xA0", "a"}, {"\xC3\xA2", "a"}, {"\xC3\xA3", "a"},
      {"\xC3\xA4", "a"}, {"\xC3\xA5", "a"}, {"\xC3\xA6", "ae"}, {"\xC3\xA7", "c"},
      {"\xC3\xA8", "e"}, {"\xC3\xA9", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"},
      {"\xC3\xAC", "i"}, {"\xC3\xAD", "i"}, {"\xC3\xAE", "i"}, {"\xC3\xAF", "i"},
      {"\xC3\xB0", "d"}, {"\xC3\xB1", "n"}, {"\xC3\xB2", "o"}, {"\xC3\xB3", "o"},
      {"\xC3\xB4", "o"}, {"\xC3\xB5", "o"}, {"\xC3\xB6", "o"}, {"\xC3\xB8", "o"},
      {"\xC3\xB9", "y"}, {"\xC3\xBA", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBC", "u"},
      {"\xC3\xBD", "y"}, {"\xC5\x81", "l"}, {"\xC5\x82", "L"}, {"\xC5\xA1", "s"},
      {"\xC5\xA0", "S"}, {"\xC5\xBE", "z"}, {"\xC5\x9D", "Z"}, {"\xC4\x8D", "c"},
      {"\xC4\x8C", "C"}, {"\xC4\x99", "e"}, {"\xC4\x98", "E"}, {"\xC5\x99", "r"},
      {"\xC5\x98", "R"}, {"\xC5\xA3", "t"}, {"\xC5\xA2", "T"}, {"\xC3\xBF", "y"},
  };
  return table;
}

void appendUtf8(std::string& out, unsigned codepoint) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::string stripTrailingSlash(std::string value) {
  while (value.size() > 1 && value.back() == '/') value.pop_back();
  return value;
}

}  // namespace

std::string trim(const std::string& value) {
  size_t begin = value.find_first_not_of(" \t\r\n\v\f");
  if (begin == std::string::npos) return "";
  size_t end = value.find_last_not_of(" \t\r\n\v\f");
  return value.substr(begin, end - begin + 1);
}

std::string toLowerAscii(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

std::string toUpperAscii(std::string value) {
  for (char& c : value) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return value;
}

bool iequals(const std::string& a, const std::string& b) {
  return toLowerAscii(a) == toLowerAscii(b);
}

bool startsWithI(const std::string& value, const std::string& prefix) {
  if (value.size() < prefix.size()) return false;
  return toLowerAscii(value.substr(0, prefix.size())) == toLowerAscii(prefix);
}

std::string replaceAll(std::string value, const std::string& from, const std::string& to) {
  if (from.empty()) return value;
  size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
  return value;
}

std::vector<std::string> splitLines(const std::string& value) {
  std::vector<std::string> out;
  std::string current;
  for (char c : value) {
    if (c == '\n') {
      out.push_back(current);
      current.clear();
    } else if (c != '\r') {
      current.push_back(c);
    }
  }
  out.push_back(current);
  return out;
}

std::string joinValues(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string transliterate(const std::string& value) {
  const auto& table = accentTable();
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size();) {
    bool matched = false;
    for (size_t len = 2; len <= 3 && i + len <= value.size(); ++len) {
      auto found = table.find(value.substr(i, len));
      if (found != table.end()) {
        out += found->second;
        i += len;
        matched = true;
        break;
      }
    }
    if (!matched) {
      out.push_back(value[i++]);
    }
  }
  return out;
}

std::string normalizeText(const std::string& value) {
  std::string base = toLowerAscii(transliterate(value));
  std::vector<std::string> words;
  std::string current;
  for (char c : base) {
    bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (keep) {
      current.push_back(c);
    } else if (!current.empty()) {
      words.push_back(current);
      current.clear();
    }
  }
  if (!current.empty()) words.push_back(current);
  return joinValues(words, " ");
}

std::vector<std::string> tokensOf(const std::string& normalized) {
  std::vector<std::string> out;
  std::string current;
  for (char c : normalized) {
    if (c == ' ') {
      if (!current.empty()) out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

std::string collapseSpaces(const std::string& value) {
  std::string out;
  bool pendingSpace = false;
  for (char c : value) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(c);
  }
  return out;
}

bool gVerbose = false;

namespace {

LogSink gSink = nullptr;
std::mutex sinkMutex;
std::atomic<bool> gAbort{false};

}  // namespace

void setLogSink(LogSink sink) {
  std::lock_guard<std::mutex> lock(sinkMutex);
  gSink = sink;
}

bool abortRequested() { return gAbort.load(std::memory_order_relaxed); }
void requestAbort() { gAbort.store(true, std::memory_order_relaxed); }
void resetAbort() { gAbort.store(false, std::memory_order_relaxed); }

void logInfo(const std::string& line) {
  static std::mutex writeMutex;
  std::lock_guard<std::mutex> lock(writeMutex);
  if (gSink) {
    gSink(line.c_str(), 0);
    return;
  }
  std::fwrite(line.data(), 1, line.size(), stderr);
  std::fputc('\n', stderr);
}

void logDebug(const std::string& line) {
  if (!gVerbose) return;
  static std::mutex writeMutex;
  std::lock_guard<std::mutex> lock(writeMutex);
  if (gSink) {
    gSink(line.c_str(), 1);
    return;
  }
  std::fwrite(("  " + line + "\n").data(), 1, line.size() + 3, stderr);
}

std::string percentEncode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

std::string percentDecode(const std::string& value) {
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      int hi = std::isdigit(static_cast<unsigned char>(value[i + 1])) ? value[i + 1] - '0'
                                                                      : std::tolower(value[i + 1]) - 'a' + 10;
      int lo = std::isdigit(static_cast<unsigned char>(value[i + 2])) ? value[i + 2] - '0'
                                                                      : std::tolower(value[i + 2]) - 'a' + 10;
      if (hi >= 0 && hi < 16 && lo >= 0 && lo < 16) {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
      } else {
        out.push_back(value[i]);
      }
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

std::string decodeEntities(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size();) {
    if (value[i] != '&') {
      out.push_back(value[i++]);
      continue;
    }
    size_t semicolon = value.find(';', i);
    if (semicolon == std::string::npos || semicolon - i > 10) {
      out.push_back(value[i++]);
      continue;
    }
    std::string entity = value.substr(i + 1, semicolon - i - 1);
    if (entity == "amp") {
      out.push_back('&');
    } else if (entity == "lt") {
      out.push_back('<');
    } else if (entity == "gt") {
      out.push_back('>');
    } else if (entity == "quot") {
      out.push_back('"');
    } else if (entity == "apos") {
      out.push_back('\'');
    } else if (entity == "nbsp") {
      out.push_back(' ');
    } else if (!entity.empty() && entity[0] == '#') {
      unsigned codepoint = 0;
      if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
        codepoint = static_cast<unsigned>(std::strtoul(entity.c_str() + 2, nullptr, 16));
      } else {
        codepoint = static_cast<unsigned>(std::strtoul(entity.c_str() + 1, nullptr, 10));
      }
      if (codepoint > 0 && codepoint < 0x110000) appendUtf8(out, codepoint);
    } else {
      out += "&" + entity + ";";
    }
    i = semicolon + 1;
  }
  return out;
}

UrlParts crackUrl(const std::string& url) {
  UrlParts parts;
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return parts;
  parts.scheme = toLowerAscii(url.substr(0, schemeEnd));
  if (parts.scheme != "http" && parts.scheme != "https") return parts;

  size_t authorityStart = schemeEnd + 3;
  size_t pathStart = url.find('/', authorityStart);
  size_t queryStart = url.find('?', authorityStart);
  size_t fragmentStart = url.find('#', authorityStart);

  size_t authorityEnd = url.size();
  if (pathStart != std::string::npos) authorityEnd = std::min(authorityEnd, pathStart);
  if (queryStart != std::string::npos) authorityEnd = std::min(authorityEnd, queryStart);
  if (fragmentStart != std::string::npos) authorityEnd = std::min(authorityEnd, fragmentStart);

  std::string authority = url.substr(authorityStart, authorityEnd - authorityStart);
  size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find('@') == std::string::npos) {
    parts.host = toLowerAscii(authority.substr(0, colon));
    parts.port = std::atoi(authority.c_str() + colon + 1);
  } else {
    parts.host = toLowerAscii(authority);
  }
  if (parts.port <= 0) parts.port = parts.scheme == "https" ? 443 : 80;

  size_t contentStart = authorityEnd;
  size_t tailStart = url.size();
  if (fragmentStart != std::string::npos && fragmentStart >= contentStart) tailStart = fragmentStart;

  std::string rest = url.substr(contentStart, tailStart - contentStart);
  if (!rest.empty() && rest[0] == '?') {
    parts.path = "/";
    parts.query = rest.substr(1);
  } else {
    size_t query = rest.find('?');
    if (query == std::string::npos) {
      parts.path = rest.empty() ? "/" : rest;
    } else {
      parts.path = rest.substr(0, query).empty() ? "/" : rest.substr(0, query);
      parts.query = rest.substr(query + 1);
    }
  }
  if (fragmentStart != std::string::npos) parts.fragment = url.substr(fragmentStart + 1);
  parts.valid = !parts.host.empty();
  return parts;
}

std::string normalizePath(const std::string& path, bool trailingSlash) {
  std::vector<std::string> segments;
  size_t pos = 0;
  while (pos < path.size()) {
    size_t slash = path.find('/', pos);
    if (slash == std::string::npos) slash = path.size();
    std::string segment = path.substr(pos, slash - pos);
    if (segment == "..") {
      if (!segments.empty()) segments.pop_back();
    } else if (!segment.empty() && segment != ".") {
      segments.push_back(segment);
    }
    pos = slash + 1;
  }
  std::string out = "/" + joinValues(segments, "/");
  if (trailingSlash && !out.empty() && out.back() != '/') out += "/";
  return out == "//" ? "/" : out;
}

std::string resolveUrl(const std::string& base, const std::string& reference) {
  std::string ref = trim(reference);
  if (ref.empty()) return "";
  if (startsWithI(ref, "http://") || startsWithI(ref, "https://")) return ref;
  if (ref.rfind("//", 0) == 0) {
    UrlParts parts = crackUrl(base);
    if (!parts.valid) return "";
    return parts.scheme + ":" + ref;
  }

  UrlParts parts = crackUrl(base);
  if (!parts.valid) return "";
  std::string root = parts.scheme + "://" + parts.host +
                     ((parts.port == 80 || parts.port == 443) ? "" : ":" + std::to_string(parts.port));

  if (ref[0] == '/') return root + normalizePath(ref, false);

  bool baseTrailing = !parts.path.empty() && parts.path.back() == '/';
  if (ref[0] == '?') return root + normalizePath(parts.path, baseTrailing) + ref;
  if (ref[0] == '#') return root + normalizePath(parts.path, baseTrailing) + ref;

  bool keepTrailing = !ref.empty() && ref.back() == '/';
  size_t cut = parts.path.find_last_of('/');
  std::string directory = cut == std::string::npos ? "/" : parts.path.substr(0, cut + 1);
  return root + normalizePath(directory + ref, keepTrailing);
}

std::string normalizeUrl(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return trim(url);

  std::vector<std::string> kept;
  size_t pos = 0;
  while (pos <= parts.query.size()) {
    size_t amp = parts.query.find('&', pos);
    if (amp == std::string::npos) amp = parts.query.size();
    std::string pair = parts.query.substr(pos, amp - pos);
    if (!pair.empty()) {
      std::string key = toLowerAscii(pair.substr(0, pair.find('=')));
      bool drop = key == "ref" || key == "src" || key == "aff" || key == "source" ||
                  key.rfind("utm_", 0) == 0;
      if (!drop) kept.push_back(pair);
    }
    pos = amp + 1;
  }

  std::string root = parts.scheme + "://" + parts.host +
                     ((parts.port == 80 || parts.port == 443) ? "" : ":" + std::to_string(parts.port));
  std::string path = stripTrailingSlash(parts.path);
  if (path.empty()) path = "";
  std::string query = kept.empty() ? "" : "?" + joinValues(kept, "&");
  return root + path + query;
}

std::string hostOf(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return "";
  std::string host = parts.host;
  if (startsWithI(host, "www.")) host = host.substr(4);
  return host;
}

std::string registrable(const std::string& host) {
  std::string value = toLowerAscii(host);
  if (startsWithI(value, "www.")) value = value.substr(4);
  static const std::vector<std::string> twoLabel = {
      "co.uk", "org.uk", "ac.uk", "gov.uk", "com.au", "net.au", "co.nz", "co.jp",
      "com.br", "com.tr", "co.in", "co.za", "com.mx", "com.ar", "com.cn", "com.tw"};

  std::vector<std::string> labels;
  size_t pos = 0;
  while (pos <= value.size()) {
    size_t dot = value.find('.', pos);
    if (dot == std::string::npos) dot = value.size();
    labels.push_back(value.substr(pos, dot - pos));
    pos = dot + 1;
  }
  if (labels.size() <= 2) return value;
  std::string lastTwo = labels[labels.size() - 2] + "." + labels[labels.size() - 1];
  for (const std::string& suffix : twoLabel) {
    if (suffix == lastTwo) {
      return labels[labels.size() - 3] + "." + lastTwo;
    }
  }
  return lastTwo;
}

std::string formatBytes(int64_t bytes) {
  if (bytes <= 0) return "";
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), value >= 100.0 ? "%.0f%s" : "%.1f%s", value, units[unit]);
  return buffer;
}

std::string truncateTo(const std::string& value, size_t width) {
  if (width == 0 || value.size() <= width) return value;
  if (width <= 3) return value.substr(0, width);
  return value.substr(0, width - 3) + "...";
}

std::string htmlUnescapeForUrl(const std::string& value) {
  std::string out = replaceAll(value, "\\/", "/");
  out = replaceAll(out, "&amp;", "&");
  out = replaceAll(out, "&#x2F;", "/");
  out = replaceAll(out, "&#47;", "/");
  return out;
}

std::string queryParam(const std::string& query, const std::string& name) {
  size_t pos = 0;
  const size_t need = name.size();
  while (pos <= query.size()) {
    size_t amp = query.find('&', pos);
    if (amp == std::string::npos) amp = query.size();
    std::string pair = query.substr(pos, amp - pos);
    if (pair.size() > need && pair[need] == '=' && pair.compare(0, need, name) == 0) {
      return pair.substr(need + 1);
    }
    pos = amp + 1;
  }
  return "";
}

std::string base64DecodeLoose(const std::string& input) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  unsigned int buffer = 0;
  int bits = 0;
  for (char c : input) {
    if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
    size_t index = alphabet.find(c);
    if (index == std::string::npos && c == '-') index = 62;
    if (index == std::string::npos && c == '_') index = 63;
    if (index == std::string::npos) continue;
    buffer = (buffer << 6) | static_cast<unsigned int>(index);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
      buffer &= (1u << bits) - 1u;
    }
  }
  return out;
}

bool looksBase64(const std::string& value) {
  if (value.size() < 24) return false;
  for (char c : value) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '/' && c != '-' &&
        c != '_' && c != '=') {
      return false;
    }
  }
  return true;
}

std::string base64UrlDecode(const std::string& input) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  unsigned int buffer = 0;
  int bits = 0;
  for (char c : input) {
    if (c == '=') break;
    size_t index = alphabet.find(c);
    if (index == std::string::npos && c == '+') index = 62;
    if (index == std::string::npos && c == '/') index = 63;
    if (index == std::string::npos) continue;
    buffer = (buffer << 6) | static_cast<unsigned int>(index);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
      buffer &= (1u << bits) - 1u;
    }
  }
  return out;
}

std::string unwrapBingHref(const std::string& href) {
  if (!startsWithI(href, "http")) return "";
  UrlParts parts = crackUrl(href);
  if (!parts.valid) return "";
  if (registrable(parts.host) != "bing.com") return href;

  std::string raw = queryParam(parts.query, "u");
  if (raw.rfind("a1", 0) != 0) return "";
  std::string decoded = base64UrlDecode(raw.substr(2));
  return startsWithI(decoded, "http") ? decoded : "";
}

std::string unwrapDuckDuckGoHref(const std::string& href) {
  if (href.empty()) return "";
  std::string absolute = href;
  if (absolute.rfind("//", 0) == 0) absolute = "https:" + absolute;

  UrlParts parts = crackUrl(absolute);
  if (!parts.valid) return "";
  if (registrable(parts.host) != "duckduckgo.com") return absolute;

  std::string target = queryParam(parts.query, "uddg");
  return target.empty() ? "" : percentDecode(target);
}

CountingSemaphore::CountingSemaphore(int permits) : permits_(permits > 0 ? permits : 1) {}

void CountingSemaphore::acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  // Stop must not leave a queued request waiting for a permit held by a
  // timeout that is about to be cancelled anyway
  while (permits_ <= 0) {
    if (abortRequested()) return;
    cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return permits_ > 0; });
  }
  --permits_;
}

void CountingSemaphore::release() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++permits_;
  }
  cv_.notify_one();
}

ThreadPool::ThreadPool(int threads) {
  if (threads < 1) threads = 1;
  for (int i = 0; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  workReady_.notify_all();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) worker.join();
  }
}

void ThreadPool::submit(std::function<void()> job) {
  // once Stop is pressed, new work is dropped at the door - a queued backlog of
  // tens of thousands of no-op jobs is what made the button take seconds back
  if (abortRequested()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(job));
    ++outstanding_;
  }
  workReady_.notify_one();
}

void ThreadPool::waitForIdle(long long deadlineMs) {
  std::unique_lock<std::mutex> lock(mutex_);
  timedOut_ = false;
  if (deadlineMs <= 0) {
    allIdle_.wait(lock, [this] { return outstanding_ == 0; });
    return;
  }
  timedOut_ = !allIdle_.wait_for(lock, std::chrono::milliseconds(deadlineMs),
                                 [this] { return outstanding_ == 0; });
}

ThreadPool::JobDone::~JobDone() {
  std::lock_guard<std::mutex> lock(pool->mutex_);
  if (--pool->outstanding_ == 0) pool->allIdle_.notify_all();
}

void ThreadPool::workerLoop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      workReady_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) return;
      job = std::move(queue_.front());
      queue_.pop_front();
    }

    JobDone done{this};
    try {
      job();
    } catch (...) {
      // a throwing job must never wedge the pool: the counter is released above
    }
  }
}

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int64_t wallMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
