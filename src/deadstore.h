#pragma once

#include <string>
#include <vector>

// A remembered set of links that are known to be no good: stubs, 404s, hosts
// that never answer. Stored as a plain tsv so it can be read or edited by hand.
// Lives beside the user's other PKG-LinkScrapper state, not next to the exe.

std::string deadStorePath();

// Where PKG-LinkScrapper keeps its learned state - dead.tsv and good.tsv sit here.
std::string stateDirectory();

bool loadDeadStore(std::vector<std::string>& urls);
bool deadStoreContains(const std::vector<std::string>& urls, const std::string& url);
bool appendDeadStore(const std::string& url, const std::string& state, const std::string& host,
                     const std::string& title, std::string& error);
bool clearDeadStore(std::string& error);
size_t deadStoreCount();
