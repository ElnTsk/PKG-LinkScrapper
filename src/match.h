#pragma once

#include <string>
#include <vector>

double scoreTitle(const std::string& query, const std::string& candidate);
std::string detectConsole(const std::string& text);
std::string foreignPlatform(const std::string& text);
std::string titleFromUrl(const std::string& url);
bool isGenericLabel(const std::string& text);

// An anchor that names the shop instead of the parcel - "Gofile", "Mirror",
// "1File (Multi)". The page it sits on is the better title for such a row.
bool isHostLabel(const std::string& text);
// A label that does say something about the parcel - update, dlc, part 3 - kept
// as a tag on the page title rather than replacing it.
std::string kindTag(const std::string& text);
// the title a link should carry: its own words when they describe the file, the
// page title otherwise, the page title plus a tag when the label names a kind
std::string titleFromLink(const std::string& pageTitle, const std::string& anchorText);
bool isBareHostRoot(const std::string& url);
bool looksLikeHandler(const std::string& url);
bool isBlockedHost(const std::string& host);
bool slugHasPlatform(const std::string& slug);
