#pragma once

#include <string>
#include <vector>

#include "crawl.h"

// Multi-part archives (.part01.rar, .001, .z01, split .pkg parts) collapse into
// one row per set. The key is the url with the part index removed, so only a
// genuine sequence merges - a different folder, version or title never does.

bool isPartUrl(const std::string& url);
int partIndexOf(const std::string& url);

// One row per set. A grouped row keeps its members in partUrls (sorted), the
// summed size in sizeBytes, and the best state any member managed.
std::vector<Hit> groupParts(const std::vector<Hit>& hits);

// "21 parts", or empty for a row that isn't a set.
std::string partsLabel(const Hit& hit);

// Folder + sequence signature for a part url; empty when the url isn't a part.
std::string partGroupKey(const std::string& url);

// Trim to a row budget without ever splitting a set: either all of part01..21
// survives or none of it does. Input order decides priority.
std::vector<Hit> trimByGroup(const std::vector<Hit>& hits, size_t rowLimit);
