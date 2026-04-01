/* SPDX-License-Identifier: MIT */
//
// Copyright (c) 2025 Alexander Sacharov <a.sacharov@gmx.de>
//               All rights reserved.
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "Daemon.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>

/**
 * @brief Constructor for the Daemon class.
 *
 * This constructor initializes the state of the daemon to 'start' and sets up signal handlers
 * for the 'ExitSignal', 'TerminateSignal', 'ReloadSignal', 'User1' and 'User2' signals.
 */
app::Daemon::Daemon() {
  signal(kExitSignal, Daemon::signalHandler);
  signal(kTerminateSignal, Daemon::signalHandler);
  signal(kReloadSignal, Daemon::signalHandler);
  signal(kUserSignal1, Daemon::signalHandler);
  signal(kUserSignal2, Daemon::signalHandler);
}

/**
 * @brief Handles the interrupt signals received by the daemon.
 *
 * This function is responsible for handling interrupt signals received by the daemon.
 * It updates the state of the daemon based on the received signal.
 *
 * @param signal The signal number received.
 */
void app::Daemon::signalHandler(int signal) {
  std::cout << "Interrupt signal number [" << signal << "] received." << std::endl;

  switch (signal) {
    case kExitSignal:
    case kTerminateSignal: {
      Daemon::instance().m_State = State::Stop;
      break;
    }
    case kReloadSignal: {
      Daemon::instance().m_State = State::Reload;
      break;
    }
    case kUserSignal1: {
      Daemon::instance().m_State = State::User1;
      break;
    }
    case kUserSignal2: {
      Daemon::instance().m_State = State::User2;
      break;
    }
    default: {
      /* Ignore unhandled signals */
      break;
    }
  }
}

/**
 * @brief Writes the process ID to a file.
 *
 * This method writes the process ID to a specified file, allowing the process to be identified later.
 *
 * @param pidFileName The name of the file to write the process ID to.
 * @return True if the process ID is written to the file successfully, false otherwise.
 */
bool app::Daemon::writePidToFile(const std::string& pidFileName) {
  if (!pidFileName.empty()) {
    std::fstream fileRbackStream(pidFileName, std::ios::out);
    if (!fileRbackStream.is_open()) {
      std::cerr << "Failed to open " << pidFileName << std::endl;
      return false;
    }
    fileRbackStream << std::to_string(getpid());
    fileRbackStream.close();
  }
  return true;
}

/**
 * @brief Starts the daemon.
 *
 * This function starts the daemon by calling the handler function registered with the 'm_HandlerBeforeToStart' member.
 * If the handler function returns 'true', the daemon is started by calling the POSIX 'daemon()' function.
 * @attention If the handler returns erroneous, the calling application has to terminate!
 *
 * @param pidFileName The name of the file to write the process ID to.
 * @return 'true' if the daemon is started successfully, 'false' otherwise.
 */
bool app::Daemon::makeDaemon(const std::string& pidFileName) {
  if (!m_IsInitialized) {
    m_IsInitialized = true;
    m_PidFileName = pidFileName;
    if (daemon(0, 1) != 0) {
      std::cerr << "Failed demonizing: " << strerror(errno) << std::endl;
    } else if (writePidToFile(pidFileName)) {
      m_Pid = getpid();
      return true;
    }
  }
  return false;
}
