#pragma once

#include <mutex>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>

namespace utils {

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
    std::cout << "[MEMORY] " << std::left << std::setw(25) << label 
              << " | VmSize: " << std::setw(8) << vmSize << " kB"
              << " | VmRSS: " << std::setw(8) << vmRSS << " kB" << std::endl;
}

} // namespace utils
