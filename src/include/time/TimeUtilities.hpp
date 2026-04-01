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

#pragma once

//-----------------------------------------------------------------------------
// includes
//-----------------------------------------------------------------------------
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>

// for BinTime conversion helpers/types
#include "timeUtils.h"

//----------------------------------------------------------------------------
// Public Prototypes
//----------------------------------------------------------------------------

namespace time_utils {

/**
 * @brief Convert a time point to seconds and fractional seconds.
 *
 * This function converts a given time point to seconds and fractional seconds.
 * The time point can be of any duration type. The returned values are represented as a tuple
 * containing two doubles - the seconds and the fractional part of seconds.
 *
 * @tparam Double The type of the doubles to represent seconds and fractional seconds.
 * @tparam TimePoint The type of the time point to convert.
 * @param timePoint The time point to convert to seconds and fractional seconds.
 * @return std::tuple<Double, Double> A tuple containing the seconds and fractional seconds.
 */
template <typename Double, typename TimePoint>
inline std::tuple<Double, Double> TimePntToSecondsAndFractional(const TimePoint& timePoint) {
  // Convert generic time_point to seconds as floating-point
  Double seconds = Double(timePoint.time_since_epoch().count()) * TimePoint::period::num / TimePoint::period::den;
  Double fractionalSeconds = std::modf(seconds, &seconds);
  return std::tuple<Double, Double>(seconds, fractionalSeconds);
}

/**
 * @brief Formats the given time into a specified date and time format.
 *
 * This function takes a `std::ostringstream` object as the output stream,
 * a pointer to a `std::tm` object representing the time,
 * a `std::string` specifying the date format,
 * a `double` representing the fractional seconds,
 * and a `std::size_t` specifying the precision of the fractional seconds.
 *
 * The function formats the time in the given date format using `std::put_time`,
 * and appends the formatted date to the output stream.
 * It then appends the fractional seconds with the specified precision to the output stream.
 *
 * @param oss The output `std::ostringstream` object.
 * @param tm A pointer to a `std::tm` object representing the time.
 * @param dateFormat The date format string.
 * @param fractionalSeconds The fractional seconds.
 * @param precision The precision of the fractional seconds.
 */
void FormatTimeToStream(std::ostringstream& oss, const std::tm* tm, const std::string& dateFormat,
                        double fractionalSeconds, std::size_t precision);

/**
 * @brief Convert a std::chrono::system_clock::time_point to IEC GOOSE BinTime.
 *
 * BinTime layout (from timeUtils.h):
 *   - t_day1984: days since 1984-01-01 (UTC)
 *   - t_msDay:   milliseconds within the day (0..86_399_999)
 *
 * This function interprets the provided time point as UTC (as per
 * std::chrono::system_clock convention on most platforms) and maps it to
 * the BinTime representation using the constants defined in timeUtils.h.
 */
BinTime TimePointToBinTime(const std::chrono::system_clock::time_point& tp) noexcept;

/**
 * @brief Converts a `std::chrono::system_clock::time_point` to a `UtcTime` structure.
 *
 * This function converts a time point from the system clock into a `UtcTime` representation.
 * It extracts the whole seconds and sub-second nanosecond remainder from the time point,
 * normalizes the nanosecond remainder to be within the range [0, 1e9), and adjusts the
 * seconds value accordingly when the nanosecond remainder is negative.
 * The fractional seconds are then converted to a 24-bit binary fraction with rounding.
 * Rare cases of rounding overflow are also handled by incrementing the seconds value
 * and resetting the fractional seconds.
 *
 * The output `UtcTime` structure contains:
 * - `t_soc`: The number of Unix seconds since the epoch in UTC.
 * - `t_fos`: The fractional seconds as an upper 24-bit binary fraction.
 * - `quality`: A quality field that can be set as needed (default is 0).
 *
 * @param tp A `std::chrono::system_clock::time_point` representing the time.
 * @return A `UtcTime` structure containing the corresponding UTC seconds and fractional seconds.
 */
UtcTime TimePointToUtcTime(const std::chrono::system_clock::time_point& tp) noexcept;

/**
 * Converts a given time point to a string representation with specified precision.
 *
 * Format: "%Y-%b-%d %H:%M:" followed by zero-padded seconds+fraction, e.g.:
 *   "2025-Jan-02 03:04:005.250"
 *
 * @tparam Double The desired floating point type for representing seconds.
 * @tparam Precision The desired precision for fractional seconds.
 * @tparam TimePoint The type of the time point to convert.
 *
 * @param timePoint The time point to convert to a string.
 *
 * @return A string representation of the given time point.
 *
 * @throws std::runtime_error if an error occurs during conversion.
 */
template <typename Double = double, std::size_t Precision = std::numeric_limits<Double>::digits10, typename TimePoint>
inline std::string TimePointToString(const TimePoint& timePoint) {
  static_assert(std::is_floating_point<Double>::value, "TimePointToString: Double must be a floating-point type");
  static_assert(Precision <= std::numeric_limits<Double>::digits10,
                "TimePointToString: Precision exceeds digits10 of Double");

  const std::string dateFormat = "%Y-%b-%d %H:%M:";

  const auto secondsAndFraction = TimePntToSecondsAndFractional<Double, TimePoint>(timePoint);
  const Double wholeSeconds = std::get<0>(secondsAndFraction);
  const Double fractionalSeconds = std::get<1>(secondsAndFraction);

  std::time_t tt = static_cast<std::time_t>(wholeSeconds);

  std::ostringstream oss;
  std::tm* tm = std::localtime(&tt);
  if (!tm) {
    throw std::runtime_error(std::strerror(errno));
  }

  FormatTimeToStream(oss, tm, dateFormat, fractionalSeconds, Precision);

  if (!oss) {
    throw std::runtime_error("time-point-to-string");
  }

  return oss.str();
}

/**
 * Converts a string representation of a date and time to a TimePoint object.
 *
 * Input format: "%Y-%b-%d %H:%M:%S[.fraction]"
 *
 * @param str The string to convert.
 * @tparam TimePoint The type of TimePoint to create.
 * @return The TimePoint object representing the specified date and time.
 * @throws std::invalid_argument If the input string is in an invalid format.
 */
template <typename TimePoint>
TimePoint TimePointFromString(const std::string& str) {
  using namespace std::chrono;

  const std::string dateTimeFormat = "%Y-%b-%d %H:%M:%S";

  std::istringstream inputStream(str);
  std::tm timeStruct{};
  double fractionalSeconds = 0.0;

  if (!(inputStream >> std::get_time(&timeStruct, dateTimeFormat.c_str()))) {
    throw std::invalid_argument("Invalid date-time format");
  }

  // Base time-point from whole seconds
  std::time_t tt = std::mktime(&timeStruct);
  if (tt == static_cast<std::time_t>(-1)) {
    throw std::invalid_argument("Invalid date-time value");
  }

  TimePoint timePoint{seconds(tt)};

  // No fractional part
  if (inputStream.eof()) {
    return timePoint;
  }

  // Expect '.' then one or more digits representing fractional seconds
  if (inputStream.peek() != '.') {
    throw std::invalid_argument("Invalid fractional seconds");
  }
  inputStream.get();  // consume '.'

  std::string frac_digits;
  while (inputStream.good()) {
    int c = inputStream.peek();
    if (c == EOF || !std::isdigit(c))
      break;
    frac_digits.push_back(static_cast<char>(inputStream.get()));
  }

  if (frac_digits.empty()) {
    throw std::invalid_argument("Invalid fractional seconds");
  }

  // Convert digits to a fractional seconds value
  double frac_val = 0.0;
  for (char ch : frac_digits) {
    frac_val = frac_val * 10.0 + static_cast<double>(ch - '0');
  }
  frac_val /= std::pow(10.0, static_cast<int>(frac_digits.size()));

  // Convert fractional seconds primarily to milliseconds (rounded)
  long long ms = static_cast<long long>(std::llround(frac_val * 1000.0));
  if (ms >= 1000)
    ms = 999;  // clamp just in case rounding overshoots

  return timePoint + std::chrono::duration_cast<typename TimePoint::duration>(std::chrono::milliseconds(ms));
}

//----------------------------------------------------------------------------
// Event timestamp utilities
//----------------------------------------------------------------------------

/**
 * @brief Convert a time point to an event timestamp string "YYYY-MM-DD HH:MM:SS.mmm".
 *
 * @tparam TimePoint A std::chrono::time_point type (usually std::chrono::system_clock::time_point).
 */
template <typename TimePoint>
inline std::string TimePointToEventTimestamp(const TimePoint& timePoint) noexcept {
  using namespace std::chrono;

  try {
    // Reinterpret given clock as system_clock based on its duration since epoch
    const auto tp_sys = time_point_cast<milliseconds>(system_clock::time_point(timePoint.time_since_epoch()));

    const auto tp_seconds = time_point_cast<seconds>(tp_sys);
    const auto fractional_millis = duration_cast<milliseconds>(tp_sys - tp_seconds).count();  // 0..999

    const std::time_t time_seconds = system_clock::to_time_t(tp_seconds);
    std::tm local_tm{};
    if (std::tm* tmp = std::localtime(&time_seconds)) {
      local_tm = *tmp;  // copy to avoid dangling pointer issues
    } else {
      return std::string{};
    }

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S.") << std::setw(3) << std::setfill('0') << fractional_millis;

    if (!oss) {
      return std::string{};
    }

    return oss.str();
  } catch (...) {
    // Best-effort, no exceptions allowed
    return std::string{};
  }
}

/**
 * @brief Convenience wrapper: current system_clock time to "YYYY-MM-DD HH:MM:SS.mmm".
 */
inline std::string ClockNowToEventTimestamp() noexcept {
  return TimePointToEventTimestamp(std::chrono::system_clock::now());
}

/**
 * @brief Parse an event timestamp string "YYYY-MM-DD HH:MM:SS[.mmm]" back into a
 *        std::chrono::system_clock::time_point.
 *
 * On any parse / conversion error this function returns a default-constructed
 * time_point (i.e. epoch) and does not throw.
 */
std::chrono::system_clock::time_point EventTimestampToTimePoint(const std::string& timestamp) noexcept;

}  // namespace time_utils