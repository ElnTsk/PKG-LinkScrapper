#pragma once

#include <string>
#include <vector>

// Which side of a link is a file, which is a page, which is nothing at all.
// Kept free of windows code so the selftest can exercise it directly.

std::string bareHost(const std::string& url);

// lower-case suffix test, shared by the host lists and the resolvers
bool endsWithText(const std::string& value, const std::string& suffix);

bool isAssetPath(const std::string& path);
bool hasFileExtension(const std::string& path);
bool looksLikeSharePath(const std::string& path, const std::string& query);

// skip | direct | filehost | container | short | page
std::string classify(const std::string& url);
bool isFileBearing(const std::string& kind);

// Hosts whose share url answers with a web page - a listing, a wait screen or
// a captcha gate - instead of the file's bytes. Their links are always read
// before they are believed, and never taken on trust from the good store.
bool shareServesPage(const std::string& host);

// True when host is one of the listed domains or a subdomain of one. Entries
// are bare, lower-case, no leading www - "dlget.ir" covers "dl2.dlget.ir".
bool hostInList(const std::string& host, const std::vector<std::string>& list);
