#include "merge.h"

#include <cstdio>
#include <fstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include "util.h"

#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

namespace {

constexpr size_t kChunk = 1024 * 1024;

std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

long long fileSize(const std::string& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) return -1;
  long long size = static_cast<long long>(stream.tellg());
  return size;
}

std::string hexDigest(const unsigned char* bytes, DWORD length) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(static_cast<size_t>(length) * 2);
  for (DWORD i = 0; i < length; ++i) {
    out.push_back(digits[bytes[i] >> 4]);
    out.push_back(digits[bytes[i] & 0x0F]);
  }
  return out;
}

struct BcryptState {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  ~BcryptState() {
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  }
};

}  // namespace

MergeOutcome mergePkgParts(const std::vector<std::string>& partFiles,
                           const std::string& outputPath) {
  MergeOutcome result;
  result.output = outputPath;
  result.shaFile = outputPath + ".sha256.txt";

  if (partFiles.empty()) {
    result.error = "no parts to merge";
    return result;
  }
  if (outputPath.empty()) {
    result.error = "no output path";
    return result;
  }

  for (const std::string& part : partFiles) {
    long long size = fileSize(part);
    if (size < 0) {
      result.error = "missing part: " + baseName(part);
      return result;
    }
    if (size == 0) {
      result.error = "empty part: " + baseName(part);
      return result;
    }
  }

  BcryptState crypto;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&crypto.algorithm, BCRYPT_SHA256_ALGORITHM,
                                                  nullptr, 0))) {
    result.error = "cannot open SHA-256 provider";
    return result;
  }
  if (!BCRYPT_SUCCESS(BCryptCreateHash(crypto.algorithm, &crypto.hash, nullptr, 0, nullptr, 0,
                                       0))) {
    result.error = "cannot create SHA-256 hash";
    return result;
  }

  const std::string temp = outputPath + ".merging";
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  if (!out) {
    result.error = "cannot write " + baseName(outputPath);
    return result;
  }

  std::vector<char> buffer(kChunk);
  long long total = 0;
  for (const std::string& part : partFiles) {
    std::ifstream in(part, std::ios::binary);
    if (!in) {
      out.close();
      DeleteFileA(temp.c_str());
      result.error = "cannot read " + baseName(part);
      return result;
    }
    while (in) {
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      std::streamsize got = in.gcount();
      if (got <= 0) break;
      out.write(buffer.data(), got);
      if (!out) {
        out.close();
        DeleteFileA(temp.c_str());
        result.error = "disk write failed while merging";
        return result;
      }
      if (!BCRYPT_SUCCESS(BCryptHashData(crypto.hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                         static_cast<ULONG>(got), 0))) {
        out.close();
        DeleteFileA(temp.c_str());
        result.error = "SHA-256 update failed";
        return result;
      }
      total += got;
    }
  }
  out.flush();
  bool writeOk = static_cast<bool>(out);
  out.close();
  if (!writeOk) {
    DeleteFileA(temp.c_str());
    result.error = "disk flush failed while merging";
    return result;
  }

  unsigned char digest[32];
  if (!BCRYPT_SUCCESS(BCryptFinishHash(crypto.hash, digest, sizeof(digest), 0))) {
    DeleteFileA(temp.c_str());
    result.error = "SHA-256 finish failed";
    return result;
  }
  result.sha256 = hexDigest(digest, sizeof(digest));
  result.bytes = total;

  if (!MoveFileExA(temp.c_str(), outputPath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileA(temp.c_str());
    result.error = "cannot rename the merged file into place";
    return result;
  }

  std::ofstream sha(result.shaFile, std::ios::binary | std::ios::trunc);
  if (!sha) {
    result.error = "merged, but cannot write the checksum file";
    return result;
  }
  sha << result.sha256 << "  " << baseName(outputPath) << "\n" << total << " bytes\n";
  sha.close();

  result.ok = true;
  return result;
}