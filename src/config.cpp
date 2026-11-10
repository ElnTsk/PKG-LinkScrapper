#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "json.h"
#include "util.h"

namespace {

const char* kEmbeddedConfig = R"JSON({
  "version": 1,
  "defaults": {
    "timeout_ms": 8000,
    "concurrency": 10,
    "verify": true,
    "match_threshold": 0.72,
    "max_per_game": 40,
    "max_fetch_per_game": 32,
    "min_host_delay_ms": 150,
    "keep_pages": true,
    "depth": 1,
    "good_ttl_days": 7,
    "trusted_hosts": [
      "1fichier.com", "mediafire.com", "gofile.io", "superpsx.com",
      "dlget.ir", "thesuperboffers.com", "vikingfile.com", "archive.org"
    ]
  },
  "sources": [
    {
      "name": "ddg",
      "type": "search",
      "engine": "duckduckgo",
      "query": "\"{q}\" {console} pkg download",
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "bing",
      "type": "search",
      "engine": "bing",
      "query": "\"{q}\" {console} pkg (1fichier OR mega OR mediafire OR filecrypt)",
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "ddg-pastebins",
      "type": "search",
      "engine": "duckduckgo",
      "query": "\"{q}\" ({console} pkg) (site:rentry.co OR site:pastebin.com OR site:controlc.com)",
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "dlpsgame",
      "type": "index",
      "search_url": "https://dlpsgame.com/?s={q}",
      "paged_url": "https://dlpsgame.com/page/{page}/?s={q}",
      "pages": 3,
      "depth": 1,
      "resolve_redirects": true,
      "keep_pages": true
    },
    {
      "name": "superpsx",
      "type": "index",
      "trusted": true,
      "search_url": "https://www.superpsx.com/?s={q}",
      "paged_url": "https://www.superpsx.com/page/{page}/?s={q}",
      "pages": 2,
      "depth": 1,
      "resolve_redirects": true,
      "keep_pages": true
    },
    {
      "name": "psxhax",
      "type": "index",
      "urls": ["https://www.psxhax.com/tags/ps4-pkg-files/"],
      "pages": 1,
      "depth": 1,
      "keep_pages": true
    },
    {
      "name": "pkg-zone",
      "type": "index",
      "urls": ["https://pkg-zone.com/?category=pkg", "https://pkg-zone.com/?category=fpkg"],
      "pages": 1,
      "depth": 1,
      "keep_pages": true
    },
    {
      "name": "p30day-telegram",
      "type": "index",
      "search_url": "https://t.me/s/p30day_ps5_info2?q={q}",
      "pages": 1,
      "depth": 2,
      "keep_pages": false
    },
    {
      "name": "p30day",
      "type": "index",
      "search_url": "https://www.p30day.ir/?s={q}",
      "paged_url": "https://www.p30day.ir/page/{page}/?s={q}",
      "pages": 3,
      "depth": 1,
      "keep_pages": true
    },
    {
      "name": "archive-org",
      "type": "index",
      "trusted": true,
      "search_url": "https://archive.org/advancedsearch.php?q={q}+pkg&fl[]=identifier&fl[]=title&rows=30&output=json",
      "item_url": "https://archive.org/download/{id}/",
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "archive-collections",
      "type": "index",
      "trusted": true,
      "urls": [
        "https://archive.org/download/ps4-fpkg-collection-english-c/",
        "https://archive.org/download/ps4-fpkg-collection-english-j/",
        "https://archive.org/download/ps4-fpkg-collection-english-q/",
        "https://archive.org/download/ps4-fpkg-collection-english-v/",
        "https://archive.org/download/ps4-fpkg-collection-english-w/",
        "https://archive.org/download/ps4-fpkg-collection-english-x/",
        "https://archive.org/download/ps4-fpkg-collection-english-z/"
      ],
      "pages": 1,
      "depth": 0,
      "keep_pages": false
    },
    {
      "name": "pegasus-dlps",
      "type": "catalog",
      "trusted": true,
      "console": "PS5",
      "urls": ["https://pegasus-dl.dev/catalogs/dlps.json"],
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "pegasus-pippo",
      "type": "catalog",
      "trusted": true,
      "console": "PS5",
      "urls": ["https://pegasus-dl.dev/catalogs/pippo.json"],
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "pegasus-pfs",
      "type": "catalog",
      "trusted": true,
      "console": "PS5",
      "urls": ["https://pegasus-dl.dev/catalogs/pfs.json"],
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "pegasus-zer0",
      "type": "catalog",
      "trusted": true,
      "console": "PS5",
      "urls": ["https://pegasus-dl.dev/catalogs/zer0.json"],
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "github-ps4db",
      "type": "catalog",
      "trusted": true,
      "console": "PS4",
      "urls": ["https://raw.githubusercontent.com/huggingfacer04/ps4-games-database/main/ps4_games_with_downloads.json"],
      "pages": 1,
      "depth": 1,
      "keep_pages": false
    },
    {
      "name": "pegasus-ps4",
      "type": "catalog",
      "trusted": true,
      "console": "PS4",
      "urls": ["https://raw.githubusercontent.com/M3hmetSa1t/pegasus-ps4-collection-catalog/main/pegasus-ps4-catalog.json"],
      "pages": 1,
      "depth": 0,
      "keep_pages": false
    },
    {
      "name": "orbispatches",
      "type": "index",
      "console": "PS4",
      "search_url": "https://www.orbispatches.com/api/internal/search?term={q}",
      "item_url": "https://www.orbispatches.com/{id}",
      "pages": 1,
      "depth": 1,
      "keep_pages": true
    },
    {
      "name": "prosperopatches",
      "type": "index",
      "console": "PS5",
      "search_url": "https://prosperopatches.com/api/internal/search?term={q}",
      "item_url": "https://prosperopatches.com/{id}",
      "pages": 1,
      "depth": 1,
      "keep_pages": true
    },
    {
      "name": "psxhax-forum",
      "type": "index",
      "enabled": false,
      "search_url": "https://www.psxhax.com/search/?keywords={q}&search_type=thread&title_only=1",
      "pages": 1,
      "depth": 2,
      "keep_pages": false
    },
    {
      "name": "arabicps4games",
      "type": "index",
      "enabled": false,
      "urls": ["https://arabicps4games.github.io/"],
      "paged_url": "https://arabicps4games.github.io/indexps4page{page}.html",
      "pages": 10,
      "depth": 1,
      "keep_pages": false
    }
  ]
})JSON";

