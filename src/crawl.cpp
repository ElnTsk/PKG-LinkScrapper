#include "crawl.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <thread>

#include "catalog.h"
#include "hosts.h"
#include "html.h"
#include "deadstore.h"
#include "http.h"
#include "json.h"
#include "match.h"
#include "parts.h"
#include "ping.h"
#include "trust.h"
#include "verify.h"

namespace {

std::atomic<int64_t> g_huntStartMs{0};
std::atomic<int64_t> g_huntDeadlineMs{0};

bool huntExpired() {
  const int64_t deadline = g_huntDeadlineMs.load();
  return deadline > 0 && nowMs() >= deadline;
}

bool stopRequested() { return abortRequested() || huntExpired(); }

const std::vector<std::string>* g_dead = nullptr;

bool rememberedDead(const std::string& url) {
  return g_dead && deadStoreContains(*g_dead, url);
}

void dedupeInPlace(std::vector<std::string>& values) {
  std::set<std::string> seen;
  std::vector<std::string> out;
  for (const std::string& value : values) {
    if (value.empty() || !seen.insert(value).second) continue;
    out.push_back(value);
  }
  values = out;
}

struct Ctx {
  std::string game;
  std::string pref;
  const Config* config = nullptr;
  ThreadPool* pool = nullptr;
  std::function<void(const Hit&)> onHit;
  double threshold = 0.72;
  int maxFetch = 14;
  size_t hitCeiling = 160;

