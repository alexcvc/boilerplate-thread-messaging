/* SPDX-License-Identifier: A-EBERLE */
/****************************************************************************\
**                   _        _____ _               _                      **
**                  / \      | ____| |__   ___ _ __| | ___                 **
**                 / _ \     |  _| | '_ \ / _ \ '__| |/ _ \                **
**                / ___ \ _  | |___| |_) |  __/ |  | |  __/                **
**               /_/   \_(_) |_____|_.__/ \___|_|  |_|\___|                **
**                                                                         **
*****************************************************************************
** Copyright (c) 2010 - 2025 A. Eberle GmbH & Co. KG. All rights reserved. **
\****************************************************************************/

//-----------------------------------------------------------------------------
// includes
//-----------------------------------------------------------------------------

// for BinTime conversion helpers/types
#include "TimeUtilities.hpp"

#include "spdlog/spdlog.h"

void time_utils::FormatTimeToStream(std::ostringstream& oss, const std::tm* tm, const std::string& dateFormat,
                                    double fractionalSeconds, std::size_t precision) {
  // Write the date/time prefix
  oss << std::put_time(tm, dateFormat.c_str());

  // Write seconds as 3 digits (zero-padded)
  oss << std::setw(3) << std::setfill('0') << tm->tm_sec;

  // Compute fractional field as an integer with the requested precision
  const double scale = std::pow(10.0, static_cast<int>(precision));
  long long frac = static_cast<long long>(std::llround(fractionalSeconds * scale));
  if (frac >= static_cast<long long>(scale)) {
    // Handle rare rounding overflow (e.g., 0.9995 at precision 3 -> 1000)
    frac = 0;
    // We do not carry into seconds here to avoid mutating the provided tm structure.
  }

  // Write '.' followed by zero-padded fractional digits with exact precision width
  oss << '.' << std::setw(static_cast<int>(precision)) << std::setfill('0') << frac;
}

BinTime time_utils::TimePointToBinTime(const std::chrono::system_clock::time_point& tp) noexcept {
  using namespace std::chrono;

  // Break down into whole seconds since Unix epoch and the sub-second remainder in ms
  const auto since_epoch_ms = duration_cast<milliseconds>(tp.time_since_epoch());
  const auto since_epoch_s = duration_cast<seconds>(since_epoch_ms);
  const auto rem_ms = (since_epoch_ms - duration_cast<milliseconds>(since_epoch_s)).count();  // 0..999 or negative

  // Seconds since 1970-01-01
  long long unix_sec = since_epoch_s.count();

  // Seconds since 1984-01-01 00:00:00 UTC
  long long sec_since_1984 = unix_sec - static_cast<long long>(JAN_1_1984);

  // Compute day index and second-of-day, making sure modulo is non-negative
  long long day1984 = 0;
  long long sec_of_day = 0;
  if (sec_since_1984 >= 0) {
    day1984 = sec_since_1984 / SECS_PER_DAY;
    sec_of_day = sec_since_1984 % SECS_PER_DAY;
  } else {
    // For negative values, adjust so that sec_of_day is positive
    // Example: -1 sec since 1984 => day -1, sec_of_day = 86399
    day1984 = (sec_since_1984 - (SECS_PER_DAY - 1)) / SECS_PER_DAY;  // floor division
    sec_of_day = sec_since_1984 - day1984 * SECS_PER_DAY;
  }

  long long ms_of_day = sec_of_day * 1000LL + rem_ms;
  if (ms_of_day < 0) {
    // Borrow one day if remainder was negative
    ms_of_day += static_cast<long long>(MS_PER_DAY);
    --day1984;
  } else if (ms_of_day >= static_cast<long long>(MS_PER_DAY)) {
    // Carry into next day in rare cases
    ms_of_day -= static_cast<long long>(MS_PER_DAY);
    ++day1984;
  }

  BinTime bt{};
  bt.t_day1984 = static_cast<long>(day1984);
  bt.t_msDay = static_cast<long>(ms_of_day);
  return bt;
}

UtcTime time_utils::TimePointToUtcTime(const std::chrono::system_clock::time_point& tp) noexcept {
  using namespace std::chrono;

  // Break into whole seconds and sub-second remainder
  const auto since_epoch = tp.time_since_epoch();
  const auto whole_sec = duration_cast<seconds>(since_epoch);
  const auto subsec_ns = duration_cast<nanoseconds>(since_epoch - whole_sec)
                             .count();  // can be 0..999,999,999 or negative on some impls if tp<epoch

  // Normalize remainder to [0, 1e9) and adjust whole seconds if needed
  long long sec = whole_sec.count();
  long long ns = subsec_ns;
  if (ns < 0) {
    ns += 1'000'000'000LL;
    --sec;
  }

  // Convert fractional seconds to 24-bit binary fraction with rounding
  // fraction24 = round((ns / 1e9) * 2^24)
  uint64_t frac24 =
      static_cast<uint64_t>(std::llround((static_cast<long double>(ns) * (1ull << 24)) / 1'000'000'000.0L));

  // Handle rare rounding overflow (e.g., ns ~= 1e9)
  if (frac24 >= (1ull << 24)) {
    frac24 = 0;
    ++sec;
  }

  UtcTime out{};
  out.t_soc = static_cast<uint32_t>(sec);          // Unix seconds (UTC)
  out.t_fos = static_cast<uint32_t>(frac24 << 8);  // upper 24 bits hold fraction, lower 8 bits = 0
  out.quality = 0;                                 // set as needed (e.g., UTC_LEAPSECONDKNOWN bit)
  return out;
}

std::chrono::system_clock::time_point time_utils::EventTimestampToTimePoint(const std::string& timestamp) noexcept {
  using namespace std::chrono;

  std::tm tm{};
  std::istringstream iss(timestamp);

  // Parse date and time part: "YYYY-MM-DD HH:MM:SS"
  iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  if (!iss) {
    spdlog::error("Invalid event timestamp: bad date/time: {}", timestamp);
    return {};
  }

  // Parse optional ".mmm" milliseconds
  long millis = 0;
  if (iss.peek() == '.') {
    char dot = 0;
    iss >> dot;  // consume '.'
    if (dot != '.') {
      spdlog::error(("Invalid event timestamp: missing '.' before milliseconds: {}"), timestamp);
      return {};
    }

    // read digits explicitly to detect non-digit cases
    std::string ms_digits;
    while (iss && std::isdigit(iss.peek())) {
      ms_digits.push_back(static_cast<char>(iss.get()));
    }
    if (ms_digits.empty()) {
      spdlog::error("Invalid event timestamp: milliseconds missing: {}", timestamp);
      return {};
    }
    if (ms_digits.size() > 3) {
      spdlog::error(("Invalid event timestamp: milliseconds out of range (>999): {}"), timestamp);
      return {};
    }
    // convert
    millis = 0;
    for (char ch : ms_digits)
      millis = millis * 10 + (ch - '0');
    if (millis < 0 || millis > 999) {
      spdlog::error("Invalid event timestamp: milliseconds out of range: {}", timestamp);
      return {};
    }
  }

  std::time_t tt = std::mktime(&tm);
  if (tt == static_cast<std::time_t>(-1)) {
    spdlog::error("Invalid event timestamp: mktime failed: {}", timestamp);
    return {};
  }

  auto tp_seconds = std::chrono::system_clock::from_time_t(tt);
  return tp_seconds + milliseconds(millis);
}
