#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>

#include "Quality.hpp"
#include "slaves.h"

enum class ProcessImageChangeReason : uint8_t {
  INITIALIZATION,
  DATA_CHANGE,
  QUALITY_CHANGE,
  NEW_EVENTS,
};

inline ProcessImageChangeReason operator|(ProcessImageChangeReason a, ProcessImageChangeReason b) {
  return static_cast<ProcessImageChangeReason>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline ProcessImageChangeReason& operator|=(ProcessImageChangeReason& a, ProcessImageChangeReason b) {
  a = a | b;
  return a;
}

inline bool hasFlag(ProcessImageChangeReason value, ProcessImageChangeReason flag) {
  return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0u;
}

// ---------------------------------------------------------------------------
// Event schema and helpers
// ---------------------------------------------------------------------------

namespace events {

// One event parsed from a single FIFO text line.
struct LogBookEvent {
  int eventId = 0;           // numeric event id (e.g. cas1_LOG_*)
  int arrayIndex1Based = 0;  // 1-based index into event array, 0 means unmapped
  std::string timestampStr;  // raw timestamp string as parsed
};

// Describes a family of related events and its index range.
struct EventDesc {
  const bool isComing{false};         // true for "coming" (set), false for "going" (clear)
  const int eventId{0};               // numeric event id (from slaves.h)
  const std::size_t totalIndices{0};  // number of indices available (1..totalIndices)
  const char* name{nullptr};          // event family name (string literal)
};

static constexpr std::size_t kMappingCount = 8U;  // number of entries in mapping table

// Default mapping table for CAS1 log events.
static const std::array<EventDesc, kMappingCount> kMappings{{
    {true, cas1_LOG_BEF_coming, 32, "BEF_coming"},
    {false, cas1_LOG_BEF_going, 32, "BEF_going"},
    {true, cas1_LOG_BE_coming, 32, "BE_coming"},
    {false, cas1_LOG_BE_going, 32, "BE_going"},
    {true, cas1_LOG_BAF_coming, 128, "BAF_coming"},
    {false, cas1_LOG_BAF_going, 128, "BAF_going"},
    {true, cas1_LOG_BA_coming, 8, "BA_coming"},
    {false, cas1_LOG_BA_going, 8, "BA_going"},
}};

/**
 * Number (K) of event BAF for transfer status
 * Represents the position in the event descriptor list
 * where the BAF status events ("BAF_coming", "BAF_going") are defined.
 */
constexpr int EVENT_NUMBER_BAF_STATUS = 3;
/**
 * Number (K) of event BAF for error handling
 * Represents the position in the event descriptor list
 * where the BAF status events ("BAF_coming", "BAF_going") are defined.
 */
constexpr int EVENT_NUMBER_BAF_ERROR = 4;

}  // namespace events

namespace beh {
/**
 * Array index for the BAF status.
 */
constexpr int BAF_STATUS_INDEX = 2;
/**
 * Array index for the BAF error.
 */
constexpr int BAF_ERROR_INDEX = 3;
}  // namespace beh

struct SlaveProcessImage {
  slave_pi slavePi{};                                ///< last copy slave process image
  iec61850::Quality quality;                         ///< quality of last data (defaults to Invalid)
  std::chrono::system_clock::time_point lastUpdate;  ///< last time stamp of upgrade

  /**
   * @brief Safe copy of slave process image
   * @param newPi The new process image to copy
   */
  void update(const slave_pi& newPi) {
    std::memcpy(&slavePi, &newPi, sizeof(slave_pi));
    lastUpdate = std::chrono::system_clock::now();
  }

  [[nodiscard]] const slave_pi& getSlavePi() const noexcept {
    return slavePi;
  }
  [[nodiscard]] iec61850::Quality getQuality() const noexcept {
    return quality;
  }
  [[nodiscard]] std::chrono::system_clock::time_point getLastUpdate() const noexcept {
    return lastUpdate;
  }
};

// ---------------------------------------------------------------------------
// Backward-compatible aliases (kept for transition)
// ---------------------------------------------------------------------------

using EventWithTimestamp [[deprecated("Use events::LogBookEvent instead")]] = events::LogBookEvent;
using EventDescription [[deprecated("Use events::EventDesc instead")]] = events::EventDesc;

inline constexpr std::size_t kEventDescriptionCount [[deprecated("Use events::kMappingCount instead")]] =
    events::kMappingCount;

inline constexpr auto& kEventDescriptions [[deprecated("Use events::kMappings instead")]] = events::kMappings;