  std::mutex mutex;
  std::vector<Hit> hits;
  std::set<std::string> seen;
  std::set<std::string> visited;
  std::map<std::string, int> perSource;
  std::vector<std::string> errors;
  int fetched = 0;
};

std::string followRedirects(const std::string& url, int maxHops = 5) {
  std::string current = url;
  for (int hop = 0; hop < maxHops; ++hop) {
    if (stopRequested()) return current;
    HttpResponse res = httpGetRangeNoFollow(current);
    if (res.status >= 300 && res.status < 400) {
      std::string location = res.header("location");
      if (location.empty()) return current;
      std::string next = resolveUrl(current, location);
      if (next.empty()) return current;
      current = next;
      continue;
    }
    return res.finalUrl.empty() ? current : res.finalUrl;
  }
  return current;
}

void recordHit(Ctx& ctx,
               const Source& source,
               const std::string& url,
               const std::string& kind,
               const std::string& title,
               const std::string& consoleHint,
               bool pass,
               double matchScore,
               const std::string& pageUrl) {
  if (!pass) return;
  if (rememberedDead(url)) return;
  if (kind == "filehost" && isBareHostRoot(url)) return;

  std::lock_guard<std::mutex> lock(ctx.mutex);
  if (ctx.hits.size() >= ctx.hitCeiling) return;

  std::string key = ctx.game + "|" + normalizeUrl(url);
  if (ctx.seen.count(key)) return;

  std::string cleanTitle = collapseSpaces(trim(title));
  std::string urlTitle = titleFromUrl(url);
  std::string slug = titleFromUrl(pageUrl.empty() ? url : pageUrl);

  if (slugHasPlatform(slug)) {
    if (!foreignPlatform(slug).empty()) return;
  } else if (!foreignPlatform(cleanTitle + " " + urlTitle).empty()) {
    return;
  }

  std::string detected = consoleHint;
  if (detected.empty()) detected = detectConsole(cleanTitle);
  if (detected.empty()) detected = detectConsole(urlTitle);
  if (slugHasPlatform(slug)) {
    std::string fromSlug = detectConsole(slug);
    if (!fromSlug.empty()) detected = fromSlug;
  }

  if (ctx.pref != "both" && !detected.empty() && detected != toUpperAscii(ctx.pref)) return;

  Hit hit;
  hit.game = ctx.game;
  hit.console = detected.empty() ? "?" : detected;
  hit.kind = kind;
  hit.title = cleanTitle;
  hit.source = source.name;
  hit.site = pageUrl.empty() ? std::string() : registrable(bareHost(pageUrl));
  hit.url = url;
  hit.host = registrable(bareHost(url));
  hit.matchScore = matchScore;
  hit.trust = trustLabel(url, hit.host, source.name);
  ctx.seen.insert(key);
  ctx.hits.push_back(hit);
  if (ctx.onHit) ctx.onHit(hit);
}

void ingestJob(Ctx& ctx,
               const Source* source,
               const std::string& url,
               const std::string& anchorText,
               const std::string& pageTitle,
               bool trusted,
               int depth,
               const std::string& consoleHint);

void submitIngest(Ctx& ctx,
                  const Source* source,
                  std::string url,
                  std::string anchorText,
                  std::string pageTitle,
                  bool trusted,
                  int depth,
                  std::string consoleHint) {
  ctx.pool->submit([&ctx, source, url = std::move(url), anchorText = std::move(anchorText),
                    pageTitle = std::move(pageTitle), trusted, depth,
                    consoleHint = std::move(consoleHint)]() mutable {
    ingestJob(ctx, source, url, anchorText, pageTitle, trusted, depth, consoleHint);
  });
}

// SuperPSX keeps its file links on a companion page (/dll-<code>/) that the
// game page only advertises with a generic label, so the title filter never
// lets it be followed and the row stops at the website. Pull those pages by
// hand and keep the host buttons they carry.
size_t harvestLinkPages(Ctx& ctx,
                        const Source& source,
                        const Page& page,
                        const std::string& baseUrl,
                        const std::string& title,
                        const std::string& hint,
                        double pageScore) {
  const std::string site = registrable(bareHost(baseUrl));
  std::vector<std::string> linkPages;
  for (const PageLink& link : page.links) {
    if (linkPages.size() >= 3) break;
    if (classify(link.href) != "page") continue;
    if (registrable(bareHost(link.href)) != site) continue;
    if (toLowerAscii(crackUrl(link.href).path).find("/dll-") == std::string::npos) continue;
    linkPages.push_back(link.href);
  }

  size_t found = 0;
  for (const std::string& linkPage : linkPages) {
    if (stopRequested()) break;
    {
      std::lock_guard<std::mutex> lock(ctx.mutex);
      if (ctx.fetched >= ctx.maxFetch) return found;
      if (ctx.visited.insert(normalizeUrl(linkPage)).second) ++ctx.fetched;
    }
    HttpResponse res = httpGet(linkPage);
    if (res.status != 200 || res.body.empty()) continue;
    std::string where = res.finalUrl.empty() ? linkPage : res.finalUrl;
    for (const PageLink& anchor : extractPage(res.body, where).links) {
      std::string kind = classify(anchor.href);
      if (!isFileBearing(kind)) continue;
      std::string label = collapseSpaces(anchor.text);
      std::string base = title.empty() ? titleFromUrl(anchor.href) : title;
      std::string hitTitle = label.empty() || iequals(label, base) ? base : base + " [" + label + "]";
      recordHit(ctx, source, anchor.href, kind, hitTitle, hint, true, std::max(pageScore, ctx.threshold),
                where);
      ++found;
    }
  }
  return found;
}

void ingestJob(Ctx& ctx,
               const Source* source,
               const std::string& url,
               const std::string& anchorText,
               const std::string& pageTitle,
               bool trusted,
               int depth,
               const std::string& consoleHint) {
  if (stopRequested()) return;

  std::string current = url;
  std::string kind = classify(current);
  if (kind == "skip") return;

  if (kind == "short") {
    std::string resolved = followRedirects(current);
    if (resolved.empty() || resolved == current) return;
    current = resolved;
    kind = classify(current);
    if (kind == "skip" || kind == "short") return;
  }

  if (isFileBearing(kind)) {
    std::string fallback = anchorText.empty() ? (pageTitle.empty() ? titleFromUrl(current) : pageTitle)
                                              : anchorText;
    double scoreValue = scoreTitle(ctx.game, anchorText + " " + pageTitle + " " + titleFromUrl(current));
    recordHit(ctx, *source, current, kind, collapseSpaces(fallback), consoleHint,
              trusted || scoreValue >= ctx.threshold, std::max(scoreValue, trusted ? 1.0 : 0.0), "");
    return;
  }

  if (depth <= 0) return;

  {
    std::lock_guard<std::mutex> lock(ctx.mutex);
    if (ctx.fetched >= ctx.maxFetch) return;
    // no single source may eat the whole page budget
    const int perSourceCap = std::max(4, ctx.maxFetch / 3);
    if (++ctx.perSource[source->name] > perSourceCap) {
      --ctx.perSource[source->name];
      return;
    }
    if (!ctx.visited.insert(normalizeUrl(current)).second) {
      --ctx.perSource[source->name];
      return;
    }
    ++ctx.fetched;
  }

  HttpResponse res = httpGet(current);
  if (res.status != 200 || res.body.empty()) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "page %ld %s", res.status,
                  res.status ? "" : (res.error.empty() ? "failed" : res.error.c_str()));
    logDebug(std::string(buffer) + " " + current);
    return;
  }

  std::string finalUrl = res.finalUrl.empty() ? current : res.finalUrl;
  Page page = extractPage(res.body, finalUrl);
  std::string title = page.title.empty() ? anchorText : page.title;
  std::string hint = detectConsole(title);
  if (hint.empty()) hint = detectConsole(finalUrl);
  if (hint.empty()) hint = consoleHint;

  double pageScore = scoreTitle(ctx.game, title.empty() ? anchorText : title);
  if (!trusted && pageScore < ctx.threshold) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", pageScore);
    logDebug("page skipped (" + std::string(buffer) + ") " + finalUrl);
    return;
  }

  size_t hostLinks = 0;

  // a link is kept when either the page it sits on matches the title, or the
  // link itself does. search results alone do not confer trust - that is how a
  // single file listing used to answer every query
  const bool pageRelevant = pageScore >= ctx.threshold;
  auto linkMatches = [&](const std::string& href, const std::string& text) {
    return scoreTitle(ctx.game, text + " " + titleFromUrl(href) + " " + title) >= ctx.threshold;
  };
  auto acceptable = [&](const std::string& href, const std::string& text) {
    return pageRelevant || linkMatches(href, text);
  };

  for (const PageLink& link : page.links) {
    if (stopRequested()) break;
    std::string linkKind = classify(link.href);

    if (isFileBearing(linkKind)) {
      if (!acceptable(link.href, link.text)) continue;
      std::string title2 = titleFromLink(title, link.text);
      recordHit(ctx, *source, link.href, linkKind, title2, hint, true,
                std::max(pageScore, ctx.threshold), finalUrl);
      ++hostLinks;
      continue;
    }

    if (linkKind == "short") {
      std::string resolved = followRedirects(link.href);
      std::string resolvedKind = classify(resolved);
      if (!resolved.empty() && isFileBearing(resolvedKind) && acceptable(resolved, link.text)) {
        recordHit(ctx, *source, resolved, resolvedKind,
                  titleFromLink(title, link.text), hint, true,
                  std::max(pageScore, ctx.threshold), finalUrl);
        ++hostLinks;
      }
      continue;
    }

    if (linkKind == "page" && source->resolveRedirects &&
        registrable(bareHost(link.href)) == registrable(bareHost(finalUrl)) &&
        looksLikeHandler(link.href)) {
      std::string resolved = followRedirects(link.href);
      std::string resolvedKind = classify(resolved);
      if (!resolved.empty() && resolved != link.href && isFileBearing(resolvedKind) &&
          acceptable(resolved, link.text)) {
        recordHit(ctx, *source, resolved, resolvedKind,
                  titleFromLink(title, link.text), hint, true,
                  std::max(pageScore, ctx.threshold), finalUrl);
        ++hostLinks;
      }
    }
  }

  hostLinks += harvestLinkPages(ctx, *source, page, finalUrl, title, hint, pageScore);

  if (depth > 1 && ctx.fetched < ctx.maxFetch) {
    for (const PageLink& link : page.links) {
      if (stopRequested()) break;
      if (classify(link.href) != "page") continue;
      if (scoreTitle(ctx.game, link.text) < ctx.threshold) continue;
      submitIngest(ctx, source, link.href, link.text, title, false, depth - 1, hint);
    }
  }

  if (hostLinks == 0 && source->keepPages) {
    // a page only survives this far when the source matched it; the floor keeps
    // a trusted submit (json ids, tracker pages) from being trimmed off the end
    recordHit(ctx, *source, finalUrl, "page", title, hint, true,
              std::max(pageScore, ctx.threshold), finalUrl);
  }
}

