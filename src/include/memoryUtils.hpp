#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace utils {

/// Returns current local time as "HH:MM:SS.µµµµµµ" (microsecond precision).
inline std::string currentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1'000'000;
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06lld", tm.tm_hour, tm.tm_min, tm.tm_sec,
                static_cast<long long>(us.count()));
  return buf;
}

inline std::mutex& getLogMutex() {
  static std::mutex m;
  return m;
}

inline void printMemoryUsage(const std::string& label) {
  std::ifstream statusFile("/proc/self/status");
  std::string line;
  long vmSize = -1, vmRSS = -1;
  while (std::getline(statusFile, line)) {
    if (line.compare(0, 7, "VmSize:") == 0) {
      sscanf(line.c_str(), "VmSize: %ld", &vmSize);
    } else if (line.compare(0, 6, "VmRSS:") == 0) {
      sscanf(line.c_str(), "VmRSS: %ld", &vmRSS);
    }
  }
  std::lock_guard<std::mutex> lock(getLogMutex());
  std::cout << "[MEMORY] " << std::left << std::setw(25) << label << " | VmSize: " << std::setw(8) << vmSize << " kB"
            << " | VmRSS: " << std::setw(8) << vmRSS << " kB" << std::endl;
}

}  // namespace utils
