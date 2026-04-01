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

#include "ApplicationWorker.hpp"

// Goose context helpers
#include <goose_com_t.h>

#include <iostream>

#include "GooseContext.hpp"
#include "GooseWorker.hpp"
#include "PipeEventWorker.hpp"
#include "ReadXmlConfig.hpp"
#include "ShmProcImageWorker.hpp"
#include "spdlog/spdlog.h"

/**
 * @brief Represents the name of a thread.
 */
const std::string kThreadName = "Worker Appl:";

/**
 * @brief Starts multiple tasks concurrently.
 * @return void
 */
bool app::ApplicationWorker::startTasks() {
  auto nErr{0u};

  spdlog::info("{} Start all Tasks", kThreadName);

  if (!GooseWorker::instance().start()) {
    nErr++;
  }
  if (!PipeEventWorker::instance().start()) {
    nErr++;
  }
  if (!ShmProcImageWorker::instance().start()) {
    nErr++;
  }
  return nErr == 0;
}

/**
 * @brief Stops all currently running tasks.
 * @warning Use this function with caution. Abruptly stopping tasks may lead
 *          to data loss or inconsistent system state if tasks are not designed
 *          to handle such interruptions gracefully.
 */
void app::ApplicationWorker::stopTasks() {
  spdlog::info("{} Stop all Tasks", kThreadName);

  ShmProcImageWorker::instance().stop();
  while (ShmProcImageWorker::instance().isRunning()) {
    std::this_thread::sleep_for(100ms);
  }
  PipeEventWorker::instance().stop();
  GooseWorker::instance().stop();
}

bool app::ApplicationWorker::startupGooseService() {
  constexpr std::size_t kConfigPathBufferSize = 256;
  char configPath[kConfigPathBufferSize] = "/usr/bin/eor3dapp/param/eor3d_goose_target.xml";

  const int retFilename = slave_get_newest_filename("eor3d_goose", ".xml", configPath, sizeof(configPath));
  if (retFilename < 0) {
    spdlog::error("{} slave_get_newest_filename failed", kThreadName);
    return false;
  }

  m_daemonConfig.pathConfigFile.assign(configPath);
  m_isToBeReconfigured = false;

  if (!PreConfigGoose(m_gooseAppContext)) {
    spdlog::warn("{} Failed to preconfigure GOOSE", kThreadName);
    // free all application objects
    ResetGooseAppContext(&m_gooseAppContext);
    return false;
  }

  m_gooseAppContext.user_data = &GooseWorker::instance().slaveProcessImage();
  m_gooseAppContext.user_len = sizeof(slave_pi);

  if (!StartGoose(m_gooseAppContext)) {
    spdlog::error("{} Failed to start GOOSE", kThreadName);
    // shutdown and free all application objects
    ResetGooseAppContext(&m_gooseAppContext);
    return false;
  }

  return true;
}

void app::ApplicationWorker::shutdownGooseService() {
  spdlog::info("{} stop GOOSE and release GOOSE resources", kThreadName);
  // Release Goose resources
  ShutdownGoose(m_gooseAppContext);
}

/**
 * @brief Executes the main logic for a given task or process.
 */
void app::ApplicationWorker::run(const compat::stop_token& stopToken) {
  auto notifyStopCondition = [&]() {
    m_waitCv.notify_all();
  };

  SLAVES slaves;
  // set wakeup in case of a requested stop
  [[maybe_unused]] compat::stop_callback stopCb(stopToken, notifyStopCondition);
  spdlog::info("{} started", kThreadName);

  // on startup, we have to set it once if algo is already running
#ifdef USE_DEBUG_X86_64
  m_isGooseEnabled = true;
  m_isToBeReconfigured = false;  // first time
#else
  m_isGooseEnabled = m_daemonConfig.forceStart;
  m_isToBeReconfigured = true;  // first time
#endif
  spdlog::info("{} Goose main is starting. Goose service {}", kThreadName, m_isGooseEnabled ? "enabled" : "disabled");

  while (!stopToken.stop_requested()) {
    if (auto result = update_config_for_goose_slave(&slaves); result == 0) {
      // configuration had been changed
#ifdef USE_DEBUG_X86_64
#warning "GOOSE is enabled in debug mode"
      m_isGooseEnabled = true;
#else
      m_isGooseEnabled = slaves.goose_slave.goose_enabled == 1;
#endif
      m_isToBeReconfigured = true;
      spdlog::info("{} There is a new GOOSE configuration. GOOSE is {}", kThreadName,
                   m_isGooseEnabled ? "enabled" : "disabled");
    }

    switch (m_runtimeState) {
      case RunTimeState::idle: {
        // synchronize slave
        // check if reload requested
        if (m_isGooseEnabled) {
          if (!startupGooseService()) {
            spdlog::error("{} Failed to load GOOSE configuration, staying in idle mode", kThreadName);
            break;
          }

          spdlog::info("{} GOOSE starting with configuration: {}", kThreadName, m_daemonConfig.pathConfigFile);

          if (!startTasks()) {
            spdlog::error("{} Failed to start tasks, staying in idle mode", kThreadName);
            m_runtimeState = RunTimeState::stopping;
            continue;
          }

          spdlog::info("{} Goose enabled, starting operation mode", kThreadName);
          m_runtimeState = RunTimeState::operation;
          continue;
        } else {
          spdlog::trace("{} Goose disabled, .. idle mode", kThreadName);
        }
      } break;

      case RunTimeState::operation: {
        // check if GOOSE enabled
        if (m_isGooseEnabled) {
          if (m_isToBeReconfigured) {
            // read configuration
            spdlog::info("{} New GOOSE configuration detected, reloading tasks", kThreadName);
            m_runtimeState = RunTimeState::stopping;
            continue;
          }
        } else {
          // goose disabled, idle mode
          spdlog::info("{} Goose disabled, switching to idle mode", kThreadName);
          stopTasks();
          m_runtimeState = RunTimeState::stopping;
          continue;
        }
      } break;

      case RunTimeState::stopping: {
        bool isAnyThreadRunning = false;
        if (GooseWorker::instance().isRunning()) {
          spdlog::info("{} -- GOOSE Worker stopping..", kThreadName);
          isAnyThreadRunning = true;
        }
        if (PipeEventWorker::instance().isRunning()) {
          spdlog::info("{} -- Event Worker stopping..", kThreadName);
          isAnyThreadRunning = true;
        }
        if (ShmProcImageWorker::instance().isRunning()) {
          spdlog::info("{} -- SHM Worker stopping..", kThreadName);
          isAnyThreadRunning = true;
        }

        // shutdown GOOSE threads and context
        shutdownGooseService();

        if (!isAnyThreadRunning) {
          spdlog::info("{} All tasks stopped successfully", kThreadName);
          m_runtimeState = RunTimeState::idle;
          break;
        }
        spdlog::info("{} Waiting for all tasks to stop..", kThreadName);

      } break;

      default:
        break;
    }

    {
      std::unique_lock lock(m_waitMutex);
      m_waitCv.wait_for(lock, m_waitIntervalMs);
    }
  }

  spdlog::info("{} stopped", kThreadName);
}

bool app::ApplicationWorker::isReadyToStart() noexcept {
  if (m_waitIntervalMs == std::chrono::milliseconds::zero()) {
    spdlog::warn("{} wait interval is zero, set 2000 ms", kThreadName);
    m_waitIntervalMs = std::chrono::milliseconds(2000);
  }
  return true;
}