std::vector<std::string> searchDuckDuckGo(const std::string& query, int pages) {
  std::vector<std::string> out;

  for (int page = 0; page < pages; ++page) {
    if (stopRequested()) break;
    std::string url = "https://lite.duckduckgo.com/lite/?q=" + percentEncode(query) +
                      "&s=" + std::to_string(page * 10);
    HttpResponse res = httpGet(url);
    if (res.status != 200 || res.body.empty()) break;
    size_t before = out.size();
    for (const RawAnchor& anchor : rawAnchors(res.body)) {
      if (anchor.className.find("result-link") == std::string::npos) continue;
      out.push_back(unwrapDuckDuckGoHref(anchor.href));
    }
    if (out.size() == before) break;
  }

  if (!out.empty()) {
    dedupeInPlace(out);
    return out;
  }

  for (int page = 0; page < pages; ++page) {
    if (stopRequested()) break;
    std::string url = "https://html.duckduckgo.com/html/?q=" + percentEncode(query) +
                      "&s=" + std::to_string(page * 30) + "&dc=" + std::to_string(page * 30 + 1);
    HttpResponse res = httpGet(url);
    if (res.status != 200 || res.body.empty()) break;
    size_t before = out.size();
    for (const RawAnchor& anchor : rawAnchors(res.body)) {
      if (anchor.className.find("result__a") == std::string::npos) continue;
      out.push_back(unwrapDuckDuckGoHref(anchor.href));
    }
    if (out.size() == before) break;
  }

  dedupeInPlace(out);
  return out;
}

