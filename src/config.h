#pragma once

#include <string>
#include <vector>

struct Settings {
  int timeoutMs = 8000;
  int concurrency = 10;
  bool verify = true;
  double threshold = 0.72;
  int maxPerGame = 40;
  int maxFetchPerGame = 32;
  int minHostDelayMs = 150;
  bool keepPages = true;
  int depth = 1;
  int goodTtlDays = 7;
  std::string userAgent;
  // domains and source names with a standing record of serving bytes; a hit
  // from one is labelled recommended and sorted above the rest
  std::vector<std::string> trustedHosts;
  std::vector<std::string> trustedSources;
};

struct Source {
  std::string name;
  std::string type;
  std::string engine;
  std::vector<std::string> query;
  std::vector<std::string> urls;
  std::vector<std::string> searchUrl;
  std::vector<std::string> pagedUrl;
  std::string itemUrl;
  std::vector<std::string> instances;
  int pages = 1;
  int depth = 1;
  // catalog sources name the console their entries belong to; empty means the
  // titles and urls have to say it themselves
  std::string console;
  bool enabled = true;
  bool trusted = false;
  bool resolveRedirects = false;
  bool keepPages = true;
  std::string origin;
};

struct Config {
  Settings settings;
  std::vector<Source> sources;
  // sources switched off by "enabled": false or a "disable" list - kept so a
  // caller can show them and offer to bring them back
  std::vector<Source> parked;
  std::vector<std::string> files;
  std::vector<std::string> disabled;
};

bool loadConfig(Config& out, const std::string& exeDir, const std::string& configArg,
                std::string& error);
std::string currentExePath();
std::string exeDirectory();

// the shipped defaults, as the json text the loader accepts
std::string embeddedConfigText();
// every source with its urls and flags, in the shape loadConfig reads back
std::string renderConfig(const Config& config);
// where a user config lives and is picked up automatically
std::string userConfigDirectory();
std::string userConfigFile();