bool readFile(const std::string& path, std::string& out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  out = buffer.str();
  return true;
}

bool exists(const std::string& path) {
  DWORD attributes = GetFileAttributesA(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::string& path) {
  DWORD attributes = GetFileAttributesA(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::vector<std::string> listJsonFiles(const std::string& directory) {
  std::vector<std::string> out;
  WIN32_FIND_DATAA findData;
  HANDLE handle = FindFirstFileA((directory + "\\*.json").c_str(), &findData);
  if (handle == INVALID_HANDLE_VALUE) return out;
  do {
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    out.push_back(directory + "\\" + findData.cFileName);
  } while (FindNextFileA(handle, &findData));
  FindClose(handle);
  std::sort(out.begin(), out.end());
  return out;
}

std::string joinPath(const std::string& left, const std::string& right) {
  if (left.empty()) return right;
  if (!left.empty() && (left.back() == '\\' || left.back() == '/')) return left + right;
  return left + "\\" + right;
}

std::string userConfigDirInternal() {
  const char* appData = std::getenv("APPDATA");
  if (appData && *appData) return joinPath(appData, "PKG-LinkScrapper");
  const char* home = std::getenv("USERPROFILE");
  if (home && *home) return joinPath(home, ".config\\PKG-LinkScrapper");
  return "PKG-LinkScrapper";
}

std::vector<std::string> stringList(const JsonValue* value);

void dedupeInPlace(std::vector<std::string>& values) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const std::string& value : values) {
    if (value.empty() || !seen.insert(value).second) continue;
    out.push_back(value);
  }
  values = out;
}

void applyDefaults(Settings& settings, const JsonValue& document) {
  const JsonValue* defaults = document.find("defaults");
  if (!defaults) defaults = document.find("settings");
  if (!defaults) return;
  if (const JsonValue* item = defaults->find("timeout_ms")) settings.timeoutMs = item->asInt(settings.timeoutMs);
  if (const JsonValue* item = defaults->find("concurrency")) settings.concurrency = item->asInt(settings.concurrency);
  if (const JsonValue* item = defaults->find("verify")) settings.verify = item->asBool(settings.verify);
  if (const JsonValue* item = defaults->find("match_threshold")) settings.threshold = item->asDouble(settings.threshold);
  if (const JsonValue* item = defaults->find("max_per_game")) settings.maxPerGame = item->asInt(settings.maxPerGame);
  if (const JsonValue* item = defaults->find("max_fetch_per_game")) settings.maxFetchPerGame = item->asInt(settings.maxFetchPerGame);
  if (const JsonValue* item = defaults->find("min_host_delay_ms")) settings.minHostDelayMs = item->asInt(settings.minHostDelayMs);
  if (const JsonValue* item = defaults->find("keep_pages")) settings.keepPages = item->asBool(settings.keepPages);
  if (const JsonValue* item = defaults->find("depth")) settings.depth = item->asInt(settings.depth);
  if (const JsonValue* item = defaults->find("user_agent")) settings.userAgent = item->asString(settings.userAgent);
  if (const JsonValue* item = defaults->find("good_ttl_days")) settings.goodTtlDays = std::max(1, item->asInt(settings.goodTtlDays));
  for (const std::string& value : stringList(defaults->find("trusted_hosts"))) {
    if (!value.empty()) settings.trustedHosts.push_back(value);
  }
  for (const std::string& value : stringList(defaults->find("trusted_sources"))) {
    if (!value.empty()) settings.trustedSources.push_back(value);
  }
  // the lists also read at the top level, beside "sources"
  for (const std::string& value : stringList(document.find("trusted_hosts"))) {
    if (!value.empty()) settings.trustedHosts.push_back(value);
  }
  for (const std::string& value : stringList(document.find("trusted_sources"))) {
    if (!value.empty()) settings.trustedSources.push_back(value);
  }
  dedupeInPlace(settings.trustedHosts);
  dedupeInPlace(settings.trustedSources);
}

std::vector<const JsonValue*> sourceNodes(const JsonValue& document) {
  std::vector<const JsonValue*> out;
  if (document.isArray()) {
    for (const JsonValue& item : document.array) out.push_back(&item);
    return out;
  }
  const JsonValue* sources = document.find("sources");
  if (!sources || !sources->isArray()) return out;
  for (const JsonValue& item : sources->array) out.push_back(&item);
  return out;
}

std::vector<std::string> stringList(const JsonValue* value) {
  if (!value) return {};
  return value->asStringList();
}

Source parseSource(const JsonValue& node, const Settings& settings, const std::string& origin,
                   std::string& error) {
  Source source;
  source.origin = origin;
  source.name = node.find("name") ? node.find("name")->asString("") : "";
  source.type = node.find("type") ? toLowerAscii(node.find("type")->asString("")) : "";
  source.engine = node.find("engine") ? node.find("engine")->asString("") : "";
  source.depth = settings.depth;
  source.keepPages = settings.keepPages;
  source.pages = 1;

  if (const JsonValue* item = node.find("enabled")) source.enabled = item->asBool(true);
  if (const JsonValue* item = node.find("pages")) source.pages = std::max(1, item->asInt(1));
  if (const JsonValue* item = node.find("depth")) source.depth = std::max(0, item->asInt(settings.depth));
  if (const JsonValue* item = node.find("keep_pages")) source.keepPages = item->asBool(settings.keepPages);
  if (const JsonValue* item = node.find("resolve_redirects")) source.resolveRedirects = item->asBool(false);
  if (const JsonValue* item = node.find("trusted")) source.trusted = item->asBool(false);
  if (const JsonValue* item = node.find("console")) source.console = toUpperAscii(item->asString(""));

  source.query = stringList(node.find("query"));
  source.urls = stringList(node.find("urls"));
  source.searchUrl = stringList(node.find("search_url"));
  source.pagedUrl = stringList(node.find("paged_url"));
  source.itemUrl = node.find("item_url") ? node.find("item_url")->asString("") : "";
  source.instances = stringList(node.find("instances"));

  if (source.name.empty()) {
    error = origin + ": source without a name";
    return source;
  }
  if (source.type != "search" && source.type != "index" && source.type != "catalog") {
    error = origin + ": source \"" + source.name + "\" has unknown type \"" + source.type + "\"";
    return source;
  }
  if (source.type == "search" && (source.engine.empty() || source.query.empty())) {
    error = origin + ": search source \"" + source.name + "\" needs engine and query";
    return source;
  }
  if (source.type == "index" && source.urls.empty() && source.searchUrl.empty()) {
    error = origin + ": index source \"" + source.name + "\" needs urls or search_url";
    return source;
  }
  if (source.type == "catalog" && source.urls.empty()) {
    error = origin + ": catalog source \"" + source.name + "\" needs urls";
    return source;
  }
  return source;
}

void mergeDocument(Config& config, const JsonValue& document, const std::string& origin,
                   std::string& error) {
  applyDefaults(config.settings, document);

  const JsonValue* disable = document.find("disable");
  if (disable && disable->isArray()) {
    for (const JsonValue& item : disable->array) {
      if (item.type == JsonValue::Type::String) config.disabled.push_back(item.string);
    }
  }

  for (const JsonValue* node : sourceNodes(document)) {
    if (!node->isObject()) continue;
    Source source = parseSource(*node, config.settings, origin, error);
    if (!error.empty()) return;
    auto existing =
        std::find_if(config.sources.begin(), config.sources.end(),
                     [&](const Source& item) { return item.name == source.name; });
    if (existing != config.sources.end()) {
      *existing = source;
    } else {
      config.sources.push_back(source);
    }
  }
}

}  // namespace

