#pragma once

#include <string>

// Some hosts will not give the file to a probe: the share url answers with a
// page, and the real link only appears after the site's own download form is
// posted. That is not a bypass - it is the two-step the host designed, done
// with the same requests a browser makes.
//
// The links come back one at a time and are spent when used, so this is called
// when she presses Download, never while a hunt is deciding states.

struct DirectLink {
  bool ok = false;
  std::string url;    // serves the bytes
  std::string name;   // the filename the host offers
  std::string note;   // why it did not work, in the host's own words
};

// True when this url's host has a resolver below.
bool resolvableHost(const std::string& url);

// Walk the share page to the link it hides. Empty note means "nothing known".
DirectLink resolveDirect(const std::string& url);
