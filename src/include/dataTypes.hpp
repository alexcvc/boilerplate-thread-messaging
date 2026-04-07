#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FileEvent {
  std::string fileName;
  uint64_t fileSize;
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
  std::string info;
};

struct ObserverCommand {
  enum class Type {
    StopObserving,   ///< Pause event generation for durationSec seconds
    StartObserving,  ///< Resume event generation after durationSec seconds
    StressMode,      ///< Switch to 100 ms interval for 20 s, then revert to 2 s normal
    SetNormalMode,   ///< Immediately exit stress; set interval to 1 s
  };
  Type type;
  int durationSec{0};
};
