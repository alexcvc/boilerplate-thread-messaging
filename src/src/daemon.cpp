#include "daemon.hpp"

#include <sys/types.h>
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
app::daemon::daemon() {
  signal(kExitSignal, daemon::signalHandler);
  signal(kTerminateSignal, daemon::signalHandler);
  signal(kReloadSignal, daemon::signalHandler);
  signal(kUserSignal1, daemon::signalHandler);
  signal(kUserSignal2, daemon::signalHandler);
}

/**
 * @brief Handles the interrupt signals received by the daemon.
 *
 * This function is responsible for handling interrupt signals received by the daemon.
 * It updates the state of the daemon based on the received signal.
 *
 * @param signal The signal number received.
 */
void app::daemon::signalHandler(int signal) {
  std::cout << "Interrupt signal number [" << signal << "] received." << std::endl;

  switch (signal) {
    case kExitSignal:
    case kTerminateSignal: {
      daemon::instance().m_State = State::Stop;
      break;
    }
    case kReloadSignal: {
      daemon::instance().m_State = State::Reload;
      break;
    }
    case kUserSignal1: {
      daemon::instance().m_State = State::User1;
      break;
    }
    case kUserSignal2: {
      daemon::instance().m_State = State::User2;
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
bool app::daemon::writePidToFile(const std::string& pidFileName) {
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
bool app::daemon::makeDaemon(const std::string& pidFileName) {
  if (!m_IsInitialized) {
    m_IsInitialized = true;
    m_PidFileName = pidFileName;
    // Equivalent to daemon(0, 1): fork, setsid, chdir("/"); keep stdio open.
    pid_t pid = fork();
    if (pid < 0) {
      std::cerr << "Failed demonizing (fork): " << strerror(errno) << std::endl;
    } else if (pid > 0) {
      // Parent exits; child continues as daemon.
      _exit(EXIT_SUCCESS);
    } else if (setsid() < 0) {
      std::cerr << "Failed demonizing (setsid): " << strerror(errno) << std::endl;
    } else if (chdir("/") < 0) {
      std::cerr << "Failed demonizing (chdir): " << strerror(errno) << std::endl;
    } else if (writePidToFile(pidFileName)) {
      m_Pid = getpid();
      return true;
    }
  }
  return false;
}
