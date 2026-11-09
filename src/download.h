#pragma once

#include <string>
#include <vector>

// A small JDownloader-style queue: stream the direct links (or every part of a
// set) into a folder the user picked, with resume, live speed and per-file
// state. It runs on its own WinHTTP session so hunting is never starved.

struct DownloadItem {
  std::string id;
  std::string url;
  std::string name;
  std::string path;      // final file
  std::string partPath;  // written here until it is complete
  long long totalBytes = 0;
  long long doneBytes = 0;
  double speedMBs = 0;
  std::string state = "queued";  // queued | running | paused | done | failed | cancelled
  std::string error;
  long long startedMs = 0;
};

// Queues one url per entry. Existing files are skipped, not overwritten.
// `names`, when given, is the file name to save each url under - the host's own
// name beats the token in its url. Returns the ids in the order given.
std::vector<std::string> downloadsAdd(const std::vector<std::string>& urls,
                                      const std::string& folder,
                                      const std::vector<std::string>& names = {});

void downloadsStart(int workers);
void downloadsPause();
void downloadsResume();
void downloadsCancel(const std::string& id);
void downloadsRemoveFinished();
void downloadsAbort();

std::vector<DownloadItem> downloadsSnapshot();
double downloadsAggregateMBs();
size_t downloadsActive();
bool downloadsBusy();

// a name for the folder picker's remembered location
std::string downloadsFolderDefault();
void downloadsFolderRemember(const std::string& folder);
