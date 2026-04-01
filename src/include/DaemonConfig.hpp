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

/**
 * @file
 * @brief   Contains the CLI configuration for a daemon process.
 * @ingroup
 */

#pragma once

#include <cstdint>
#include <string>

// clang-format off
#define PACKAGE                 "goose-slave"
#define PACKAGE_SYSLOG          PACKAGE
#define PACKAGE_PID             "/var/run/" PACKAGE ".pid"
#define PACKAGE_PATH_PARAM      "/usr/bin/eor3dapp/param"
#define PREFIX PACKAGE          ": "
#define PACKAGE_XML_PREFIX      "eor3d_goose_target_"
// clang-format on

namespace app {
enum class OperationMode : uint8_t {
  oper = 0,  ///< 'oper' - operation mode (default)
  test = 1,  ///< 'test' - GOOSE loopback test
  sim = 2    ///< 'sim'  - simulation the change in algo
};

enum class RunTimeState : uint8_t {
  idle = 0,       ///< idle
  operation = 1,  ///< operation mode
  stopping = 2,   ///< do  all  before go to idle mode
};

/**
 * @struct DaemonConfig
 *
 * @brief The DaemonConfig struct represents the configuration for a daemon process.
 *
 * The DaemonConfig struct holds information such as the path of the PID file,
 * whether the process should run as a daemon, and whether there is a test console
 * running in the foreground.
 */
struct DaemonConfig {
  std::string pidFile{PACKAGE_PID};                    ///< The path of the PID file
  bool isDaemon{true};                                 ///< Whether the process should run as a daemon
  bool hasTestConsole{false};                          ///< Whether there is a test console running in the foreground
  bool forceStart{false};                              ///< no wait ALGO - start force
  std::string pathConfigFile{};                        ///< The path of the configuration file
  bool printConfig{false};                             ///< print configuration
  std::string adapterName{"eth0"};                     ///< adapter that used for GOOSE
  OperationMode daemonStartMode{OperationMode::oper};  ///< operation mode
  uint32_t simIntervalMSec{4000};                      ///< simulation the change in algo data.
};
}  // namespace app