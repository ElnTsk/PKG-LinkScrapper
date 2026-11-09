#include "resolve.h"

#include <string>
#include <vector>

#include "html.h"
#include "hosts.h"
#include "http.h"
#include "util.h"

namespace {

DirectLink oneFichier(const std::string& url) {
  DirectLink link;
  HttpResponse form = httpPostForm(url, "op=download2&dl_no_ssl=on");
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

}  // namespace

bool resolvableHost(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) return false;
  return registrable(parts.host) == "1fichier.com";
}

DirectLink resolveDirect(const std::string& url) {
  UrlParts parts = crackUrl(url);
  if (!parts.valid) {
    DirectLink link;
    link.note = "not an http url";
    return link;
  }
  if (registrable(parts.host) == "1fichier.com") return oneFichier(url);

  DirectLink link;
  link.note = "no walk implemented for this host";
  return link;
}
