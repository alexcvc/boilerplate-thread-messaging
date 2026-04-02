#pragma once

//-----------------------------------------------------------------------------
// includes
//-----------------------------------------------------------------------------
#include <csignal>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

//----------------------------------------------------------------------------
// Declarations
//----------------------------------------------------------------------------
namespace app {
class Daemon {
 public:
  // Do not allow copy
  Daemon(Daemon const&) = delete;
  void operator=(Daemon const&) = delete;
  Daemon(Daemon&&) = delete;
  Daemon& operator=(Daemon&&) = delete;
  ~Daemon() = default;

  /**
   * @brief The state of the daemon.
   */
  enum class State : uint8_t { Start, Running, Reload, Stop, User1, User2 };

  /**
   * @brief Signals to exit the daemon.
   */
  static constexpr int kExitSignal = SIGINT;        ///< Signal to exit the daemon
  static constexpr int kTerminateSignal = SIGTERM;  ///< Signal to terminate the daemon
  static constexpr int kReloadSignal = SIGHUP;      ///< Signal to reload the daemon
  static constexpr int kUserSignal1 = SIGUSR1;      ///< Signal to execute user-defined action
  static constexpr int kUserSignal2 = SIGUSR2;      ///< Signal to execute user-defined action

  /**
   * @brief Gets the instance of the daemon.
   * @return The instance of the daemon.
   */
  static Daemon& instance() {
    static Daemon instance;
    return instance;
  }

  /**
   * @brief Starts the daemon.
   * @return True if the daemon is started, false otherwise.
   */
  [[nodiscard]] bool startAll() {
    m_State = State::Running;
    if (m_HandlerBeforeToStart) {
      return m_HandlerBeforeToStart();
    }
    return false;
  }

  /**
   * @brief Reloads the daemon.
   * @return True if the daemon is reloaded, false otherwise.
   */
  [[nodiscard]] bool reloadAll() {
    m_State = State::Reload;
    return true;
  }

  /**
   * @brief Closes the daemon.
   * @return True if the daemon is closed, false otherwise.
   */
  [[nodiscard]] bool closeAll() {
    m_State = State::Stop;
    if (m_HandlerBeforeToExit) {
      return m_HandlerBeforeToExit();
    }
    return false;
  }

  /**
   * @brief Sets the function to be called before the daemon starts.
   * @param func The function to be called.
   */
  void setStartFunction(std::function<bool()> func) {
    m_HandlerBeforeToStart = std::move(func);
  }

  /**
   * @brief Sets the function to be called when reloaded.
   * @param func The function to be called.
   */
  void setReloadFunction(std::function<bool()> func) {
    m_HandlerReload = std::move(func);
  }

  /**
   * @brief Sets the function to be called in case of USER1 signal.
   * @param func The function to be called.
   */
  void setUser1Function(std::function<bool()> func) {
    m_HandlerUser1 = std::move(func);
  }

  /**
   * @brief Sets the function to be called in case of USER2 signal.
   * @param func The function to be called.
   */
  void setUser2Function(std::function<bool()> func) {
    m_HandlerUser2 = std::move(func);
  }

  /**
   * @brief Sets the function to be called before exits.
   * @param func The function to be called.
   */
  void setCloseFunction(std::function<bool()> func) {
    m_HandlerBeforeToExit = std::move(func);
  }

  /**
   * @brief Checks if the daemon is running.
   * @return True if the daemon is running, false otherwise.
   */
  [[nodiscard]] bool isRunning() {
    if (m_State == State::Reload) {
      performReloadIfRequired();
    } else if (m_State == State::User1) {
      performUser1IfRequired();
    } else if (m_State == State::User2) {
      performUser2IfRequired();
    }
    return m_State == State::Running;
  }

  /**
   * @brief Gets the state of the daemon.
   * @return The state of the daemon.
   */
  [[nodiscard]] State getState() const {
    return m_State;
  }

  /**
   * @brief Sets the state of the daemon.
   * @param state The new state.
   */
  void setState(State state) {
    m_State = state;
  }

  /**
   * @brief Makes a process as daemon in background.
   * @param pidFileName Name of the PID file.
   */
  bool makeDaemon(const std::string& pidFileName);

 private:
  Daemon();

  /**
   * @brief Performs the reload if required.
   */
  void performReloadIfRequired() {
    m_State = State::Running;
    if (m_HandlerReload) {
      if (m_HandlerReload() == false) {
        m_State = State::Stop;
      }
    }
  }

  /**
   * @brief Performs the user1 operation if required.
   */
  void performUser1IfRequired() {
    m_State = State::Running;
    if (m_HandlerUser1) {
      if (m_HandlerUser1() == false) {
        m_State = State::Stop;
      }
    }
  }

  /**
   * @brief Performs the user2 operation if required.
   */
  void performUser2IfRequired() {
    m_State = State::Running;
    if (m_HandlerUser2) {
      if (m_HandlerUser2() == false) {
        m_State = State::Stop;
      }
    }
  }

  /**
   * @brief Handles the signals.
   * @param signal The signal to be handled.
   */
  static void signalHandler(int signal);

  /**
   * @brief Writes the process ID to a file.
   * @param pidFileName The name of the PID file.
   * @return True if the process ID is written to the file, false otherwise.
   */
  static bool writePidToFile(const std::string& pidFileName);

  // Member variables
  State m_State{State::Start};                   ///< State of the daemon
  pid_t m_Pid{-1};                               ///< PID of the child process
  bool m_IsInitialized{false};                   ///< Initialization state
  std::string m_PidFileName;                     ///< Storage location for PID
  std::function<bool()> m_HandlerBeforeToStart;  ///< Function to be called before the daemon starts
  std::function<bool()> m_HandlerReload;         ///< Function to be called when the daemon is reloaded
  std::function<bool()> m_HandlerUser1;          ///< Function to be called by USER1 signal
  std::function<bool()> m_HandlerUser2;          ///< Function to be called by USER2 signal
  std::function<bool()> m_HandlerBeforeToExit;   ///< Function to be called before the daemon exits
};

}  // namespace app
