#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string finalUrl;
  std::string error;
  std::map<std::string, std::string> headers;

  bool ok() const { return status >= 200 && status < 400; }
  std::string header(const std::string& lowerName) const;
};

void httpInit(const std::string& userAgent, int concurrency, int timeoutMs, int retries, int minHostDelayMs);
// true once httpInit has run - callers outside the crawl check before a request
bool httpReady();
void httpShutdown();
void httpCancelAll();

HttpResponse httpRequest(const std::string& url,
                         const std::string& verb,
                         const std::vector<std::string>& extraHeaders = {},
                         bool followRedirects = true,
                         bool useCache = true,
                         int timeoutOverrideMs = 0,
                         // a form body turns the verb into a real POST; never cached
                         const std::string& body = std::string(),
                         // 0 keeps the page-sized cap; catalogs ask for more
                         size_t maxBodyBytes = 0);

HttpResponse httpGet(const std::string& url, bool useCache = true);
// a json catalog is megabytes, not a page: bigger cap, longer fuse, cached
HttpResponse httpGetCatalog(const std::string& url);
HttpResponse httpHeadWithRange(const std::string& url);
HttpResponse httpGetRangeNoFollow(const std::string& url);
// the form post a share page needs before it will name the file. never cached:
// the answer is a one-time link
HttpResponse httpPostForm(const std::string& url, const std::string& form,
                          const std::string& referer = std::string());
// a json body, for webhook endpoints
HttpResponse httpPostJson(const std::string& url, const std::string& json);
