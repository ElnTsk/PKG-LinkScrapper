#include "hosts.h"

#include <cctype>

#include "match.h"
#include "util.h"

bool endsWithText(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

namespace {

bool endsWithAny(const std::string& value, const char* const* list, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (endsWithText(value, list[i])) return true;
  }
  return false;
}

const std::vector<std::string>& fileHosts() {
  static const std::vector<std::string> hosts = {
      "1fichier.com", "mega.nz",           "mega.co.nz",   "mediafire.com", "drive.google.com",
      "docs.google.com", "pixeldrain.com", "gofile.io",    "buzzheavier.com", "qiwi.gg",
      "krakenfiles.com", "ddownload.com",  "ranoz.gg",     "1cloudfile.com", "akirabox.com",
      "akirabox.to",     "vikingfile.com", "rootz.so",     "send.cm",        "mixdrop.co",
      "mixdrop.ag",      "rapidgator.net", "nitroflare.com", "uploadhaven.com", "workupload.com",
      "anonfiles.com",   "bayfiles.com",   "file.io",      "transfer.sh",    "catbox.moe",
      "litterbox.catbox.moe", "fileditch.com", "temp.sh",  "oshi.at",        "bashupload.com",
      "bowfile.com",     "hexupload.net",  "upload.ee",    "usersdrive.com", "upfiles.com",
      "orangedl.com",    "up-4ever.com",   "dlget.ir",     "thesuperboffers.com",
      "mocha.my",        "itsfileditchfiles.st", "datanodes.to", "datanodes.co",
      "filekeeper.net",  "datavaults.co",  "fileditchfiles.st", "fuckingfast.net",
      "1024terabox.com", "link-vault.org"};
  return hosts;
}

const std::vector<std::string>& containerHosts() {
  static const std::vector<std::string> hosts = {"filecrypt.cc", "filecrypt.co", "filecrypt.to",
                                                 "filecrypt.sx"};
  return hosts;
}

const std::vector<std::string>& shortHosts() {
  static const std::vector<std::string> hosts = {"bit.ly",        "goo.gl",      "cutt.ly",
                                                 "tinyurl.com",   "t.co",        "shrinkearn.com",
                                                 "shrinkme.io",   "shrinkme.ink", "tii.ai",
                                                 "clk.sh",        "shorturl.at", "rebrand.ly",
                                                 "gplinks.co",    "adfoc.us",    "fc.lc",
                                                 "short.pe",      "za.gl",       "ouo.io",
                                                 "linkvertise.com", "lnk.to",     "tpi.li",
                                                 "tvi.la",        "exe.io",      "ouo.press",
                                                 "url4b.com",     "gagangle.com"};
  return hosts;
}

const char* const kAssetExtensions[] = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".ico",
                                        ".svg", ".css",  ".js",  ".mp4", ".mkv",  ".webm", ".woff",
                                        ".woff2", ".ttf", ".avif"};

const char* const kFileExtensions[] = {".pkg", ".exfat", ".rar", ".zip", ".7z", ".split", ".bin",
                                       ".iso", ".tar",   ".gz",  ".001", ".part0", ".mpq"};

const char* const kShareMarkers[] = {"/file/", "/files/", "/f/", "/d/", "/dl/", "/get/",
                                     "/download/", "/downloads/", "/archives/", "/users/",
                                     "/direct/", "/goto/", "/share/", "/link/", "/s/"};

bool hostIn(const std::string& host, const std::vector<std::string>& list) {
  for (const std::string& entry : list) {
    if (host == entry || endsWithText(host, "." + entry)) return true;
  }
  return false;
}

}  // namespace

std::string bareHost(const std::string& url) {
  UrlParts parts = crackUrl(url);
  std::string host = parts.host;
  if (startsWithI(host, "www.")) host = host.substr(4);
  return host;
}

bool isAssetPath(const std::string& path) {
  return endsWithAny(path, kAssetExtensions, sizeof(kAssetExtensions) / sizeof(kAssetExtensions[0]));
}

bool hasFileExtension(const std::string& path) {
  return endsWithAny(path, kFileExtensions, sizeof(kFileExtensions) / sizeof(kFileExtensions[0]));
}

// being on a file host is not enough - cover art, banners and guide pages live
// there too, and they are not what anybody asked for
bool looksLikeSharePath(const std::string& path, const std::string& query) {
  if (isAssetPath(path)) return false;
  for (const char* marker : kShareMarkers) {
    if (path.find(marker) != std::string::npos) return true;
  }
  if (!query.empty()) return true;
  if (endsWithAny(path, kFileExtensions, sizeof(kFileExtensions) / sizeof(kFileExtensions[0]))) {
    return true;
  }

  std::vector<std::string> segments;
  size_t pos = 0;
  while (pos < path.size()) {
    size_t slash = path.find('/', pos);
    if (slash == std::string::npos) slash = path.size();
    if (slash > pos) segments.push_back(path.substr(pos, slash - pos));
    pos = slash + 1;
  }
  if (segments.empty() || segments.size() > 2) return false;

  const std::string& last = segments.back();
  if (last.size() < 6 || last.find('.') != std::string::npos) return false;
  for (char c : last) {
    if (!std::isalnum(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string classify(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return "skip";

  std::string host = bareHost(url);
  if (isBlockedHost(host)) return "skip";

  std::string path = toLowerAscii(parts.path);
  if (isAssetPath(path)) return "skip";
  if (endsWithText(path, ".pkg") || endsWithText(path, ".exfat") ||
      (path.size() > 4 && path.find(".pkg.part") != std::string::npos)) {
    return "direct";
  }
  if (hostIn(host, fileHosts())) {
    return looksLikeSharePath(path, parts.query) ? "filehost" : "page";
  }
  if (hostIn(host, containerHosts())) return "container";
  if (hostIn(host, shortHosts())) return "short";
  return "page";
}

bool isFileBearing(const std::string& kind) {
  return kind == "direct" || kind == "filehost" || kind == "container";
}

const std::vector<std::string>& pageStyleHosts() {
  // measured, not guessed: each of these answers a share url with markup that
  // has to be read before the link means anything
  static const std::vector<std::string> hosts = {
      "gofile.io",        "vikingfile.com", "vik1ngfile.site", "1fichier.com", "rootz.so",
      "mocha.my",         "mediafire.com",  "buzzheavier.com", "akirabox.com", "akirabox.to",
      "mixdrop.co",       "mixdrop.ag",     "drive.google.com", "docs.google.com",
      "thesuperboffers.com", "qiwi.gg",     "uploadhaven.com", "ddownload.com",
      "datavaults.co",    "1024terabox.com"};
  return hosts;
}

bool shareServesPage(const std::string& host) {
  return hostIn(host, pageStyleHosts());
}

bool hostInList(const std::string& host, const std::vector<std::string>& list) {
  if (host.empty() || list.empty()) return false;
  std::string lower = toLowerAscii(host);
  if (startsWithI(lower, "www.")) lower = lower.substr(4);
  std::vector<std::string> entries;
  entries.reserve(list.size());
  for (const std::string& entry : list) {
    std::string value = toLowerAscii(collapseSpaces(trim(entry)));
    if (startsWithI(value, "www.")) value = value.substr(4);
    if (!value.empty()) entries.push_back(value);
  }
  return hostIn(lower, entries);
}