std::vector<std::string> searchBing(const std::string& query, int pages) {
  std::vector<std::string> out;

  for (int page = 1; page <= pages; ++page) {
    if (stopRequested()) break;
    std::string url = "https://www.bing.com/search?q=" + percentEncode(query) + "&count=30&first=" +
                      std::to_string((page - 1) * 10 + 1);
    HttpResponse res = httpGet(url);
    if (res.status != 200 || res.body.empty()) break;
    size_t before = out.size();
    std::string self = "bing.com";
    for (const RawAnchor& anchor : rawAnchors(res.body)) {
      std::string href = anchor.href;
      if (!startsWithI(href, "http")) continue;
      std::string host = bareHost(href);
      if (registrable(host) == registrable(self)) {
        std::string decoded = unwrapBingHref(href);
        if (!decoded.empty()) out.push_back(decoded);
      } else {
        out.push_back(href);
      }
    }
    if (out.size() == before) break;
  }

  if (!out.empty()) {
    dedupeInPlace(out);
    return out;
  }

  HttpResponse res = httpGet("https://www.bing.com/search?format=rss&q=" + percentEncode(query) +
                             "&count=30");
  if (res.status == 200) {
    const std::string& body = res.body;
    size_t pos = 0;
    while ((pos = body.find("<link>", pos)) != std::string::npos) {
      size_t end = body.find("</link>", pos);
      if (end == std::string::npos) break;
      std::string href = trim(body.substr(pos + 6, end - pos - 6));
      href = replaceAll(href, "&amp;", "&");
      if (startsWithI(href, "http")) out.push_back(href);
      pos = end;
    }
  }

  dedupeInPlace(out);
  return out;
}

