#include "report.h"

#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "deadstore.h"
#include "hosts.h"
#include "http.h"
#include "json.h"
#include "util.h"

namespace {

// the logs channel webhook - reports land in the server log where the owner
// inspects them; the pi attaches allow/deny reactions and files them
const char* kWebhook =
    "https://discord.com/api/webhooks/1559172286479601734/"
    "F5Rpsl3Cfefg-lLIYJzoLpc2PlGi-qPr8-moiFLJkQ_I1EZq05A3x2z8hJ8XNictRJzc";
const char* kOwnerId = "413515992790990848";
const char* kReportList =
    "https://raw.githubusercontent.com/ElnTsk/PKG-LinkScrapper/main/Dead%20Links%20Database.txt";

std::string reportedPath() { return stateDirectory() + "\\reported.tsv"; }

// the public list, parsed once per process (re-read after five minutes): the
// report button checks it so the same dead link is never filed twice
std::mutex g_listMutex;
std::set<std::string> g_listedUrls;
int64_t g_listTakenMs = 0;

const std::set<std::string>& deadList(bool force) {
  std::lock_guard<std::mutex> lock(g_listMutex);
  if (!force && !g_listedUrls.empty() && wallMs() - g_listTakenMs < 5LL * 60 * 1000) {
    return g_listedUrls;
  }
  HttpResponse response = httpRequest(kReportList, "GET", std::vector<std::string>{}, true, false,
                                      8000);
  if (response.status == 200 && !response.body.empty()) {
    g_listedUrls.clear();
    for (const std::string& rawLine : splitLines(response.body)) {
      std::string line = trim(rawLine);
      if (line.empty() || line[0] == '#') continue;
      size_t tab = line.find_last_of('\t');
      std::string url = trim(tab == std::string::npos ? line : line.substr(tab + 1));
      if (url.find("http") == 0) g_listedUrls.insert(normalizeUrl(url));
    }
    g_listTakenMs = wallMs();
  }
  return g_listedUrls;
}

// the gui can reach this before any hunt ran; a request needs the session
void ensureHttp() {
  if (httpReady()) return;
  httpInit(
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/131.0.0.0 Safari/537.36",
      2, 8000, 1, 150);
}

std::string clip(std::string value, size_t width) {
  for (char& c : value) {
    if (c == '\r' || c == '\n') c = ' ';
  }
  value = collapseSpaces(value);
  if (value.size() > width) value = value.substr(0, width - 1) + "…";
  return value;
}

}  // namespace

std::string deadReportCode(const std::string& url) {
  unsigned int hash = 2166136261u;
  for (unsigned char c : normalizeUrl(url)) {
    hash ^= c;
    hash *= 16777619u;
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%06x", hash & 0xFFFFFFu);
  return buffer;
}

bool deadReportAlreadySent(const std::string& url) {
  std::ifstream stream(reportedPath());
  if (!stream) return false;
  std::string needle = normalizeUrl(url);
  std::string line;
  while (std::getline(stream, line)) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    if (normalizeUrl(trim(line.substr(0, tab))) == needle) return true;
  }
  return false;
}

void markDeadReportSent(const std::string& url) {
  std::string folder = stateDirectory();
  CreateDirectoryA(folder.c_str(), nullptr);
  std::ofstream stream(reportedPath(), std::ios::binary | std::ios::app);
  if (!stream) return;
  stream << normalizeUrl(url) << '\t' << wallMs() << "\r\n";
}

bool sendDeadReport(const DeadReport& report, std::string& error) {
  ensureHttp();
  const std::string code = deadReportCode(report.url);

  auto addField = [](std::string& out, const std::string& name, const std::string& value) {
    if (value.empty()) return;
    if (!out.empty()) out += ",";
    out += "{\"name\":" + jsonEscape(clip(name, 256)) + ",\"value\":" +
           jsonEscape(clip(value, 1000)) + ",\"inline\":true}";
  };
  std::string fields;
  addField(fields, "Game", report.game);
  addField(fields, "Host", report.host);
  addField(fields, "Source", report.source);
  addField(fields, "Row state", report.state);
  addField(fields, "Size", report.size);
  addField(fields, "URL", report.url);

  std::string body = "{";
  body += "\"username\":\"PKG-LinkScrapper\",";
  body += "\"content\":\"<@" + std::string(kOwnerId) + "> dead link report #" + code + "\",";
  body += "\"allowed_mentions\":{\"parse\":[],\"users\":[\"" + std::string(kOwnerId) + "\"]},";
  body += "\"embeds\":[{";
  body += "\"title\":\"Dead link report #" + code + "\",";
  body += "\"color\":15158332,";
  body += "\"fields\":[" + fields + "],";
  body += "\"footer\":{\"text\":" +
          jsonEscape("PKG-LinkScrapper" + (report.title.empty() ? "" : " " + clip(report.title, 120)) +
                     " · made by </ElonTusk>") + "}";
  body += "}]}";

  logDebug("report body " + body);
  HttpResponse response = httpPostJson(kWebhook, body);
  if (response.status < 200 || response.status >= 300) {
    logDebug("report reply " + response.body);
    error = "webhook answered " + std::to_string(response.status) +
            (response.error.empty() ? "" : " (" + response.error + ")");
    return false;
  }
  markDeadReportSent(report.url);
  error.clear();
  return true;
}

bool deadReportListed(const std::string& url) {
  ensureHttp();
  const std::set<std::string>& listed = deadList(false);
  return listed.count(normalizeUrl(url)) > 0;
}

void applyDeadVerdicts() {
  ensureHttp();
  g_listTakenMs = 0;
  const std::set<std::string>& listed = deadList(true);
  if (listed.empty()) return;

  // every line in the list is a report the owner approved - confirmed dead,
  // so it is remembered dead here too
  for (const std::string& url : listed) {
    std::string host = registrable(bareHost(url));
    std::string error;
    appendDeadStore(url, "reported", host, "", error);
  }
}
