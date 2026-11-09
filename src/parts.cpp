#include "parts.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

#include "util.h"

namespace {

struct Detect {
  bool isPart = false;
  int index = 0;
  int total = 0;
  std::string key;
};

int toInt(const std::string& value) {
  int out = 0;
  for (char c : value) {
    if (c < '0' || c > '9') break;
    out = out * 10 + (c - '0');
  }
  return out;
}

bool digitsOnly(const std::string& value) {
  if (value.empty()) return false;
  for (char c : value) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

// name.part07.rar
bool matchPartRar(const std::string& lower, Detect& out) {
  size_t marker = lower.rfind(".part");
  if (marker == std::string::npos) return false;
  size_t cursor = marker + 5;
  size_t digitsStart = cursor;
  while (cursor < lower.size() && std::isdigit(static_cast<unsigned char>(lower[cursor]))) ++cursor;
  if (cursor == digitsStart) return false;
  size_t dot = lower.find('.', cursor);
  if (dot == std::string::npos) return false;
  out.isPart = true;
  out.index = toInt(lower.substr(digitsStart, cursor - digitsStart));
  out.key = lower.substr(0, digitsStart) + lower.substr(cursor);
  return true;
}

// PSN pkg parts: TITLE_00-GAME_0.pkg, _1.pkg, _2.pkg
bool matchPkgUnderscore(const std::string& lower, Detect& out) {
  size_t dot = lower.find_last_of('.');
  if (dot == std::string::npos || lower.compare(dot, 4, ".pkg") != 0) return false;
  std::string stem = lower.substr(0, dot);
  size_t underscore = stem.find_last_of('_');
  if (underscore == std::string::npos || underscore + 1 >= stem.size()) return false;
  std::string tail = stem.substr(underscore + 1);
  if (!digitsOnly(tail) || tail.size() > 3) return false;
  out.isPart = true;
  out.index = toInt(tail);
  out.key = stem.substr(0, underscore + 1) + ".pkg";
  return true;
}

// name.001, name.002
bool matchNumericTail(const std::string& lower, Detect& out) {
  size_t dot = lower.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= lower.size()) return false;
  std::string tail = lower.substr(dot + 1);
  if (tail.size() != 3 || !digitsOnly(tail)) return false;
  int value = toInt(tail);
  if (value < 1 || value > 999) return false;
  out.isPart = true;
  out.index = value;
  out.key = lower.substr(0, dot) + ".";
  return true;
}

// name.z01 / name.r07 split archives
bool matchLetterDigitTail(const std::string& lower, Detect& out) {
  size_t dot = lower.find_last_of('.');
  if (dot == std::string::npos || dot + 4 != lower.size()) return false;
  std::string tail = lower.substr(dot + 1);
  if (!std::isalpha(static_cast<unsigned char>(tail[0]))) return false;
  if (!std::isdigit(static_cast<unsigned char>(tail[1])) ||
      !std::isdigit(static_cast<unsigned char>(tail[2])))
    return false;
  out.isPart = true;
  out.index = (tail[1] - '0') * 10 + (tail[2] - '0');
  out.key = lower.substr(0, dot + 1) + tail[0] + "00";
  return true;
}

Detect detect(const std::string& url) {
  Detect out;
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return out;

  std::string path = toLowerAscii(parts.path);
  std::string decoded = percentDecode(path);

  for (const std::string& candidate : {decoded, path}) {
    if (matchPartRar(candidate, out)) return out;
    if (matchPkgUnderscore(candidate, out)) return out;
    if (matchLetterDigitTail(candidate, out)) return out;
    if (matchNumericTail(candidate, out)) return out;
  }

  out.isPart = false;
  return out;
}

// "part 1", "part03", "part.2" - the number a catalog puts in the link name
// when the file host itself hides it behind an opaque token
bool matchPartWord(const std::string& lower, Detect& out) {
  size_t pos = 0;
  while ((pos = lower.find("part", pos)) != std::string::npos) {
    size_t cursor = pos + 4;
    while (cursor < lower.size() && (lower[cursor] == ' ' || lower[cursor] == '.' ||
                                     lower[cursor] == '-' || lower[cursor] == '_' ||
                                     lower[cursor] == '	')) {
      ++cursor;
    }
    size_t digitsStart = cursor;
    while (cursor < lower.size() && std::isdigit(static_cast<unsigned char>(lower[cursor]))) ++cursor;
    if (cursor > digitsStart) {
      int index = toInt(lower.substr(digitsStart, cursor - digitsStart));
      if (index > 0 && index < 1000) {
        out.isPart = true;
        out.index = index;
        out.key = lower.substr(0, pos) + lower.substr(cursor);
        return true;
      }
    }
    pos += 4;
  }
  return false;
}

std::string setFolder(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return normalizeUrl(url);
  size_t lastSlash = parts.path.find_last_of('/');
  std::string folder = lastSlash == std::string::npos ? "/" : parts.path.substr(0, lastSlash + 1);
  return parts.scheme + "://" + parts.host + folder;
}

}  // namespace