std::vector<std::string> searchSearx(const std::string& query, int pages, const Source& source) {
  std::vector<std::string> instances =
      source.instances.empty() ? std::vector<std::string>{"https://searx.be"} : source.instances;
  std::vector<std::string> out;

  for (const std::string& instance : instances) {
    std::string host = bareHost(instance);
    for (int page = 1; page <= pages; ++page) {
      if (stopRequested()) break;
      std::string url = instance + "/search?q=" + percentEncode(query) + "&pageno=" +
                        std::to_string(page) + "&language=en";
      HttpResponse res = httpGet(url);
      if (res.status != 200 || res.body.empty()) break;
      size_t before = out.size();
      for (const RawAnchor& anchor : rawAnchors(res.body)) {
        if (!startsWithI(anchor.href, "http")) continue;
        std::string anchorHost = bareHost(anchor.href);
        if (registrable(anchorHost) == registrable(host)) continue;
        out.push_back(anchor.href);
      }
      if (out.size() == before) break;
    }
  }

  dedupeInPlace(out);
  return out;
}

std::vector<std::string> engineSearch(const Source& source, const std::string& query, int pages) {
  std::string engine = toLowerAscii(source.engine);
  if (engine == "duckduckgo" || engine == "ddg") return searchDuckDuckGo(query, pages);
  if (engine == "bing") return searchBing(query, pages);
  if (engine == "searx") return searchSearx(query, pages, source);
  logDebug("unknown engine \"" + source.engine + "\" in source " + source.name);
  return {};
}

void runSearch(Ctx& ctx, const Source& source) {
  int emptyInARow = 0;
  for (const std::string& consoleTag : consolesFor(ctx.pref)) {
    for (const std::string& templateText : source.query) {
      if (stopRequested()) return;
      // an engine that is bot-blocked stays blocked; do not burn the whole run on it
      if (emptyInARow >= 2) {
        logDebug(source.name + " skipped - engine returned nothing twice");
        return;
      }
      std::string query = renderTemplate(templateText, ctx.game, consoleTag, 1);
      std::vector<std::string> urls = engineSearch(source, query, source.pages);
      logDebug(source.name + " \"" + query + "\" -> " + std::to_string(urls.size()) + " results");
      emptyInARow = urls.empty() ? emptyInARow + 1 : 0;
      if (urls.empty()) continue;
      for (const std::string& url : urls) {
        submitIngest(ctx, &source, url, "", "", true, source.depth, "");
      }
    }
  }
}

// parsed catalogs outlive a single hunt: the gui runs several in one process
// and each one is a few megabytes of json
std::mutex g_catalogMutex;
std::map<std::string, std::shared_ptr<const std::vector<CatalogEntry>>> g_catalogs;

std::shared_ptr<const std::vector<CatalogEntry>> catalogEntries(const std::string& url) {
  {
    std::lock_guard<std::mutex> lock(g_catalogMutex);
    auto found = g_catalogs.find(url);
    if (found != g_catalogs.end()) return found->second;
  }

  HttpResponse res = httpGetCatalog(url);
  if (res.status != 200 || res.body.empty()) {
    logDebug("catalog " + std::to_string(res.status) + " " +
             (res.error.empty() ? url : res.error + " " + url));
    return nullptr;
  }
  JsonValue document;
  std::string parseError;
  if (!parseJson(res.body, document, parseError)) {
    logDebug("catalog parse failed (" + parseError + ") " + url);
    return nullptr;
  }

  auto entries = std::make_shared<std::vector<CatalogEntry>>(collectCatalogEntries(document));
  logDebug("catalog " + url + " -> " + std::to_string(entries->size()) + " entries");
  std::lock_guard<std::mutex> lock(g_catalogMutex);
  g_catalogs[url] = entries;
  return entries;
}

