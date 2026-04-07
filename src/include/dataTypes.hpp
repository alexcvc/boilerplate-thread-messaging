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
