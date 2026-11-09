#pragma once

#include <string>
#include <vector>

#include "crawl.h"

void sortHits(std::vector<Hit>& hits, const std::vector<std::string>& gameOrder);
std::string renderHits(const std::vector<Hit>& hits, const std::string& format);
std::string summarize(const std::vector<Hit>& hits, size_t gameCount);
