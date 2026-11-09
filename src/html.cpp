#include "html.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "json.h"
#include "util.h"

namespace {

const char* kUrlKeys[] = {"url", "urls", "link", "links", "download", "downloads",
                          "href", "pkg", "path", "src", "file", "mirror", "mirrors"};
const char* kNameKeys[] = {"name", "title", "label", "game", "slug", "id"};

bool isUrlValue(const std::string& value) {
  return startsWithI(value, "http://") || startsWithI(value, "https://");
}

void walkJson(const JsonValue& node, const std::string& inheritedName, Page& page,
              std::vector<std::string>& seen);

std::string nameFrom(const JsonValue& object) {
  for (const char* key : kNameKeys) {
    const JsonValue* found = object.find(key);
    if (found && found->type == JsonValue::Type::String && !found->string.empty()) {
      return collapseSpaces(trim(found->string));
    }
  }
  return "";
}

void collectUrls(const JsonValue& node, const std::string& name, Page& page,
                 std::vector<std::string>& seen) {
  if (node.type == JsonValue::Type::String) {
    if (!isUrlValue(node.string)) return;
    if (std::find(seen.begin(), seen.end(), node.string) == seen.end()) {
      seen.push_back(node.string);
      page.links.push_back({node.string, name});
    }
    return;
  }
  if (node.isArray()) {
    for (const JsonValue& item : node.array) collectUrls(item, name, page, seen);
  }
}

void walkJson(const JsonValue& node, const std::string& inheritedName, Page& page,
              std::vector<std::string>& seen) {
  if (page.links.size() >= 5000) return;

  if (node.isObject()) {
    std::string name = nameFrom(node);
    if (name.empty()) name = inheritedName;
    for (const char* key : kUrlKeys) {
      const JsonValue* found = node.find(key);
      if (found) collectUrls(*found, name, page, seen);
    }
    for (const auto& entry : node.object) walkJson(entry.second, name, page, seen);
    return;
  }

  if (node.isArray()) {
    for (const JsonValue& item : node.array) walkJson(item, inheritedName, page, seen);
    return;
  }

  if (node.type == JsonValue::Type::String && isUrlValue(node.string)) {
    collectUrls(node, inheritedName, page, seen);
  }
}

}  // namespace

bool looksLikeJson(const std::string& body) {
  std::string head = trim(body.substr(0, std::min<size_t>(body.size(), 512)));
  if (head.empty()) return false;
  if (head[0] != '{' && head[0] != '[') return false;
  return head.find("<html") == std::string::npos && head.find("<!doctype") == std::string::npos;
}

Page extractJson(const std::string& body) {
  Page page;
  JsonValue root;
  std::string error;
  if (!parseJson(body, root, error)) return page;
  std::vector<std::string> seen;
  walkJson(root, "", page, seen);
  return page;
}

namespace {

bool isDirectFile(const std::string& path) {
  std::string lower = toLowerAscii(path);
  if (lower.size() > 4 && (lower.compare(lower.size() - 4, 4, ".pkg") == 0 ||
                           lower.compare(lower.size() - 5, 5, ".exfat") == 0)) {
    return true;
  }
  size_t dot = lower.find_last_of('.');
  if (dot == std::string::npos) return false;
  return lower.compare(dot, 4, ".pkg") == 0 || lower.compare(dot, 6, ".exfat") == 0;
}

bool looksLikeTagStart(const std::string& lower, size_t position, const char* tag) {
  if (position == std::string::npos || position >= lower.size()) return false;
  size_t length = std::strlen(tag);
  if (lower.compare(position, length, tag) != 0) return false;
  size_t next = position + length;
  if (next >= lower.size()) return false;
  char c = lower[next];
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>' || c == '/';
}

}  // namespace

std::string stripTags(const std::string& value) {
  std::string out;
  bool inside = false;
  for (char c : value) {
    if (c == '<') {
      inside = true;
    } else if (c == '>') {
      inside = false;
    } else if (!inside) {
      out.push_back(c);
    }
  }
  return out;
}

