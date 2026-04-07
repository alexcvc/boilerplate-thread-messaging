#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#include "daemon.hpp"
#include "daemonConfig.hpp"
#include "threadManager.hpp"

using namespace std::chrono_literals;
//----------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Typedefs, enums, unions, variables
//----------------------------------------------------------------------------
enum class handleConsoleType {
  none,
  exit,
  addThread,
  stopThread,
};

struct TaskEvent {
  std::mutex event_mutex;
  std::condition_variable event_condition;
};

//----------------------------------------------------------------------------
// Prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Declarations
//----------------------------------------------------------------------------

/**
 * @brief Displays the version information of the program.
 * @param prog The name of the program.
 * @return None
 */
static void ShowVersion(const char* prog) {
  std::cout << prog << '\n';
}

/**
 * Displays the help message for the program.
 * @param programName The name of the program.
 * @param var The character representing the option with an error. Defaults to 0.
 */
static void DisplayHelp(const char* programName, const char* errorOption = "") {
  constexpr std::array<std::string_view, 3> kOptionHelpLines = {"  -D, --demonization       start in background",
                                                                "  -v, --version            version",
                                                                "  -h, --help               this message"};
  if (errorOption && *errorOption) {
    std::cerr << "Error in option: " << errorOption << "\n";
  }
  std::cout << "\nUsage: " << programName << " [OPTIONS]\n\n";
  for (const auto& option : kOptionHelpLines) {
    std::cout << option << '\n';
  }

  if (errorOption && *errorOption) {
    exit(EXIT_FAILURE);
  }
}

/**
 * @brief Handles the argument for a given option.
 * @param option The option for which the argument is provided.
 * @param argument The argument provided for the option.
 * @param argv0 The name of the program.
 * @return None
 */
void HandleOptionArgument(const char* option, const char* argument, const char* argv0) {
  if (!strlen(argument)) {
    std::cerr << "Missing " << option << " argument for option\n";
    DisplayHelp(argv0);
  }
}

/**
 * @brief Processes the command line options passed to the program.
 *
 * This function parses the command line options and sets the corresponding
 * variables based on the provided values. It uses getopt_long function to
 * handle both short and long options.
 *
 * @param argc The number of command line arguments.
 * @param argv The array of command line argument strings.
 * @param config
 */
static void ProcessCommandLine(int argc, char* argv[], app::daemonConfig& config) {
  const char* help_options = "h?vD";
  const option long_options[] = {
      {"help", no_argument, nullptr, 0},
      {"version", no_argument, nullptr, 'v'},
      {"background", no_argument, nullptr, 'D'},
      {nullptr, 0, nullptr, 0},
  };

  int option_index = 0;
  for (;;) {
    int current_option = getopt_long(argc, argv, help_options, long_options, &option_index);
    if (current_option == -1) {
      break;
    }

    switch (current_option) {
      case 0:
        DisplayHelp(argv[0]);
        break;

      case 'h':
      case '?':
        DisplayHelp(argv[0]);
        exit(EXIT_SUCCESS);

      case 'v':
        ShowVersion(argv[0]);
        exit(EXIT_SUCCESS);

      case 'D':
        config.isDaemon = true;
        break;

      default:
        std::cerr << "Unknown option: " << std::to_string(current_option) << std::endl;
        DisplayHelp(argv[0]);
    }
  }
}

/**
 * @brief handle console input
 */
handleConsoleType HandleConsole() {
  int key = getchar();
  if (key == EOF) {
    // Nothing to read (non-blocking stdin) or an error unrelated to input
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return handleConsoleType::none;
    }
    return handleConsoleType::none;
  }
  switch (key) {
    case 'q':
      return handleConsoleType::exit;
    case 'a':
      return handleConsoleType::addThread;
    case 's':
      return handleConsoleType::stopThread;
    case 'v':
      ShowVersion(program_invocation_short_name);
      break;
    case '?':
    case 'h':
      fprintf(stderr, "Test console:\n");
      fprintf(stderr, " q   -  quit from application.\n");
      fprintf(stderr, " a   -  add a new worker thread.\n");
      fprintf(stderr, " s   -  stop the last added worker thread.\n");
      fprintf(stderr, " v   -  version\n");
      fprintf(stderr, " h|? -  this information.\n");
      break;
    default:;
  }
  return handleConsoleType::none;
}

/**
 * @file main.c
 * @brief This is the main entry point for the application.
 */