void runCatalog(Ctx& ctx, const Source& source) {
  const std::string consoleHint =
      source.console.empty() ? std::string() : toUpperAscii(source.console);
  // a ps4 catalog cannot answer a ps5 hunt - do not spend the download on it
  if (!consoleHint.empty() && ctx.pref != "both" && consoleHint != toUpperAscii(ctx.pref)) {
    logDebug(source.name + " skipped - catalog is " + consoleHint + ", hunt is " + ctx.pref);
    return;
  }
  for (const std::string& templateUrl : source.urls) {
    if (stopRequested()) return;
    std::string url = renderTemplate(templateUrl, ctx.game, consoleHint, 1);
    auto entries = catalogEntries(url);
    if (!entries) continue;

    size_t matched = 0;
    size_t emitted = 0;
    for (const CatalogEntry& entry : *entries) {
      if (stopRequested()) return;
      double score = scoreTitle(ctx.game, entry.title + " " + entry.id);
      if (score < ctx.threshold) continue;
      ++matched;

      for (const CatalogLink& link : entry.links) {
        std::string kind = classify(link.url);
        if (kind == "skip") continue;
        std::string label = collapseSpaces(link.label);
        // the catalog's own words: "USA Base - Akirabox", "LZ4 · Viki - pt.1".
        // the host half repeats the Host column but the variant half is the
        // only place a part number or region survives
        std::string title = entry.title;
        if (!label.empty() && !iequals(label, entry.title) && label.size() <= 80) {
          title += " [" + label + "]";
        }
        if (isFileBearing(kind)) {
          recordHit(ctx, source, link.url, kind, title, consoleHint, true,
                    std::max(score, ctx.threshold), entry.pageUrl);
          ++emitted;
        } else if (kind == "page" || kind == "short") {
          submitIngest(ctx, &source, link.url, title, entry.title, true, source.depth,
                       consoleHint);
        }
      }
    }
    logDebug(source.name + " catalog " + url + " -> " + std::to_string(emitted) +
             " links from " + std::to_string(matched) + " matching entries");
  }
}

std::vector<std::string> indexUrls(const Ctx& ctx, const Source& source) {
  std::vector<std::string> urls;
  for (const std::string& consoleTag : consolesFor(ctx.pref)) {
    for (const std::string& templateText : source.urls) {
      urls.push_back(renderTemplate(templateText, ctx.game, consoleTag, 1));
    }
    for (const std::string& templateText : source.searchUrl) {
      for (int page = 1; page <= source.pages; ++page) {
        if (page == 1) {
          urls.push_back(renderTemplate(templateText, ctx.game, consoleTag, page));
        } else if (!source.pagedUrl.empty()) {
          for (const std::string& paged : source.pagedUrl) {
            urls.push_back(renderTemplate(paged, ctx.game, consoleTag, page));
          }
        }
      }
    }
  }
  dedupeInPlace(urls);
  return urls;
}

void collectIdentifiers(const JsonValue& node, std::vector<std::pair<std::string, std::string>>& out) {
  if (node.isObject()) {
    const JsonValue* id = nullptr;
    for (const char* key : {"identifier", "id", "slug", "dir", "titleid", "title_id"}) {
      id = node.find(key);
      if (id && id->type == JsonValue::Type::String && !id->string.empty()) break;
      id = nullptr;
    }
    if (id) {
      std::string title;
      for (const char* key : {"title", "name", "label"}) {
        const JsonValue* found = node.find(key);
        if (found && found->type == JsonValue::Type::String && !found->string.empty()) {
          title = found->string;
          break;
        }
      }
      out.emplace_back(id->string, title);
    }
    for (const auto& entry : node.object) collectIdentifiers(entry.second, out);
    return;
  }
  if (node.isArray()) {
    for (const JsonValue& item : node.array) collectIdentifiers(item, out);
  }
}