std::string findAttribute(const std::string& tag, const std::string& name) {
  std::string lower = toLowerAscii(tag);
  std::string needle = name + "=";
  size_t pos = 0;
  while ((pos = lower.find(needle, pos)) != std::string::npos) {
    if (pos > 0) {
      char before = tag[pos - 1];
      if (std::isalnum(static_cast<unsigned char>(before)) || before == '-' || before == '_') {
        pos += needle.size();
        continue;
      }
    }
    size_t value = pos + needle.size();
    if (value >= tag.size()) return "";
    char quote = tag[value];
    if (quote == '"' || quote == '\'') {
      size_t end = tag.find(quote, value + 1);
      if (end == std::string::npos) return "";
      return tag.substr(value + 1, end - value - 1);
    }
    size_t end = value;
    while (end < tag.size() && !std::isspace(static_cast<unsigned char>(tag[end])) && tag[end] != '>') {
      ++end;
    }
    return tag.substr(value, end - value);
  }
  return "";
}

std::vector<RawAnchor> rawAnchors(const std::string& html) {
  std::vector<RawAnchor> out;
  const std::string lower = toLowerAscii(html);
  size_t pos = 0;

  while ((pos = lower.find("<a", pos)) != std::string::npos) {
    if (!looksLikeTagStart(lower, pos, "<a")) {
      pos += 2;
      continue;
    }
    size_t tagEnd = lower.find('>', pos);
    if (tagEnd == std::string::npos) break;

    std::string tag = html.substr(pos, tagEnd - pos + 1);
    RawAnchor anchor;
    anchor.href = trim(tag.empty() ? "" : findAttribute(tag, "href"));
    if (anchor.href.empty()) anchor.href = trim(findAttribute(tag, "data-href"));
    if (anchor.href.empty()) anchor.href = trim(findAttribute(tag, "data-url"));
    anchor.className = findAttribute(tag, "class");

    size_t close = lower.find("</a", tagEnd);
    size_t textEnd = close == std::string::npos ? std::min(tagEnd + 4000, html.size()) : close;
    anchor.text = collapseSpaces(decodeEntities(stripTags(html.substr(tagEnd + 1, textEnd - tagEnd - 1))));
    if (anchor.text.size() > 300) anchor.text.resize(300);

    if (!anchor.href.empty()) out.push_back(anchor);
    pos = tagEnd + 1;
  }
  return out;
}

Page extractPage(const std::string& html, const std::string& baseUrl) {
  return extractPage(html, baseUrl, 2);
}

