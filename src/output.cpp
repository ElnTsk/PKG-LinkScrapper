#include "output.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "http.h"
#include "json.h"
#include "parts.h"
#include "util.h"

namespace {

int stateRank(const std::string& state) {
  static const std::map<std::string, int> ranks = {{"alive", 0},
                                                   {"container", 1},
                                                   {"gated", 2},
                                                   {"page", 3},
                                                   {"partial", 4},
                                                   {"blocked", 5},
                                                   {"rate-limited", 6},
                                                   {"stub", 7},
                                                   {"shell", 8},
                                                   {"dead", 9},
                                                   {"unreachable", 10},
                                                   {"unverified", 11}};
  auto found = ranks.find(state);
  return found == ranks.end() ? 99 : found->second;
}

std::string pingText(const Hit& hit) {
  if (hit.pingMs > 0) return std::to_string(hit.pingMs) + "ms";
  if (hit.pingMs == 0) return "no route";
  return std::string();
}

std::string joinParts(const Hit& hit) {
  std::string out;
  for (const std::string& url : hit.partUrls) out += (out.empty() ? "" : " ") + url;
  return out;
}

std::string csvCell(const std::string& value) {
  if (value.find_first_of("\"\n,") == std::string::npos) return value;
  return "\"" + replaceAll(value, "\"", "\"\"") + "\"";
}

std::string clean(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '\t' || c == '\r' || c == '\n') {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace

void sortHits(std::vector<Hit>& hits, const std::vector<std::string>& gameOrder) {
  std::map<std::string, int> order;
  for (size_t i = 0; i < gameOrder.size(); ++i) order[gameOrder[i]] = static_cast<int>(i);

  std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
    int gameA = order.count(a.game) ? order.at(a.game) : 999;
    int gameB = order.count(b.game) ? order.at(b.game) : 999;
    if (gameA != gameB) return gameA < gameB;
    // trusted rows lead; a good host beats a lucky find on a bad one
    bool recA = !a.trust.empty();
    bool recB = !b.trust.empty();
    if (recA != recB) return recA;
    if (stateRank(a.state) != stateRank(b.state)) return stateRank(a.state) < stateRank(b.state);
    if (a.sizeBytes != b.sizeBytes) return a.sizeBytes > b.sizeBytes;
    return a.title < b.title;
  });
}