void runIndex(Ctx& ctx, const Source& source) {
  for (const std::string& listingUrl : indexUrls(ctx, source)) {
    if (stopRequested()) return;
    HttpResponse res = httpGet(listingUrl);
    if (res.status != 200 || res.body.empty()) {
      logDebug(source.name + " listing " + std::to_string(res.status) + " " + listingUrl);
      continue;
    }

    Page page = extractPage(res.body, res.finalUrl.empty() ? listingUrl : res.finalUrl);

    // some indexes hand back identifiers instead of links; the source says how
    // to turn one into a url worth fetching
    if (!source.itemUrl.empty()) {
      JsonValue root;
      std::string parseError;
      if (parseJson(res.body, root, parseError)) {
        std::vector<std::pair<std::string, std::string>> found;
        collectIdentifiers(root, found);
        size_t built = 0;
        for (const auto& entry : found) {
          const std::string& id = entry.first;
          std::string readable = replaceAll(id, "-", " ");
          std::string title = entry.second;
          if (readable.size() < 4 && title.empty()) continue;
          if (std::max(scoreTitle(ctx.game, readable), scoreTitle(ctx.game, title)) < ctx.threshold) {
            continue;
          }
          std::string url = replaceAll(source.itemUrl, "{id}", id);
          std::string hint = detectConsole(readable + " " + title);
          if (hint.empty()) hint = toUpperAscii(source.console);
          submitIngest(ctx, &source, url, title.empty() ? readable : title, "", true, source.depth,
                       hint);
          ++built;
        }
        logDebug(source.name + " ids " + listingUrl + " -> " + std::to_string(built) + " of " +
                 std::to_string(found.size()));
      }
    }

    std::string listingTitle = page.title;
    std::string listingConsole = detectConsole(listingTitle);
    if (listingConsole.empty()) listingConsole = toUpperAscii(source.console);
    size_t matched = 0;

    for (const PageLink& link : page.links) {
      if (stopRequested()) break;
      std::string kind = classify(link.href);

      if (isFileBearing(kind)) {
        double anchorScore = scoreTitle(ctx.game, link.text);
        double pageScore = scoreTitle(ctx.game, listingTitle) * 0.6;
        if (std::max(anchorScore, pageScore) >= ctx.threshold) {
          recordHit(ctx, source, link.href, kind,
                    link.text.empty() ? listingTitle : link.text, listingConsole, true,
                    std::max(anchorScore, pageScore), listingUrl);
          ++matched;
        }
        continue;
      }

      if (kind == "page" || kind == "short") {
        // telegram and friends print the raw url as the anchor text; the slug is
        // the only part that carries the game name
        std::string slug = titleFromUrl(link.href);
        std::string label = isGenericLabel(link.text) && !slug.empty() ? slug : link.text;
        double anchorScore = scoreTitle(ctx.game, label.empty() ? slug : label);
        if (anchorScore >= ctx.threshold) {
          submitIngest(ctx, &source, link.href, label, listingTitle, false, source.depth,
                       listingConsole);
          ++matched;
        }
      }
    }

    logDebug(source.name + " listing " + listingUrl + " -> " + std::to_string(matched) +
             " matching links");
  }
}

}  // namespace

void setDeadStore(const std::vector<std::string>* urls) { g_dead = urls; }

std::vector<std::string> consolesFor(const std::string& consolePref) {
  if (consolePref == "both") return {"PS4", "PS5"};
  return {toUpperAscii(consolePref)};
}

std::string renderTemplate(const std::string& tpl, const std::string& query,
                           const std::string& consoleTag, int page) {
  std::string out = replaceAll(tpl, "{q_raw}", query);
  out = replaceAll(out, "{q}", percentEncode(query));
  out = replaceAll(out, "{console}", consoleTag);
  out = replaceAll(out, "{page}", std::to_string(page));
  return out;
}

int64_t huntStartedAtMs() { return g_huntStartMs.load(); }

void warmCatalogs(const Config& config) {
  std::set<std::string> urls;
  for (const Source& source : config.sources) {
    if (source.type != "catalog") continue;
    for (const std::string& templateUrl : source.urls) urls.insert(templateUrl);
  }
  // one thread per catalog; six of them at once is the whole list
  std::vector<std::thread> threads;
  for (const std::string& url : urls) {
    threads.emplace_back([url] {
      if (!abortRequested()) catalogEntries(url);
    });
  }
  for (std::thread& thread : threads) thread.join();
}

