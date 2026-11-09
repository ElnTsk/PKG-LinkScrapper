#include "catalog.h"

#include <set>

#include "util.h"

namespace {

std::string lowerKey(const std::string& key) {
  std::string out = toLowerAscii(key);
  out = replaceAll(out, "_", "");
  out = replaceAll(out, "-", "");
  return out;
}

bool isTitleKey(const std::string& key) {
  return key == "title" || key == "name" || key == "game" || key == "gamename";
}

bool isIdKey(const std::string& key) {
  return key == "titleid" || key == "id" || key == "cusa" || key == "ppsa";
}

bool isPageKey(const std::string& key) {
  return key == "pageurl" || key == "downloadsource" || key == "sourceurl";
}

// a field that holds one url, as opposed to a page about the file
bool isLinkKey(const std::string& key) {
  return key == "url" || key == "link" || key == "href" || key == "downloadurl" ||
         key == "fileurl";
}

// a field whose subtree is made of links; poster and description urls stay out
bool isContainerKey(const std::string& key) {
  return key == "downloadlinks" || key == "links" || key == "urls" || key == "mirrors" ||
         key == "downloads" || key == "files";
}

// inside=false is the entry itself: its own "name" is the game, not a mirror
// label, so the label starts empty and is picked up one level down
void harvestLinks(const JsonValue& node, bool inside, const std::string& label,
                  std::vector<CatalogLink>& out) {
  if (node.isString()) {
    if (inside && startsWithI(node.string, "http")) out.push_back({node.string, label});
    return;
  }
  if (node.isArray()) {
    for (const JsonValue& item : node.array) harvestLinks(item, inside, label, out);
    return;
  }
  if (!node.isObject()) return;

  std::string ownLabel = label;
  if (inside) {
    for (const auto& entry : node.object) {
      std::string key = lowerKey(entry.first);
      if ((key == "name" || key == "label") && entry.second.isString() &&
          !entry.second.string.empty()) {
        ownLabel = collapseSpaces(entry.second.string);
        break;
      }
    }
  }
  for (const auto& entry : node.object) {
    std::string key = lowerKey(entry.first);
    if (isLinkKey(key) && entry.second.isString() && startsWithI(entry.second.string, "http")) {
      out.push_back({entry.second.string, ownLabel});
    }
  }
  for (const auto& entry : node.object) {
    std::string key = lowerKey(entry.first);
    if (isLinkKey(key)) continue;
    if (inside || isContainerKey(key)) harvestLinks(entry.second, true, ownLabel, out);
  }
}

bool holdsOwnLink(const JsonValue& node) {
  for (const auto& entry : node.object) {
    if (isLinkKey(lowerKey(entry.first)) && entry.second.isString() &&
        startsWithI(entry.second.string, "http")) {
      return true;
    }
  }
  return false;
}

void walkEntries(const JsonValue& node, bool insideContainer, std::vector<CatalogEntry>& out) {
  if (node.isArray()) {
    for (const JsonValue& item : node.array) walkEntries(item, insideContainer, out);
    return;
  }
  if (!node.isObject()) return;

  // {"name": "USA Base - Akirabox", "url": "..."} inside downloadLinks is a
  // mirror, not a game; the same shape at the top of a document is an entry
  const bool linkObject = insideContainer && holdsOwnLink(node);

  CatalogEntry entry;
  if (!linkObject) {
    for (const auto& field : node.object) {
      if (!field.second.isString() || field.second.string.empty()) continue;
      std::string key = lowerKey(field.first);
      if (entry.title.empty() && isTitleKey(key)) entry.title = collapseSpaces(field.second.string);
      if (entry.id.empty() && isIdKey(key)) entry.id = field.second.string;
      if (entry.pageUrl.empty() && isPageKey(key) && startsWithI(field.second.string, "http")) {
        entry.pageUrl = field.second.string;
      }
    }
  }
  if (!entry.title.empty()) {
    harvestLinks(node, false, "", entry.links);
    std::set<std::string> seen;
    size_t kept = 0;
    for (const CatalogLink& link : entry.links) {
      if (seen.insert(link.url).second) entry.links[kept++] = link;
    }
    entry.links.resize(kept);
    if (!entry.links.empty()) out.push_back(std::move(entry));
  }
  for (const auto& field : node.object) {
    walkEntries(field.second, isContainerKey(lowerKey(field.first)), out);
  }
}

}  // namespace

std::vector<CatalogEntry> collectCatalogEntries(const JsonValue& root) {
  std::vector<CatalogEntry> out;
  walkEntries(root, false, out);
  return out;
}