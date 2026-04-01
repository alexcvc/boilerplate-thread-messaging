#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include <ApplicationWorker.hpp>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#include "Daemon.hpp"
#include "DaemonConfig.hpp"
#include "ReadXmlConfig.hpp"
#include "logging.h"
#include "slaves.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#include "version.hpp"

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
  idle,
};

struct TaskEvent {
  std::mutex event_mutex;
  std::condition_variable event_condition;
};

LogConfigParam logConfigParams{};

/**
 * @brief The options for the program.
 */
static constexpr std::array<std::string_view, 3> kOptionHelpLines = {
    "  -D, --demonization       start in background",
    "  -v, --version            version",
    "  -h, --help               this message"};

/**
 *  @brief The help options for the program.
 */
static const char* help_options = "h?vD";
static const struct option long_options[] = {
    {"help", no_argument, nullptr, 0},
    {"version", no_argument, nullptr, 'v'},
    {"background", no_argument, nullptr, 'D'},
    {nullptr, 0, nullptr, 0},
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
  auto verString = version::goose_slave::getVersion(true);
  // info
  spdlog::set_pattern("%v");
  spdlog::info("{} v.{}", prog, verString);
  // to undo a custom pattern and restore normal formatting:
  spdlog::set_pattern("%+");
}

/**
 * Displays the help message for the program.
 * @param programName The name of the program.
 * @param var The character representing the option with an error. Defaults to 0.
 */
static void DisplayHelp(const char* programName, const char* errorOption = "") {
  spdlog::set_pattern("%v");
  if (errorOption && *errorOption) {
    std::cerr << "Error in option: " << errorOption << "\n";
  }
  spdlog::info("\nUsage: {} [OPTIONS]\n", programName);
  for (const auto& option : kOptionHelpLines) {
    spdlog::info("{}", option);
  }
  // to undo a custom pattern and restore normal formatting:
  spdlog::set_pattern("%+");

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
static void ProcessCommandLine(int argc, char* argv[], app::DaemonConfig& config) {
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
        config.hasTestConsole = false;
        break;

      default:
        std::cerr << "Unknown option: " << std::to_string(current_option) << std::endl;
        DisplayHelp(argv[0]);
    }
  }
}

/**
 * @brief Initialize logging based on spdlog
 */