void huntMany(const std::vector<std::string>& games, const std::string& consolePref,
              const Config& config,
              const std::function<void(size_t, std::vector<Hit>)>& onGameDone,
              const std::function<void(const Hit&)>& onHit) {
  const int64_t huntStarted = nowMs();
  g_huntStartMs = huntStarted;
  g_huntDeadlineMs = huntStarted + kHuntBudgetMs;

  const size_t inFlight = std::min<size_t>(3, games.size());
  CountingSemaphore slots(static_cast<int>(std::max<size_t>(1, inFlight)));
  std::vector<std::thread> threads;
  threads.reserve(games.size());
  resetProbeCache();
  clearPingCache();

  for (size_t index = 0; index < games.size(); ++index) {
    threads.emplace_back([&, index] {
      slots.acquire();
      struct Release {
        CountingSemaphore* slots;
        ~Release() { slots->release(); }
      } release{&slots};

      if (stopRequested()) return;

      ThreadPool pool(config.settings.concurrency + 4);
      HuntResult result = huntGame(games[index], consolePref, config, pool, onHit);

      std::stable_sort(result.hits.begin(), result.hits.end(),
                       [](const Hit& a, const Hit& b) { return a.matchScore > b.matchScore; });
      const size_t limit = static_cast<size_t>(std::max(1, config.settings.maxPerGame));
      if (result.hits.size() > limit) {
        // every source that answered keeps a seat before score order fills the
        // rest - otherwise one catalog with thirty rows erases the other sources
        std::vector<Hit> seated;
        std::set<std::string> sources;
        std::vector<bool> taken(result.hits.size(), false);
        for (size_t i = 0; i < result.hits.size() && seated.size() < limit; ++i) {
          if (sources.insert(result.hits[i].source).second) {
            seated.push_back(result.hits[i]);
            taken[i] = true;
          }
        }
        for (size_t i = 0; i < result.hits.size() && seated.size() < limit; ++i) {
          if (taken[i]) continue;
          seated.push_back(result.hits[i]);
          taken[i] = true;
        }
        result.hits = trimByGroup(seated, limit);
      }

      // a release page is the fallback, not an answer: once any real link for
      // the game landed, the shop page row only gets clicked by mistake
      bool haveFileLink = false;
      for (const Hit& hit : result.hits) {
        if (hit.kind != "page") {
          haveFileLink = true;
          break;
        }
      }
      if (haveFileLink) {
        result.hits.erase(
            std::remove_if(result.hits.begin(), result.hits.end(),
                           [](const Hit& hit) { return hit.kind == "page"; }),
            result.hits.end());
      }

      if (config.settings.verify && !result.hits.empty() && !stopRequested()) {
        verifyAll(result.hits, pool, g_huntDeadlineMs.load());
        if (onHit) {
          for (const Hit& hit : result.hits) onHit(hit);
        }
      }

      onGameDone(index, std::move(result.hits));
    });
  }

  for (std::thread& thread : threads) thread.join();
  g_huntDeadlineMs = 0;
}

HuntResult huntGame(const std::string& game, const std::string& consolePref, const Config& config,
                    ThreadPool& pool, const std::function<void(const Hit&)>& onHit) {
  Ctx ctx;
  ctx.game = game;
  ctx.pref = consolePref;
  ctx.config = &config;
  ctx.pool = &pool;
  ctx.onHit = onHit;
  ctx.threshold = config.settings.threshold;
  ctx.maxFetch = config.settings.maxFetchPerGame;
  ctx.hitCeiling = static_cast<size_t>(std::max(1, config.settings.maxPerGame)) * 4;

  long long started = nowMs();
  for (const Source& source : config.sources) {
    const Source* pointer = &source;
    pool.submit([&ctx, pointer] {
      if (pointer->type == "search") {
        runSearch(ctx, *pointer);
      } else if (pointer->type == "catalog") {
        runCatalog(ctx, *pointer);
      } else {
        runIndex(ctx, *pointer);
      }
    });
  }
  pool.waitForIdle(120000);
  if (pool.timedOut()) {
    std::lock_guard<std::mutex> lock(ctx.mutex);
    ctx.errors.push_back(game + ": crawl deadline reached, returning what was found");
  }

  HuntResult result;
  result.hits = std::move(ctx.hits);
  result.pagesCrawled = ctx.fetched;
  result.elapsedMs = nowMs() - started;
  result.errors = ctx.errors;
  return result;
}
