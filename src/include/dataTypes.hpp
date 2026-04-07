#pragma once

#include <chrono>
#include <cstdint>
#include <string>

struct FileEvent {
  std::string fileName;
  uint64_t fileSize;
  std::chrono::steady_clock::time_point sendTime{};  ///< stamped by OI, read by RT for chain duration
};

struct AppEvent {
  std::string appName;
  std::string eventDescription;
  FileEvent originalFile;
};

struct ProcessedData {
  std::string dataId;
  int processingResult;
  AppEvent originalAppEvent;
};

struct TransformedData {
  std::string transformationType;
  double value;
  ProcessedData originalData;
};

struct FinalResult {
  bool success;
  std::string summary;
  TransformedData originalTransformedData;
};

struct MirrorEvent {
  int counter;
  std::string message;
};

struct DirectEvent {
  int counter;
};

struct ObserverCommand {
  enum class Type {
    StopObserving,   ///< Pause event generation for durationSec seconds
    StartObserving,  ///< Resume event generation after durationSec seconds
    StressMode,      ///< Switch to 100 ms interval for 20 s, then revert to 2 s normal
    SetNormalMode,   ///< Immediately exit stress; set an interval to 500 ms
  };
  Type type;
  std::chrono::seconds durationSec{0};
};
