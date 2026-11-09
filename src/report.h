#pragma once

#include <string>

struct DeadReport {
  std::string game;
  std::string url;
  std::string host;
  std::string source;
  std::string state;
  std::string size;
  std::string title;
};

// six hex characters naming this report in the log and on discord; stable for
// the same url so a verdict can be matched to it later
std::string deadReportCode(const std::string& url);

// post the report to the logs webhook; the message tags the owner
bool sendDeadReport(const DeadReport& report, std::string& error);

bool deadReportAlreadySent(const std::string& url);
void markDeadReportSent(const std::string& url);

// fetch the public deadlinks.txt - every line there is a report the owner
// approved, and those links are remembered dead
void applyDeadVerdicts();
