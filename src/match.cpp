#include "match.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>

#include "util.h"

namespace {

const std::set<std::string>& stopWords() {
  static const std::set<std::string> words = {"the", "a",    "an",  "of",   "and", "for",
                                              "to", "in",   "on",  "game", "games", "ps4",
                                              "ps5"};
  return words;
}

std::unordered_map<std::string, int> bigramMap(const std::string& value) {
  std::unordered_map<std::string, int> counts;
  for (size_t i = 0 + 1; i < value.size(); ++i) {
    counts[value.substr(i - 1, 2)]++;
  }
  return counts;
}

double dice(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return 0;
  if (a == b) return 1;
  auto left = bigramMap(a);
  auto right = bigramMap(b);
  double overlap = 0;
  for (const auto& entry : left) {
    auto found = right.find(entry.first);
    if (found != right.end()) overlap += std::min(entry.second, found->second);
  }
  double total = static_cast<double>(a.size() - 1) + static_cast<double>(b.size() - 1);
  return total > 0 ? (2.0 * overlap) / total : 0.0;
}

int indexOfSequence(const std::vector<std::string>& haystack, const std::vector<std::string>& needle) {
  if (needle.empty() || needle.size() > haystack.size()) return -1;
  for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
    bool match = true;
    for (size_t offset = 0; offset < needle.size(); ++offset) {
      if (haystack[start + offset] != needle[offset]) {
        match = false;
        break;
      }
    }
    if (match) return static_cast<int>(start);
  }
  return -1;
}

bool hasWord(const std::vector<std::string>& tokens, const std::string& word) {
  return std::find(tokens.begin(), tokens.end(), word) != tokens.end();
}

bool hasPlaystationGeneration(const std::vector<std::string>& tokens, const std::string& generation) {
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (tokens[i] == "playstation" && tokens[i + 1] == generation) return true;
  }
  return false;
}

bool hasPlatformWord(const std::vector<std::string>& tokens, const std::string& tag) {
  if (tag == "ps5") return hasWord(tokens, "ps5") || hasPlaystationGeneration(tokens, "5");
  if (tag == "ps4")
    return hasWord(tokens, "ps4") || hasPlaystationGeneration(tokens, "4") || hasWord(tokens, "fpkg");
  if (tag == "legacy") {
    for (const char* word : {"ps3", "ps2", "psx", "psp", "vita"}) {
      if (hasWord(tokens, word)) return true;
    }
    return hasPlaystationGeneration(tokens, "3") || hasPlaystationGeneration(tokens, "2");
  }
  if (tag == "other") {
    for (const char* word : {"xbox", "switch", "wii", "gamecube", "dreamcast", "nintendo"}) {
      if (hasWord(tokens, word)) return true;
    }
  }
  return false;
}

bool looksLikeTitleId(const std::string& token, const std::string& prefix) {
  if (token.size() != prefix.size() + 5) return false;
  if (toLowerAscii(token.substr(0, prefix.size())) != prefix) return false;
  for (size_t i = prefix.size(); i < token.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
  }
  return true;
}

std::vector<std::string> wordsIn(const std::string& text) {
  return tokensOf(normalizeText(text));
}

const std::vector<std::string>& blockedHosts() {
  static const std::vector<std::string> hosts = {
      "wikipedia.org",   "fandom.com",         "steamcommunity.com", "steampowered.com",
      "youtube.com",     "youtu.be",           "twitter.com",        "x.com",
      "facebook.com",    "instagram.com",      "tiktok.com",         "twitch.tv",
      "imdb.com",        "metacritic.com",     "ign.com",            "gamespot.com",
      "playstation.com", "bandainamcoent.com", "bandainamcoent.eu",  "amazon.com",
      "ebay.com",        "discord.com",        "discord.gg",         "wikia.com",
      "wiki.gg",         "nexusmods.com",      "downloadgameps3.net", "downloadgameps3.com"};
  return hosts;
}

bool endsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

double scoreTitle(const std::string& query, const std::string& candidate) {
  std::string q = normalizeText(query);
  std::string c = normalizeText(candidate);
  if (q.empty() || c.empty()) return 0;
  if (c == q) return 1;

  std::vector<std::string> queryTokens = tokensOf(q);
  std::vector<std::string> candidateTokens = tokensOf(c);
  int at = indexOfSequence(candidateTokens, queryTokens);
  if (at == 0) return 0.95;
  if (at == 1) return 0.9;

  std::vector<std::string> meaningful;
  for (const std::string& token : queryTokens) {
    if (!stopWords().count(token)) meaningful.push_back(token);
  }
  if (meaningful.empty()) return 0;

  std::set<std::string> candidateSet(candidateTokens.begin(), candidateTokens.end());
  size_t covered = 0;
  for (const std::string& token : meaningful) {
    if (candidateSet.count(token)) ++covered;
  }
  double coverage = static_cast<double>(covered) / static_cast<double>(meaningful.size());
  return std::max(coverage * 0.65, dice(q, c) * 0.9);
}

std::string detectConsole(const std::string& text) {
  std::vector<std::string> tokens = wordsIn(text);
  for (const std::string& token : tokens) {
    if (looksLikeTitleId(token, "ppsa")) return "PS5";
    if (looksLikeTitleId(token, "cusa")) return "PS4";
  }

  bool has5 = hasPlatformWord(tokens, "ps5");
  bool has4 = hasPlatformWord(tokens, "ps4");
  if (has5 && !has4) return "PS5";
  if (has4 && !has5) return "PS4";
  return "";
}