Page extractPage(const std::string& html, const std::string& baseUrl, int encodeDepth) {
  if (looksLikeJson(html)) return extractJson(html);

  Page page;
  const std::string lower = toLowerAscii(html);
  std::vector<std::string> seen;

  auto addLink = [&](const std::string& rawHref, const std::string& rawText) {
    std::string href = collapseSpaces(htmlUnescapeForUrl(trim(rawHref)));
    if (href.empty() || href[0] == '#') return;
    for (const char* scheme : {"javascript:", "mailto:", "tel:", "data:", "magnet:"}) {
      if (startsWithI(href, scheme)) return;
    }
    std::string absolute = startsWithI(href, "http") ? href : resolveUrl(baseUrl, href);
    if (!startsWithI(absolute, "http")) return;
    if (std::find(seen.begin(), seen.end(), absolute) != seen.end()) return;
    seen.push_back(absolute);
    page.links.push_back({absolute, rawText});
    if (page.links.size() >= 5000) return;
  };

  for (const RawAnchor& anchor : rawAnchors(html)) addLink(anchor.href, anchor.text);

  size_t titleStart = lower.find("<title");
  if (!looksLikeTagStart(lower, titleStart, "<title")) titleStart = std::string::npos;
  if (titleStart != std::string::npos) {
    size_t gt = html.find('>', titleStart);
    size_t end = lower.find("</title", gt);
    if (gt != std::string::npos && end != std::string::npos) {
      page.title = collapseSpaces(decodeEntities(html.substr(gt + 1, end - gt - 1)));
    }
  }

  if (page.title.empty()) {
    for (const char* marker : {"property=\"og:title\"", "name=\"og:title\""}) {
      size_t found = lower.find(marker);
      if (found == std::string::npos) continue;
      size_t windowStart = found > 160 ? found - 160 : 0;
      std::string window = html.substr(windowStart, std::min<size_t>(320, html.size() - windowStart));
      std::string content = findAttribute(window, "content");
      if (!content.empty()) {
        page.title = collapseSpaces(decodeEntities(content));
        break;
      }
    }
  }

  if (page.title.empty()) {
    size_t h1 = lower.find("<h1");
    if (h1 != std::string::npos) {
      size_t gt = html.find('>', h1);
      size_t end = lower.find("</h1", gt);
      if (gt != std::string::npos && end != std::string::npos) {
        page.title = collapseSpaces(decodeEntities(html.substr(gt + 1, end - gt - 1)));
      }
    }
  }

  size_t pos = 0;
  while ((pos = lower.find("http", pos)) != std::string::npos && page.links.size() < 5000) {
    if (pos > 0) {
      char before = html[pos - 1];
      if (std::isalnum(static_cast<unsigned char>(before)) || before == '/' || before == '"' ||
          before == '\'' || before == '=') {
        pos += 4;
        continue;
      }
    }
    size_t end = pos;
    while (end < html.size()) {
      char c = html[end];
      if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\'' || c == '<' ||
          c == '>' || c == '{' || c == '}' || c == '|' || c == '^' || c == '`') {
        break;
      }
      ++end;
    }
    std::string candidate = htmlUnescapeForUrl(html.substr(pos, end - pos));
    while (!candidate.empty() && (candidate.back() == '.' || candidate.back() == ',' ||
                                  candidate.back() == ')' || candidate.back() == ';')) {
      candidate.pop_back();
    }
    UrlParts parts = crackUrl(candidate);
    if (parts.valid && isDirectFile(parts.path)) addLink(candidate, "");
    pos = end > pos ? end : pos + 4;
  }

  if (encodeDepth > 0) {
    // wordpress link-hider plugins ship the real anchors base64'd in an
    // attribute and swap them in with javascript
    static const char* payloadAttrs[] = {"data-payload", "data-encode", "data-enc",
                                         "data-encrypted", "data-content-b64"};
    const std::string lowerCopy = toLowerAscii(html);
    for (const char* attr : payloadAttrs) {
      const std::string needle = std::string(attr) + "=\"";
      size_t search = 0;
      for (int guard = 0; guard < 64; ++guard) {
        size_t found = lowerCopy.find(needle, search);
        if (found == std::string::npos) break;
        size_t valueStart = found + needle.size();
        size_t valueEnd = html.find('"', valueStart);
        if (valueEnd == std::string::npos) break;
        std::string raw = html.substr(valueStart, valueEnd - valueStart);
        search = valueEnd + 1;
        if (!looksBase64(raw)) continue;
        std::string decoded = base64DecodeLoose(raw);
        if (decoded.find("http") == std::string::npos) continue;
        Page inner = extractPage(decoded, baseUrl, encodeDepth - 1);
        if (page.title.empty() && !inner.title.empty()) page.title = inner.title;
        for (const PageLink& link : inner.links) addLink(link.href, link.text);
      }
    }
  }

  return page;
}

std::string mirrorLink(const std::string& html, const std::string& domain) {
  const std::string suffix = "." + toLowerAscii(domain);
  for (const RawAnchor& anchor : rawAnchors(html)) {
    std::string href = anchor.href;
    if (href.rfind("//", 0) == 0) href = "http:" + href;
    UrlParts parts = crackUrl(href);
    if (!parts.valid) continue;
    std::string host = toLowerAscii(parts.host);
    if (host.size() < suffix.size() || host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0) {
      continue;
    }

    // the mirror label is a c or d followed only by digits and separators
    size_t dot = host.find('.');
    std::string label = dot == std::string::npos ? host : host.substr(0, dot);
    if (label.size() < 2 || (label[0] != 'c' && label[0] != 'd')) continue;
    bool digit = false;
    bool shape = true;
    for (size_t i = 1; i < label.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(label[i]);
      if (std::isdigit(c) != 0) {
        digit = true;
      } else if (c != '-') {
        shape = false;
        break;
      }
    }
    if (!shape || !digit) continue;
    if (parts.path.empty() || parts.path == "/") continue;
    return href;
  }
  return std::string();
}

std::string classText(const std::string& html, const std::string& className) {
  const std::string lower = toLowerAscii(html);
  size_t pos = 0;
  while ((pos = lower.find(className, pos)) != std::string::npos) {
    size_t tagStart = html.rfind('<', pos);
    size_t tagEnd = html.find('>', pos);
    if (tagStart == std::string::npos || tagEnd == std::string::npos) break;
    // the class has to be an attribute of this tag, not of one nested around it
    if (tagStart >= pos) {
      pos = tagEnd + 1;
      continue;
    }
    size_t close = html.find('<', tagEnd + 1);
    if (close != std::string::npos) {
      std::string text =
          collapseSpaces(trim(stripTags(html.substr(tagEnd + 1, close - tagEnd - 1))));
      if (text.size() >= 5) return text;
    }
    pos = tagEnd + 1;
  }
  return std::string();
}
