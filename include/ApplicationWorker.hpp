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

//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------
#include <atomic>

#include "DaemonConfig.hpp"
#include "WorkerBase.hpp"
#include "goose_com_t.h"

namespace app {

/**
 * @brief A specialized worker class responsible for managing the application context.
 *
 * ApplicationWorker handles tasks related to the overall application runtime,
 * including configuration setup, task management, and application state handling.
 */
class ApplicationWorker : public WorkerBase {
  std::chrono::milliseconds m_waitIntervalMs = 2000ms;

  int m_isGooseEnabled{false};                      ///< Goose enabled flag
  int m_isToBeReconfigured{true};                   ///< Goose configuration flag
  RunTimeState m_runtimeState{RunTimeState::idle};  ///< Runtime state of the application
  app::DaemonConfig m_daemonConfig{};               ///< The configuration of the daemon
  GooseAppContext m_gooseAppContext{};              ///< GooseAppContext instance

  ApplicationWorker() {
    // Initialize Goose application context once
    InitGooseAppContext(&m_gooseAppContext);
  }
  ~ApplicationWorker() override = default;
  ApplicationWorker(const ApplicationWorker&) = delete;
  ApplicationWorker& operator=(const ApplicationWorker&) = delete;
  ApplicationWorker(ApplicationWorker&&) = delete;
  ApplicationWorker& operator=(ApplicationWorker&&) = delete;
  /**
   * @brief Executes the main logic for the application context worker.
   * @param stopToken A token used to signal when the worker should stop execution.
   */
  void run(const compat::stop_token& stopToken) override;

  /**
   * @brief Loads the latest configuration for the Goose application.
   */
  bool startupGooseService();
  void shutdownGooseService();

 public:
  static ApplicationWorker& instance() {
    static ApplicationWorker s_instance;
    return s_instance;
  }

  [[nodiscard]] bool isReadyToStart() noexcept override;

  /**
   * @brief Initializes and starts the execution of all configured tasks.
   */
  [[nodiscard]] bool startTasks();

  /**
   * @brief Stops all currently running tasks in the application.
   */
  void stopTasks();

  /**
   * @brief Provides access to the daemon's configuration.
   * @return A reference to the `app::DaemonConfig` object.
   */
  [[nodiscard]] app::DaemonConfig& daemonConfig() {
    return m_daemonConfig;
  }

  /**
   * @brief Represents the configuration manager for the application.
   */
  [[nodiscard]] const app::DaemonConfig& config() const {
    return m_daemonConfig;
  }

  /**
   * @brief Retrieves the wait interval duration in milliseconds.
   * @return The wait interval duration as a std::chrono::milliseconds object.
   */
  [[nodiscard]] std::chrono::milliseconds getWaitIntervalMs() const {
    return m_waitIntervalMs;
  }

  /**
   * @brief Sets the wait interval for the worker in milliseconds.
   * @param waitIntervalMs The desired wait interval specified as a `std::chrono::milliseconds` value.
   */
  void setWaitIntervalMs(const std::chrono::milliseconds& waitIntervalMs) {
    m_waitIntervalMs = waitIntervalMs;
  }

  /**
   * @brief Retrieves the current runtime state of the application.
   * @return The current runtime state of the application as a `RunTimeState` enum.
   */
  [[nodiscard]] RunTimeState getRuntimeState() const {
    return m_runtimeState;
  }
  /**
   * @brief Retrieves the configuration of the daemon process.
   * @return A copy of the `app::DaemonConfig` object containing the daemon's configuration.
   */
  [[nodiscard]] app::DaemonConfig getDaemonConfig() const {
    return m_daemonConfig;
  }

  /**
   * @brief Sets the configuration of the daemon process.
   * @param daemonConfig The new configuration for the daemon process.
   */
  void setDaemonConfig(const app::DaemonConfig& daemonConfig) {
    m_daemonConfig = daemonConfig;
  }

  /**
   * @brief Retrieves the Goose application context associated with the worker.
   * @return A constant reference to the `GooseAppContext` instance.
   */
  [[nodiscard]] const GooseAppContext& gooseAppContext() const {
    return m_gooseAppContext;
  }

  /**
   * @brief Retrieves the modifiable Goose application context associated with the worker.
   * @return A reference to the `GooseAppContext` instance.
   */
  [[nodiscard]] GooseAppContext& gooseAppContext() {
    return m_gooseAppContext;
  }
};

}  // namespace app
