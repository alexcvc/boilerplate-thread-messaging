#pragma once

#include <cstdint>
#include <string>

// clang-format off
#define PACKAGE                 "thread_msg"
#define PACKAGE_SYSLOG          PACKAGE
#define PACKAGE_PID             "/var/run/" PACKAGE ".pid"
// clang-format on

namespace app {
/**
 * @struct DaemonConfig
 */
struct DaemonConfig {
  std::string pidFile{PACKAGE_PID};  ///< The path of the PID file
  bool isDaemon{false};              ///< Whether the process should run as a daemon
  std::string pathConfigFile{};      ///< The path of the configuration file
};
}  // namespace app