static void InitLogging() {
  auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  // consoleSink->set_pattern("[%C-%m-%d %T,%e] [%^%l%$] %v");
  consoleSink->set_pattern("%+");
  spdlog::set_default_logger(std::make_shared<spdlog::logger>(program_invocation_short_name, consoleSink));
  spdlog::set_level(spdlog::level::info);
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
    case 'i':
      return handleConsoleType::idle;
    case 'v':
      // info
      ShowVersion(program_invocation_short_name);
      break;
    case '+':
      if (spdlog::get_level() == spdlog::level::err) {
        spdlog::set_level(spdlog::level::warn);
      } else if (spdlog::get_level() == spdlog::level::warn) {
        spdlog::set_level(spdlog::level::info);
      } else if (spdlog::get_level() == spdlog::level::info) {
        spdlog::set_level(spdlog::level::debug);
      } else if (spdlog::get_level() == spdlog::level::debug) {
        spdlog::set_level(spdlog::level::trace);
      }
      break;
    case '-':
      if (spdlog::get_level() == spdlog::level::trace) {
        spdlog::set_level(spdlog::level::debug);
      } else if (spdlog::get_level() == spdlog::level::debug) {
        spdlog::set_level(spdlog::level::info);
      } else if (spdlog::get_level() == spdlog::level::info) {
        spdlog::set_level(spdlog::level::warn);
      } else if (spdlog::get_level() == spdlog::level::warn) {
        spdlog::set_level(spdlog::level::err);
      }
      break;
    case '?':
    case 'h':
      fprintf(stderr, "GOOSE test console:\n");
      fprintf(stderr, " +   -  do logging more verbose\n");
      fprintf(stderr, " -   -  do logging less verbose\n");
      fprintf(stderr, " i   -  go to IDLE (stop GOOSE)\n");
      fprintf(stderr, " q   -  quit from application.\n");
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
  app::Daemon& daemon = app::Daemon::instance();  ///< The daemon is a singleton
  compat::stop_source stopAppContext;             ///< stop token for the main loop

  //----------------------------------------------------------
  // initialize default sink of spdlog
  //----------------------------------------------------------
  InitLogging();

  //----------------------------------------------------------
  // set in daemon all handlers
  //----------------------------------------------------------
  // start
  daemon.setStartFunction([&]() {
    return app::ApplicationWorker::instance().start();
  });
  // stop
  daemon.setCloseFunction([&]() {
    app::ApplicationWorker::instance().stop();
    return true;
  });
  // reload
  daemon.setReloadFunction([&]() {
    app::ApplicationWorker::instance().stop();
    std::this_thread::sleep_for(1s);
    return app::ApplicationWorker::instance().start();
  });
  daemon.setUser1Function([&]() {
    spdlog::info("User1 function called.");
    return true;
  });
  daemon.setUser2Function([&]() {
    spdlog::info("User2 function called.");
    return true;
  });

  //----------------------------------------------------------
  // parse parameters
  //----------------------------------------------------------
  ProcessCommandLine(argc, argv, app::ApplicationWorker::instance().daemonConfig());

  //----------------------------------------------------------
  // Make daemon
  //----------------------------------------------------------
  if (app::ApplicationWorker::instance().daemonConfig().isDaemon) {
    if (!daemon.makeDaemon(app::ApplicationWorker::instance().daemonConfig().pidFile)) {
      std::cerr << "Error starting the daemon." << std::endl;
      return EXIT_FAILURE;
    }
  }

  //----------------------------------------------------------
  // Prepare slave
  //----------------------------------------------------------
  if (slaves_init() != 0) {
    spdlog::error("slaves_init failed");
    return EXIT_FAILURE;
  }

  if (slave_mutex_init() != 0) {
    const int err = errno;
    spdlog::error("slave_mutex_init failed: {}", err);
    return EXIT_FAILURE;
  }

  if (init_goose_slave() != 0) {
    const int err = errno;
    spdlog::error("init_goose_slave failed: {}", err);
    return EXIT_FAILURE;
  }

  if (slave_rstream_open_goose() != 0) {
    const int err = errno;
    spdlog::error("slave_rstream_open_goose failed: {}", err);
    return EXIT_FAILURE;
  }
  spdlog::debug("Slave initialized successfully");
  //----------------------------------------------------------
  // start application workers
  //----------------------------------------------------------
  if (!daemon.startAll()) {
    std::cerr << "Error starting the daemon." << std::endl;
    return EXIT_FAILURE;
  }

  //----------------------------------------------------------
  // Main loop
  if (app::ApplicationWorker::instance().daemonConfig().hasTestConsole) {
    spdlog::info("Press the h key to display the Console Menu...");
    // Set stdin to non-blocking mode so main loop never blocks on console input
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
      fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    } else {
      spdlog::warn("Failed to get stdin flags for non-blocking input");
    }
  }

  // Daemon main loop
  while (daemon.isRunning()) {
    if (app::ApplicationWorker::instance().daemonConfig().hasTestConsole) {
      auto result = HandleConsole();
      switch (result) {
        case handleConsoleType::exit:
          daemon.setState(app::Daemon::State::Stop);
          break;
        case handleConsoleType::idle:
          daemon.setState(app::Daemon::State::Stop);
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
  spdlog::info("Terminating the daemon process...");
  if (!daemon.closeAll()) {
    spdlog::error("Failed to close all resources");
  }

  // join the thread application context
  app::ApplicationWorker::instance().stop();
  spdlog::info("The daemon process ended successfully");

  return EXIT_SUCCESS;
}