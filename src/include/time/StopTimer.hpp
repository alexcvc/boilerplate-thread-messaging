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
#include <chrono>
#include <optional>

//----------------------------------------------------------------------------
// Public Prototypes
//----------------------------------------------------------------------------
/**
 * @brief This is an easy stop timer in C++11 class.
 * Timer allows setting timeout and to check elapsed of timer
 *
 */
class StopTimer {
 public:
  /** types */
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<StopTimer::Clock, std::chrono::milliseconds>;

  /**
   * @brief constructors
   */
  StopTimer() = default;
  StopTimer(std::chrono::milliseconds timeout) : m_timeout_duration(timeout) {}

  /**
   * @brief get timeout
   * @tparam TDurationUnit - to-duration unit type
   * @return duration in to-duration unit type
   */
  template <typename TDurationUnit = std::chrono::milliseconds>
  [[nodiscard]] TDurationUnit Timeout() const noexcept {
    return std::chrono::duration_cast<TDurationUnit>(m_timeout_duration);
  }

  /**
   * @brief set timeout in a duration unit
   * @tparam TDurationUnit - from-duration unit type
   * @param timeout
   */
  template <typename TDurationUnit = std::chrono::milliseconds>
  void SetTimeout(const TDurationUnit& timeout) noexcept {
    m_timeout_duration = std::chrono::duration_cast<TDurationUnit>(timeout);
  }

  /**
   * @brief is timer has been started (pushed)
   * @return true if it is running, otherwise - false
   */
  [[nodiscard]] bool IsRunning() const noexcept {
    return m_is_running;
  }

  /**
   * @brief stop running
   * @details reset running flag and reset start point in timer
   */
  void Reset() noexcept {
    m_is_running = false;
    m_start_point = {};
  }

  /**
   * @brief stop running
   * @details stop running only without a reset start point
   */
  void Stop() noexcept {
    m_is_running = false;
  }

  /**
   * @brief restart timer
   * @details restart the timer with setup start point to now time point
   * @return start time point
   */
  TimePoint Start() noexcept {
    m_is_running = true;
    m_start_point = CurrentTime();
    return m_start_point;
  }

  /**
   * @brief start timer with setup timeout
   * @details start timer with reset start point to now time point and new timeout
   * @tparam TDurationUnit - duration unit
   * @param new_timeout - new timeout in duration unit
   * @return start time point
   */
  template <typename TDurationUnit = std::chrono::milliseconds>
  TimePoint Start(const TDurationUnit& new_timeout) noexcept {
    SetTimeout<TDurationUnit>(new_timeout);
    m_is_running = true;
    m_start_point = CurrentTime();
    return m_start_point;
  }

  /**
   * @brief is a timeout interval elapsed
   * @brief check that a defined timeout interval was elapsed
   * @return optional boolean
   *        1. is not running - hasn't valued - std::nullopt
   *        2. running, but timeout is equal zero - true
   *        3. running with timeout > zero: true if timeout elapsed, otherwise - false
   */
  [[nodiscard]] std::optional<bool> IsElapsed() const noexcept {
    if (!m_is_running) {
      // the timer is not running
      return std::nullopt;
    }

    if (m_timeout_duration.count() == 0) {
      // is running with timeout 0
      return true;
    }

    return (ElapsedTime() > m_timeout_duration);
  }

  /**
   * @brief Checks if the timer is running and has elapsed time.
   * @return true if the timer was started and the time is elapsed.
   */
  [[nodiscard]] bool IsRunningAndElapsed() const noexcept {
    return IsElapsed().value_or(false);
  }

  /**
   * @brief elapsed timeout
   * @details elapsed timeout since start
   * @tparam TDurationUnit - duration unit
   * @return elapsed time since start point
   */
  template <typename TDurationUnit = std::chrono::milliseconds>
  [[nodiscard]] TDurationUnit ElapsedTime() const noexcept {
    if (!IsRunning()) {
      return TDurationUnit{};
    }

    const auto elapsed = CurrentTime() - m_start_point;
    return std::chrono::duration_cast<TDurationUnit>(elapsed);
  }

  /**
   * @brief left time
   * @details left time up to timeout point
   * @tparam TDurationUnit - duration unit
   * @return left time
   */
  template <typename TDurationUnit = std::chrono::milliseconds>
  [[nodiscard]] TDurationUnit LeftTime() const noexcept {
    if (!IsRunning()) {
      return TDurationUnit{};
    }

    const auto remaining = m_timeout_duration - ElapsedTime<>();
    return std::chrono::duration_cast<TDurationUnit>(remaining);
  }

  /**
   * @brief current time for a used clock from chrono
   * @details static function used many times in class
   * @return timepoint
   */
  [[nodiscard]] static inline TimePoint CurrentTime() noexcept {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(Clock::now());
  }

 private:
  TimePoint m_start_point{};                       ///< start time point
  std::chrono::milliseconds m_timeout_duration{};  ///< timeout
  bool m_is_running{false};                        ///< is running
};
