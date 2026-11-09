#pragma once

#include <string>
#include <vector>

#include "json.h"

struct CatalogLink {
  std::string url;
  std::string label;
};

struct CatalogEntry {
  std::string title;
  std::string id;
  std::string pageUrl;
  std::vector<CatalogLink> links;
};

// a catalog is a json document of named entries carrying download links. both
// shipped shapes reduce to this: pegasus (packages[].downloadLinks[{name,url}])
// and the plain database (array of {name, download_links:{host:[urls]}})
std::vector<CatalogEntry> collectCatalogEntries(const JsonValue& root);