std::string renderHits(const std::vector<Hit>& hits, const std::string& format) {
  if (format == "json") {
    std::string out = "[\n";
    for (size_t i = 0; i < hits.size(); ++i) {
      const Hit& hit = hits[i];
      out += "  {\n";
      out += "    \"game\": " + jsonEscape(hit.game) + ",\n";
      out += "    \"console\": " + jsonEscape(hit.console) + ",\n";
      out += "    \"state\": " + jsonEscape(hit.state) + ",\n";
      out += "    \"trust\": " + jsonEscape(hit.trust) + ",\n";
      out += "    \"http\": " + std::to_string(hit.http) + ",\n";
      out += "    \"kind\": " + jsonEscape(hit.kind) + ",\n";
      out += "    \"size_bytes\": " +
             (hit.sizeBytes > 0 ? std::to_string(hit.sizeBytes) : std::string("null")) + ",\n";
      out += "    \"size\": " + jsonEscape(hit.size) + ",\n";
      out += "    \"parts\": " + std::to_string(hit.partCount) + ",\n";
      out += "    \"ping_ms\": " + (hit.pingMs >= 0 ? std::to_string(hit.pingMs) : std::string("null")) +
             ",\n";
      out += "    \"title\": " + jsonEscape(hit.title) + ",\n";
      out += "    \"host\": " + jsonEscape(hit.host) + ",\n";
      out += "    \"source\": " + jsonEscape(hit.source) + ",\n";
      out += "    \"site\": " + jsonEscape(hit.site) + ",\n";
      out += "    \"url\": " + jsonEscape(hit.url);
      if (hit.partCount >= 2) {
        out += ",\n    \"part_urls\": [";
        for (size_t p = 0; p < hit.partUrls.size(); ++p) {
          out += (p ? ", " : "") + jsonEscape(hit.partUrls[p]);
        }
        out += "]";
      }
      out += hit.error.empty() ? "\n" : ",\n";
      if (!hit.error.empty()) out += "    \"error\": " + jsonEscape(hit.error) + "\n";
      out += std::string("  }") + (i + 1 < hits.size() ? "," : "") + "\n";
    }
    out += "]";
    return out;
  }

  static const char* header[] = {"game", "console", "state", "trust", "http", "kind",
                                 "size_bytes", "size", "parts", "ping_ms", "title", "host",
                                 "source", "url", "part_urls"};
  constexpr size_t headerCount = sizeof(header) / sizeof(header[0]);

  if (format == "csv" || format == "tsv") {
    char separator = format == "csv" ? ',' : '\t';
    std::string out;
    for (size_t i = 0; i < headerCount; ++i) {
      if (i) out += separator;
      out += header[i];
    }
    out += "\n";
    for (const Hit& hit : hits) {
      std::vector<std::string> cells = {
          hit.game,   hit.console, hit.state, hit.trust, std::to_string(hit.http), hit.kind,
          hit.sizeBytes > 0 ? std::to_string(hit.sizeBytes) : std::string(), hit.size,
          partsLabel(hit), hit.pingMs >= 0 ? std::to_string(hit.pingMs) : std::string(), hit.title,
          hit.host,   hit.source,  hit.url,
          hit.partCount >= 2 && !hit.isChild ? joinParts(hit) : std::string()};
      for (size_t i = 0; i < cells.size(); ++i) {
        if (i) out += separator;
        std::string cell = format == "csv" ? csvCell(clean(cells[i])) : clean(cells[i]);
        out += cell;
      }
      out += "\n";
    }
    out.pop_back();
    return out;
  }

  struct Column {
    const char* label;
    size_t cap;
  };
  static const Column columns[] = {{"CONSOLE", 7}, {"STATE", 12}, {"TRUST", 11}, {"SIZE", 10},
                                   {"PARTS", 9},    {"PING", 8},   {"HOST", 20},  {"TITLE", 40},
                                   {"URL", 0}};
  constexpr size_t columnCount = sizeof(columns) / sizeof(columns[0]);
  constexpr size_t urlColumn = columnCount - 1;

  auto cellAt = [](const Hit& hit, size_t index) -> std::string {
    switch (index) {
      case 0: return hit.console;
      case 1: return hit.state;
      case 2: return hit.trust;
      case 3: return hit.size;
      case 4: return partsLabel(hit);
      case 5: return pingText(hit);
      case 6: return hit.host;
      case 7: return hit.title;
      default: return hit.url;
    }
  };

  std::vector<size_t> widths(columnCount, 0);
  for (size_t column = 0; column < columnCount; ++column) {
    size_t widest = std::strlen(columns[column].label);
    for (const Hit& hit : hits) {
      widest = std::max(widest, cellAt(hit, column).size());
    }
    widths[column] = columns[column].cap ? std::min(widest, columns[column].cap) : widest;
  }

  auto line = [&](const std::vector<std::string>& cells) {
    std::string out;
    for (size_t index = 0; index < cells.size(); ++index) {
      std::string value = index == urlColumn ? cells[index] : truncateTo(cells[index], widths[index]);
      out += value;
      if (index != urlColumn) {
        out += std::string(widths[index] - value.size(), ' ');
        out += "  ";
      }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
  };

  std::vector<std::string> labels;
  for (size_t index = 0; index < columnCount; ++index) labels.push_back(columns[index].label);

  std::string out = line(labels) + "\n";
  std::string rule;
  for (size_t index = 0; index < columnCount; ++index) {
    rule += std::string(widths[index], '-');
    if (index != urlColumn) rule += "  ";
  }
  while (!rule.empty() && rule.back() == ' ') rule.pop_back();
  out += rule + "\n";

  for (const Hit& hit : hits) {
    std::vector<std::string> cells;
    for (size_t index = 0; index < columnCount; ++index) cells.push_back(cellAt(hit, index));
    out += line(cells) + "\n";
  }
  out.pop_back();
  return out;
}

std::string summarize(const std::vector<Hit>& hits, size_t gameCount) {
  std::map<std::string, size_t> counts;
  size_t sets = 0;
  size_t files = 0;
  size_t recommended = 0;
  size_t unreachableHosts = 0;
  for (const Hit& hit : hits) {
    ++counts[hit.state];
    if (hit.pingMs == 0) ++unreachableHosts;
    if (!hit.trust.empty()) ++recommended;
    if (hit.partCount >= 2) {
      ++sets;
      files += hit.partUrls.size();
    }
  }

  std::vector<std::pair<std::string, size_t>> ordered(counts.begin(), counts.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return stateRank(a.first) < stateRank(b.first); });

  std::vector<std::string> parts;
  for (const auto& entry : ordered) {
    parts.push_back(std::to_string(entry.second) + " " + entry.first);
  }

  std::string out = std::to_string(gameCount) + (gameCount == 1 ? " game" : " games");
  out += " . " + std::to_string(hits.size()) + " rows";
  if (recommended) out += " . " + std::to_string(recommended) + " recommended";
  if (sets) out += " . " + std::to_string(sets) + " part-sets (" + std::to_string(files) + " files)";
  if (unreachableHosts) out += " . " + std::to_string(unreachableHosts) + " hosts not answering";
  if (!parts.empty()) out += " . " + joinValues(parts, " . ");
  return out;
}