std::string currentExePath() {
  char buffer[MAX_PATH * 4];
  DWORD length = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) - 1));
  if (length == 0 || length >= sizeof(buffer)) return "";
  return std::string(buffer, length);
}

std::string exeDirectory() {
  std::string path = currentExePath();
  size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool loadConfig(Config& out, const std::string& exeDir, const std::string& configArg,
                std::string& error) {
  out = Config{};

  std::vector<std::string> layers;
  std::string builtin = joinPath(exeDir, "config");
  std::string bundled = joinPath(builtin, "sources.json");
  if (exists(bundled)) layers.push_back(bundled);
  for (const std::string& file : listJsonFiles(joinPath(builtin, "sources.d"))) layers.push_back(file);

  std::string user = userConfigDirInternal();
  std::string userFile = joinPath(user, "sources.json");
  if (exists(userFile)) layers.push_back(userFile);
  for (const std::string& file : listJsonFiles(joinPath(user, "sources.d"))) layers.push_back(file);

  std::string explicitPath = configArg;
  if (explicitPath.empty()) {
    const char* env = std::getenv("LINKSCRAPPER_CONFIG");
    if (env && *env) explicitPath = env;
  }
  if (!explicitPath.empty()) {
    if (!exists(explicitPath)) {
      error = "config path not found: " + explicitPath;
      return false;
    }
    if (isDirectory(explicitPath)) {
      std::string file = joinPath(explicitPath, "sources.json");
      if (exists(file)) layers.push_back(file);
      for (const std::string& item : listJsonFiles(joinPath(explicitPath, "sources.d"))) layers.push_back(item);
    } else {
      layers.push_back(explicitPath);
    }
  }

  auto applyText = [&](const std::string& text, const std::string& origin) -> bool {
    JsonValue document;
    if (!parseJson(text, document, error)) {
      error = origin + ": " + error;
      return false;
    }
    mergeDocument(out, document, origin, error);
    return error.empty();
  };

  // the built-ins go down first so a config written by an older build still
  // picks up sources added since; a file's entry replaces one by name, and a
  // parked source stays parked because the file carries enabled:false
  if (!applyText(kEmbeddedConfig, "built-in defaults")) return false;

  for (const std::string& file : layers) {
    std::string text;
    if (!readFile(file, text)) {
      error = "cannot read " + file;
      return false;
    }
    out.files.push_back(file);
    if (!applyText(text, file)) return false;
  }

  std::vector<Source> active;
  for (const Source& source : out.sources) {
    if (source.enabled) {
      if (std::find(out.disabled.begin(), out.disabled.end(), source.name) == out.disabled.end()) {
        active.push_back(source);
        continue;
      }
      Source off = source;
      off.enabled = false;
      out.parked.push_back(off);
      continue;
    }
    out.parked.push_back(source);
  }
  out.sources = active;

  // a source that says "trusted" is a recommendation by name as well as a list
  for (const Source& source : out.sources) {
    if (source.trusted) out.settings.trustedSources.push_back(source.name);
  }
  dedupeInPlace(out.settings.trustedSources);

  out.settings.timeoutMs = std::max(2000, out.settings.timeoutMs);
  out.settings.concurrency = std::max(1, std::min(64, out.settings.concurrency));
  if (out.settings.userAgent.empty()) {
    out.settings.userAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Safari/537.36";
  }
  return true;
}

std::string embeddedConfigText() { return kEmbeddedConfig; }

std::string userConfigDirectory() { return userConfigDirInternal(); }

std::string userConfigFile() { return joinPath(userConfigDirectory(), "sources.json"); }

namespace {

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string quoted(const std::string& value) { return "\"" + jsonEscape(value) + "\""; }

std::string field(const std::string& key, const std::string& value) {
  return "\"" + key + "\": " + value;
}

std::string fieldList(const std::string& key, const std::vector<std::string>& values) {
  if (values.empty()) return std::string();
  std::string out = "\"" + key + "\": [";
  for (size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + quoted(values[i]);
  out += "]";
  return out;
}

std::string indentLines(const std::string& text, const std::string& indent) {
  std::string out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t next = text.find('\n', pos);
    if (next == std::string::npos) next = text.size();
    out += indent + text.substr(pos, next - pos);
    if (next < text.size()) out += "\n";
    pos = next + 1;
  }
  return out;
}

std::string joinFields(const std::vector<std::string>& fields, const std::string& indent) {
  std::vector<std::string> kept;
  for (const std::string& line : fields) {
    if (!line.empty()) kept.push_back(line);
  }
  std::string body;
  for (size_t i = 0; i < kept.size(); ++i) {
    body += indentLines(kept[i], indent);
    if (i + 1 < kept.size()) body += ",";
    body += "\n";
  }
  return body;
}

std::string sourceFields(const Source& source) {
  std::vector<std::string> fields;
  fields.push_back(field("name", quoted(source.name)));
  fields.push_back(field("type", quoted(source.type.empty() ? "index" : source.type)));
  if (!source.engine.empty()) fields.push_back(field("engine", quoted(source.engine)));
  if (!source.console.empty()) fields.push_back(field("console", quoted(source.console)));
  fields.push_back(field("enabled", source.enabled ? "true" : "false"));
  fields.push_back(field("pages", std::to_string(source.pages)));
  fields.push_back(field("depth", std::to_string(source.depth)));
  fields.push_back(field("trusted", source.trusted ? "true" : "false"));
  fields.push_back(field("resolve_redirects", source.resolveRedirects ? "true" : "false"));
  fields.push_back(field("keep_pages", source.keepPages ? "true" : "false"));
  fields.push_back(fieldList("query", source.query));
  fields.push_back(fieldList("urls", source.urls));
  fields.push_back(fieldList("search_url", source.searchUrl));
  fields.push_back(fieldList("paged_url", source.pagedUrl));
  if (!source.itemUrl.empty()) fields.push_back(field("item_url", quoted(source.itemUrl)));
  fields.push_back(fieldList("instances", source.instances));
  return "{\n" + joinFields(fields, "  ") + "    }";
}

}  // namespace

std::string renderConfig(const Config& config) {
  const Settings& s = config.settings;

  std::vector<std::string> defaults;
  defaults.push_back(field("timeout_ms", std::to_string(s.timeoutMs)));
  defaults.push_back(field("concurrency", std::to_string(s.concurrency)));
  defaults.push_back(field("verify", s.verify ? "true" : "false"));
  char threshold[32];
  std::snprintf(threshold, sizeof(threshold), "%.2f", s.threshold);
  defaults.push_back(field("match_threshold", threshold));
  defaults.push_back(field("max_per_game", std::to_string(s.maxPerGame)));
  defaults.push_back(field("max_fetch_per_game", std::to_string(s.maxFetchPerGame)));
  defaults.push_back(field("min_host_delay_ms", std::to_string(s.minHostDelayMs)));
  defaults.push_back(field("keep_pages", s.keepPages ? "true" : "false"));
  defaults.push_back(field("depth", std::to_string(s.depth)));
  defaults.push_back(field("good_ttl_days", std::to_string(s.goodTtlDays)));
  if (!s.userAgent.empty()) defaults.push_back(field("user_agent", quoted(s.userAgent)));
  defaults.push_back(fieldList("trusted_hosts", s.trustedHosts));
  defaults.push_back(fieldList("trusted_sources", s.trustedSources));

  std::vector<std::string> blocks;
  for (const Source& source : config.sources) blocks.push_back(sourceFields(source));
  for (const Source& source : config.parked) blocks.push_back(sourceFields(source));

  std::string out = "{\n  \"version\": 1,\n  \"defaults\": {\n";
  out += joinFields(defaults, "    ");
  out += "  },\n  \"sources\": [\n";
  for (size_t i = 0; i < blocks.size(); ++i) {
    out += indentLines(blocks[i], "    ");
    out += (i + 1 < blocks.size()) ? ",\n" : "\n";
  }
  out += "  ]\n}\n";
  return out;
}