bool isPartUrl(const std::string& url) { return detect(url).isPart; }

int partIndexOf(const std::string& url) { return detect(url).index; }

std::vector<Hit> groupParts(const std::vector<Hit>& hits) {
  std::map<std::string, const Hit*> byUrl;
  for (const Hit& hit : hits) byUrl[normalizeUrl(hit.url)] = &hit;

  std::map<std::string, size_t> slots;
  std::map<std::string, int> memberIndex;
  std::vector<Hit> rows;
  rows.reserve(hits.size());

  for (const Hit& hit : hits) {
    Detect found = detect(hit.url);
    bool fromTitle = false;
    if (!found.isPart) {
      Detect titleFound;
      if (!hit.title.empty() && matchPartWord(toLowerAscii(hit.title), titleFound)) {
        found = titleFound;
        fromTitle = true;
      }
    }

    if (!found.isPart) {
      rows.push_back(hit);
      continue;
    }
    memberIndex[normalizeUrl(hit.url)] = found.index;

    std::string key = (fromTitle ? std::string("T|") : setFolder(hit.url) + "|") + found.key;
    auto existing = slots.find(key);
    if (existing == slots.end()) {
      Hit row = hit;
      row.partUrls = {hit.url};
      row.partIndex = found.index;
      slots[key] = rows.size();
      rows.push_back(row);
      continue;
    }

    rows[existing->second].partUrls.push_back(hit.url);
  }

  for (Hit& row : rows) {
    if (row.partUrls.size() < 2) {
      row.partUrls.clear();
      row.partIndex = 0;
      continue;
    }

    std::vector<std::pair<int, std::string>> members;
    members.reserve(row.partUrls.size());
    for (const std::string& url : row.partUrls) {
      auto known = memberIndex.find(normalizeUrl(url));
      members.emplace_back(known == memberIndex.end() ? 0 : known->second, url);
    }
    std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first < b.first;
      return a.second < b.second;
    });

    row.partUrls.clear();
    for (const auto& member : members) row.partUrls.push_back(member.second);

    long long total = 0;
    bool anyAlive = false;
    bool anyStub = false;
    for (const std::string& url : row.partUrls) {
      const Hit* member = byUrl[normalizeUrl(url)];
      if (!member) continue;
      total += member->sizeBytes;
      if (member->state == "alive") anyAlive = true;
      else if (member->state == "stub") anyStub = true;
    }

    row.url = row.partUrls.front();
    row.partIndex = members.front().first;
    row.sizeBytes = total;
    row.size = total > 0 ? formatBytes(total) : std::string();
    row.partCount = static_cast<int>(row.partUrls.size());
    if (anyAlive) row.state = "alive";
    else if (anyStub) row.state = "stub";
    else row.state = "partial";
  }

  return rows;
}

std::string partsLabel(const Hit& hit) {
  if (hit.isChild) {
    if (hit.partCount < 2) return std::string();
    return std::to_string(hit.partIndex) + " of " + std::to_string(hit.partCount);
  }
  if (hit.partCount < 2) return std::string();
  return std::to_string(hit.partCount) + " parts";
}

std::string partGroupKey(const std::string& url) {
  Detect found = detect(url);
  if (!found.isPart) return std::string();
  return setFolder(url) + "|" + found.key;
}

namespace {
std::string groupKeyForHit(const Hit& hit) {
  Detect found = detect(hit.url);
  if (found.isPart) return setFolder(hit.url) + "|" + found.key;
  Detect titleFound;
  if (!hit.title.empty() && matchPartWord(toLowerAscii(hit.title), titleFound)) {
    return "T|" + titleFound.key;
  }
  return std::string();
}
}  // namespace

std::vector<Hit> trimByGroup(const std::vector<Hit>& hits, size_t rowLimit) {
  std::set<std::string> kept;
  std::set<std::string> dropped;
  std::vector<Hit> out;
  out.reserve(hits.size());
  size_t rows = 0;

  for (const Hit& hit : hits) {
    std::string key = groupKeyForHit(hit);

    if (key.empty()) {
      if (rows < rowLimit) {
        ++rows;
        out.push_back(hit);
      }
      continue;
    }

    if (dropped.count(key)) continue;
    if (!kept.count(key)) {
      if (rows >= rowLimit) {
        dropped.insert(key);
        continue;
      }
      kept.insert(key);
      ++rows;
    }
    out.push_back(hit);
  }

  return out;
}
