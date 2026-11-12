#pragma once

#include <string>
#include <vector>

// A split PKG is raw byte pieces: concatenating them in index order rebuilds the
// whole .pkg exactly. This streams the pieces into one file while feeding the
// same bytes through SHA-256, so the merged file and its checksum cost a single
// pass over the data. After it lands, a sidecar "<output>.sha256.txt" carries the
// hash, the file name and the byte size.

struct MergeOutcome {
  bool ok = false;
  std::string output;    // the merged .pkg
  std::string shaFile;   // the sidecar checksum text
  std::string sha256;    // lowercase hex digest
  long long bytes = 0;   // total merged size
  std::string error;
};

// partFiles must already be in the correct order. outputPath is the destination
// .pkg (its parent folder must exist). Fails closed: a missing or empty piece,
// a write error or a hash failure leaves no half-merged file behind.
MergeOutcome mergePkgParts(const std::vector<std::string>& partFiles,
                           const std::string& outputPath);