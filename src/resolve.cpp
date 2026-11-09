#include "resolve.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "config.h"
#include "html.h"
#include "hosts.h"
#include "http.h"
#include "util.h"

namespace {

// site accounts: a cookie pasted once into cookies.txt lets the walks below ask
// as the signed-in visitor the site expects
std::string siteCookie(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return std::string();
  std::string host = registrable(parts.host);
  std::ifstream stream(userConfigDirectory() + "\\cookies.txt");
  std::string line;
  while (std::getline(stream, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;
    size_t tab = trimmed.find('	');
    if (tab == std::string::npos) continue;
    if (toLowerAscii(trim(trimmed.substr(0, tab))) == host) {
      return trim(trimmed.substr(tab + 1));
    }
  }
  return std::string();
}

bool looksLikeFile(const HttpResponse& response) {
  std::string type = toLowerAscii(response.header("content-type"));
  if (type.find("text/html") != std::string::npos) return false;
  if (type.find("octet-stream") != std::string::npos) return true;
  if (type.find("application/") != std::string::npos && type.find("json") == std::string::npos &&
      type.find("javascript") == std::string::npos) {
    return true;
  }
  return !response.header("content-disposition").empty();
}

// one ranged byte says whether an endpoint really serves the file
DirectLink probeDirect(const std::string& url, const std::string& cookie, const std::string& name) {
  DirectLink link;
  std::vector<std::string> headers{"Range: bytes=0-0"};
  if (!cookie.empty()) headers.push_back("Cookie: " + cookie);
  HttpResponse probe = httpGetWith(url, headers);
  if (probe.status >= 200 && probe.status < 300 && looksLikeFile(probe)) {
    link.ok = true;
    link.url = url;
    link.name = name;
  } else if (probe.status == 0) {
    link.note = "the host did not answer";
  } else {
    link.note = "the link answered with a page, not the file";
  }
  return link;
}

DirectLink oneFichier(const std::string& url) {
  DirectLink link;
  HttpResponse form = httpPostForm(url, "op=download2&dl_no_ssl=on", std::string(), siteCookie(url));
  if (form.status == 0) {
    link.note = "the download form did not answer";
    return link;
  }
  if (form.status >= 400) {
    link.note = "the download form answered http " + std::to_string(form.status);
    return link;
  }

  link.name = classText(form.body, "tier-name");
  link.url = mirrorLink(form.body, "1fichier.com");
  if (!link.url.empty()) {
    link.ok = true;
    return link;
  }

  std::string lower = toLowerAscii(form.body);
  // the site now hands anonymous visitors an account wall instead of the mirror
  if (lower.find("sign in and download") != std::string::npos ||
      lower.find("login.pl") != std::string::npos) {
    link.note = "1fichier now asks for an account - open the link, sign in, and use its button";
    return link;
  }
  size_t links = 0;
  size_t here = 0;
  for (const RawAnchor& anchor : rawAnchors(form.body)) {
    ++links;
    if (endsWithText(bareHost(anchor.href), "1fichier.com")) ++here;
  }
  if (lower.find("not found") != std::string::npos || lower.find("n'existe") != std::string::npos ||
      lower.find("has been deleted") != std::string::npos) {
    link.note = "the host says the file is gone";
  } else {
    // the counts say whether the answer carried no links at all, or links that
    // this walk no longer recognises - the second case is a bug, not a queue
    link.note =
        "the host returned its page instead of a link - free pulls are queued after one "
        "download, try again in a few minutes (" +
        std::to_string(links) + " links on the page, " + std::to_string(here) +
        " from this host)";
  }
  return link;
}

// rootz prints a signed token in the page data; the download endpoint wants a
// signed-in visitor, so the walk only runs when a cookie is configured
DirectLink rootz(const std::string& url) {
  DirectLink link;
  const std::string cookie = siteCookie(url);
  HttpResponse page = httpGetWith(url, cookie.empty() ? std::vector<std::string>{}
                                                      : std::vector<std::string>{"Cookie: " + cookie});
  if (page.status != 200 || page.body.empty()) {
    link.note = "the share page did not answer";
    return link;
  }
  std::string token;
  size_t pos = 0;
  while ((pos = page.body.find("\\\"", pos)) != std::string::npos) {
    size_t end = page.body.find("\\\"", pos + 2);
    if (end == std::string::npos) break;
    std::string candidate = page.body.substr(pos + 2, end - pos - 2);
    if (candidate.size() > 24 && candidate.find('.') != std::string::npos &&
        candidate.find('<') == std::string::npos && candidate.find(' ') == std::string::npos &&
        candidate.find('/') == std::string::npos) {
      token = candidate;
      break;
    }
    pos = end + 2;
  }
  if (token.empty()) {
    link.note = "no download token on the page";
    return link;
  }
  std::string endpoint = "https://www.rootz.so/api/download/" + token;
  if (cookie.empty()) {
    link.note = "rootz wants a signed-in visitor - paste its cookie into cookies.txt";
    return link;
  }
  return probeDirect(endpoint, cookie, std::string());
}

// vikingfile answers ?download=1 with the file once the visitor is signed in
DirectLink vikingfile(const std::string& url) {
  DirectLink link;
  const std::string cookie = siteCookie(url);
  if (cookie.empty()) {
    link.note = "vikingfile wants a signed-in visitor - paste its cookie into cookies.txt";
    return link;
  }
  std::string target = url;
  if (target.find('?') == std::string::npos) target += "?download=1";
  return probeDirect(target, cookie, std::string());
}

}  // namespace

bool resolvableHost(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return false;
  std::string host = registrable(parts.host);
  return host == "1fichier.com" || host == "rootz.so" || host == "vikingfile.com" ||
         host == "vik1ngfile.site";
}

DirectLink resolveDirect(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) {
    DirectLink link;
    link.note = "not an http url";
    return link;
  }
  std::string host = registrable(parts.host);
  if (host == "1fichier.com") return oneFichier(url);
  if (host == "rootz.so") return rootz(url);
  if (host == "vikingfile.com" || host == "vik1ngfile.site") return vikingfile(url);

  DirectLink link;
  link.note = "no walk implemented for this host";
  return link;
}
