#pragma once

#include <string>
#include <vector>

struct PageLink {
  std::string href;
  std::string text;
};

struct RawAnchor {
  std::string href;
  std::string className;
  std::string text;
};

struct Page {
  std::string title;
  std::vector<PageLink> links;
};

std::vector<RawAnchor> rawAnchors(const std::string& html);
// the anchor a host uses to hand over the file: a mirror of the shape
// c<n>.domain or d<n>.domain with a bare token path, never the site itself.
// Empty when the page carries no such link.
std::string mirrorLink(const std::string& html, const std::string& domain);
// the text inside the first element whose class list contains className -
// where hosts print the filename they are about to serve
std::string classText(const std::string& html, const std::string& className);
Page extractPage(const std::string& html, const std::string& baseUrl);
// encodeDepth controls how many rounds of base64 payload attributes are unwrapped
Page extractPage(const std::string& html, const std::string& baseUrl, int encodeDepth);
Page extractJson(const std::string& body);
bool looksLikeJson(const std::string& body);
std::string stripTags(const std::string& value);
std::string findAttribute(const std::string& tag, const std::string& name);