std::string foreignPlatform(const std::string& text) {
  std::vector<std::string> tokens = wordsIn(text);
  if (hasPlatformWord(tokens, "ps5") || hasPlatformWord(tokens, "ps4")) return "";
  if (hasPlatformWord(tokens, "legacy")) return "legacy";
  if (hasPlatformWord(tokens, "other")) return "other";
  return "";
}

bool slugHasPlatform(const std::string& slug) {
  std::vector<std::string> tokens = wordsIn(slug);
  return hasPlatformWord(tokens, "ps5") || hasPlatformWord(tokens, "ps4") ||
         hasPlatformWord(tokens, "legacy") || hasPlatformWord(tokens, "other");
}

std::string titleFromUrl(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return "";
  std::string path = parts.path;
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  size_t slash = path.find_last_of('/');
  std::string segment = slash == std::string::npos ? path : path.substr(slash + 1);
  segment = percentDecode(segment);
  size_t dot = segment.find_last_of('.');
  if (dot != std::string::npos) {
    std::string suffix = toLowerAscii(segment.substr(dot));
    if (suffix == ".pkg" || suffix == ".exfat") segment = segment.substr(0, dot);
  }
  for (char& c : segment) {
    if (c == '-' || c == '_' || c == '+') c = ' ';
  }
  return collapseSpaces(segment);
}

bool isGenericLabel(const std::string& text) {
  std::string value = collapseSpaces(text);
  if (value.size() < 6) return true;
  if (startsWithI(value, "http://") || startsWithI(value, "https://")) return true;
  static const std::set<std::string> labels = {"download",      "downloads",     "mega",
                                               "mega link",     "1file",         "1fichier",
                                               "mediafire",     "gdrive",        "google drive",
                                               "link",          "links",         "part 1",
                                               "part 2",        "part 3",        "part 4",
                                               "part 5",        "file",          "click here"};
  return labels.count(toLowerAscii(value)) > 0;
}

bool isHostLabel(const std::string& text) {
  std::string value = collapseSpaces(toLowerAscii(text));
  if (value.empty() || value.size() > 40) return false;   // a real title is longer
  static const std::set<std::string> exact = {
      "gofile",       "1file",         "1 file",         "multi",        "multi link",
      "mirror",       "mirrors",       "mirror 1",       "mirror 2",     "mirror 3",
      "direct",       "direct link",   "get link",       "download",     "download link",
      "download now", "download here", "download file",  "download page", "main file",
      "gdrive link",  "google drive",  "mega",           "mediafire",    "1fichier",
      "fileserver",   "file host",     "filehost",       "server",       "server 1",
      "host",         "host 1",        "link 1",         "link 2",       "link 3",
      "open link",    "read more",     "click here",     "get file",     "view file",
      "file link",    "extract to root", "click here to download",       "1file (multi)",
      "1 file (multi)"};
  if (exact.count(value) > 0) return true;

  // "Gofile / Mediafire", "Mirror 3 (1fichier)", "Download via Mega" - still a
  // shop name, and short enough that it cannot be a release title
  static const char* brands[] = {"gofile", "1fichier", "mediafire", "mega",  "mirror",
                                 "1file",  "gdrive",   "google drive", "fileserver", "mixdrop",
                                 "vikingfile", "rootz", "akirabox", "mocha", "buzzheavier"};
  for (const char* brand : brands) {
    if (value.find(brand) != std::string::npos) return true;
  }
  return false;
}

std::string kindTag(const std::string& text) {
  std::string value = collapseSpaces(text);
  std::string lower = toLowerAscii(value);
  static const char* kinds[] = {"update", "dlc", "patch", "part", "firmware", "addon", "add-on",
                                "multi",  "language", "version", "repack", "fix", "data", "sound",
                                "video",  "theme", "avatar", "manual", "demo", "trial", "beta"};
  for (const char* kind : kinds) {
    if (lower.find(kind) != std::string::npos) return value;
  }
  return std::string();
}

std::string titleFromLink(const std::string& pageTitle, const std::string& anchorText) {
  std::string label = collapseSpaces(anchorText);
  if (label.empty()) return pageTitle;
  if (!isGenericLabel(label) && !isHostLabel(label)) return label;
  std::string tag = kindTag(label);
  return tag.empty() ? pageTitle : pageTitle + " [" + tag + "]";
}

bool isBareHostRoot(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return false;
  bool rootPath = parts.path == "/" || parts.path.empty();
  return rootPath && parts.query.size() <= 1;
}

bool looksLikeHandler(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return false;
  std::string path = toLowerAscii(parts.path);

  static const std::vector<std::string> markers = {"/dll-", "/dlls-", "/dl-", "/download", "/downloads",
                                                   "/go/", "/out/", "/redirect", "/link/", "/links/",
                                                   "/file/", "/files/"};
  for (const std::string& marker : markers) {
    if (path.find(marker) != std::string::npos) return true;
  }

  size_t slash = path.find_last_of('/');
  std::string last = slash == std::string::npos ? path : path.substr(slash + 1);
  while (!last.empty() && last.back() == '/') last.pop_back();
  size_t digits = last.find_first_of("0123456789");
  if (digits == std::string::npos || digits < 2 || digits > 8) return false;
  size_t trailing = 0;
  for (size_t i = digits; i < last.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(last[i]))) ++trailing;
  }
  return trailing >= 3;
}

bool isBlockedHost(const std::string& host) {
  std::string value = toLowerAscii(host);
  if (startsWithI(value, "www.")) value = value.substr(4);
  for (const std::string& blocked : blockedHosts()) {
    if (value == blocked || endsWith(value, "." + blocked)) return true;
  }
  return false;
}