int main(int argc, char** argv) {
  app::daemon& daemon = app::daemon::instance();  ///< The daemon is a singleton
  std::stop_source stopAppContext;             ///< stop token for the main loop
  app::daemonConfig daemonConfig;
  ThreadManager threadManager;

  int threadCounter = 0;

  //----------------------------------------------------------
  // set in daemon all handlers
  //----------------------------------------------------------
  // start
  daemon.setStartFunction([&]() {
    return threadManager.start();
  });
  // stop
  daemon.setCloseFunction([&]() {
    threadManager.stopAndWait();
    return true;
  });
  // reload
  daemon.setReloadFunction([&]() {
    return true;
  });
  daemon.setUser1Function([&]() {
    std::cout << "User1 function called.\n";
    return true;
  });
  daemon.setUser2Function([&]() {
    std::cout << "User2 function called.\n";
    return true;
  });

  //----------------------------------------------------------
  // parse parameters
  //----------------------------------------------------------
  ProcessCommandLine(argc, argv, daemonConfig);

  //----------------------------------------------------------
  // Make daemon
  //----------------------------------------------------------
  if (daemonConfig.isDaemon) {
    if (!daemon.makeDaemon(daemonConfig.pidFile)) {
      std::cerr << "Error starting the daemon." << std::endl;
      return EXIT_FAILURE;
    }
  }

  //----------------------------------------------------------
  // start application workers
  //----------------------------------------------------------
  if (!daemon.startAll()) {
    std::cerr << "Error starting the daemon." << std::endl;
    return EXIT_FAILURE;
  }

  //----------------------------------------------------------
  // Main loop
  if (!daemonConfig.isDaemon) {
    std::cout << "Press the h key to display the Console Menu...\n";
    // Set stdin to non-blocking mode so main loop never blocks on console input
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
      fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    } else {
      std::cerr << "WARN: Failed to get stdin flags for non-blocking input\n";
    }
  }

  // Daemon main loop
  while (daemon.isRunning()) {
    if (!daemonConfig.isDaemon) {
      auto result = HandleConsole();
      switch (result) {
        case handleConsoleType::exit:
          daemon.setState(app::daemon::State::Stop);
          break;
        case handleConsoleType::addThread:
          std::cout << "Adding thread " << ++threadCounter << "...\n";
          threadManager.addTask([id = threadCounter](messaging::Task& task, std::stop_token stopToken) {
            std::cout << "Task " << id << " started.\n";
            while (!stopToken.stop_requested()) {
              if (id % 5 == 0) {
                // Pow 5 mixed: alternating timeout and blocking wait
                static bool toggle = false;
                toggle = !toggle;
                auto msg = toggle ? task.messageQueue().wait_for(50ms) : task.messageQueue().try_pop();
                if (msg) {
                  std::cout << "Task " << id << " (pow 5 mixed) received a message.\n";
                } else {
                  std::cout << "Task " << id << " (pow 5 mixed) heartbeat.\n";
                }
              } else if (id % 2 == 0) {
                // Even tasks: wait-time 100 ms or event (new message)
                auto msg = task.messageQueue().wait_for(100ms);
                if (msg) {
                  std::cout << "Task " << id << " (even) received a message.\n";
                } else {
                  std::cout << "Task " << id << " (even) heartbeat.\n";
                }
              } else {
                // Odd tasks: event driven (stop token, message)
                // Use wait() which blocks until message arrives or queue is notified
                // But we need to check stopToken. MessageQueue::wait() doesn't currently
                // support stopToken, but we can rely on notify_all if needed or just use
                // wait_for with a larger interval to check stopToken periodically.
                // However, the requirement says event driven.
                auto msg = task.messageQueue().wait_for(1s); // still event-ish but checks stopToken
                if (msg) {
                  std::cout << "Task " << id << " (odd) received a message.\n";
                }
              }
            }
            std::cout << "Task " << id << " stopping.\n";
          });
          std::cout << "Total managed threads: " << threadManager.getThreadCount() << "\n";
          break;
        case handleConsoleType::stopThread:
          if (threadManager.getThreadCount() > 0) {
            std::cout << "Stopping last thread...\n";
            threadManager.stopLastThread();
            std::cout << "Total managed threads: " << threadManager.getThreadCount() << "\n";
          } else {
            std::cout << "No threads to stop.\n";
          }
          break;
        case handleConsoleType::none:
        default:
          break;
      }
    } else {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  // set token to stop all workers
  stopAppContext.request_stop();

  // Terminate appManager.getScheduler() after done
  std::cout << "Terminating the daemon process...\n";
  if (!daemon.closeAll()) {
    std::cerr << "ERROR: Failed to close all resources\n";
  }

  // join the thread application context
  std::cout << "The daemon process ended successfully\n";

  return EXIT_SUCCESS;
}
