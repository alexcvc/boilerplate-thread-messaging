#include "PipeEventWorker.hpp"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <regex>
#include <stdexcept>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "DataTypes.hpp"
#include "GooseWorker.hpp"
#include "slaves.h"
#include "spdlog/spdlog.h"

namespace events {
struct LogBookEvent;
}

extern FILE* goose_stream;
extern int goose_fd;

static const std::string kThreadName = "Worker Event:";

int goose_fifo_open() {
  if (goose_stream == nullptr || goose_fd == -1)
    return -1;
  return 0;
}

int goose_fifo_get_fd() {
  return goose_fd;
}

void goose_fifo_close() {}

void PipeEventWorker::configure(std::size_t capacityPowerOfTwo) {
  // This should be called before start(). No locking is added for simplicity.
  if (capacityPowerOfTwo == 0) {
    spdlog::warn("{} configure: capacityPowerOfTwo is zero, ignoring", kThreadName);
    return;
  }

  if ((capacityPowerOfTwo & (capacityPowerOfTwo - 1)) != 0) {
    spdlog::warn("{} configure: {} is not a power-of-two, ignoring", kThreadName, capacityPowerOfTwo);
    return;
  }

  m_ringCapacityConfig = capacityPowerOfTwo;
}

bool PipeEventWorker::isReadyToStart() noexcept {
  spdlog::info("{} is starting..", kThreadName);

  if (goose_fifo_open() != 0) {
    spdlog::warn("{} goose_fifo_open failed", kThreadName);
    return false;
  }

  m_fd = goose_fifo_get_fd();
  if (m_fd < 0) {
    spdlog::error("{} goose_fifo_get_fd failed: invalid FIFO fd", kThreadName);
    return false;
  }

  if (!m_ringInitialized) {
    if (!m_ring.init(m_ringCapacityConfig)) {
      spdlog::error("{} ring init failed (capacity={})", kThreadName, m_ringCapacityConfig);
      return false;
    }
    m_ringInitialized = true;
  }

  m_eventPollFd = epoll_create1(0);
  if (m_eventPollFd < 0) {
    spdlog::error("{} epoll_create1 failed: {}", kThreadName, strerror(errno));
    return false;
  }

  auto addEpollFd = [this](int fd, const char* description) -> void {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(m_eventPollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      spdlog::error("{} epoll_ctl add {} failed: {}", kThreadName, description, strerror(errno));
    } else {
      spdlog::info("{} epoll_ctl add {} ok", kThreadName, description);
    }
  };

  addEpollFd(m_fd, "EVENT-ALGO");

  m_stopEventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (m_stopEventFd < 0) {
    spdlog::error("{} eventfd failed: {}", kThreadName, strerror(errno));
    return false;
  }

  addEpollFd(m_stopEventFd, "STOP-EVENT");

  {
    std::lock_guard lk(m_waitMutex);
    m_stopped = false;
    m_hasData = false;
    m_droppedEvents.store(0, std::memory_order_relaxed);
  }

  spdlog::info("{} is ready to start", kThreadName);

  return true;
}

void PipeEventWorker::run(const compat::stop_token& stopToken) {
  constexpr int MAX_EPOLL_EVENTS = 8;
  epoll_event events[MAX_EPOLL_EVENTS];

  auto onStop = [&]() {
    m_waitCv.notify_all();
  };
  [[maybe_unused]] compat::stop_callback<decltype(onStop)> stopCb(stopToken, onStop);

  spdlog::info("{} started", kThreadName);

  while (true) {
    if (stopToken.stop_requested()) {
      spdlog::info("stop a {} requested", kThreadName);
      break;
    }

    spdlog::trace("{} waiting for events", kThreadName);

    int eventsCount = epoll_wait(m_eventPollFd, events, MAX_EPOLL_EVENTS, m_epollwaitTimeout.count());
    if (eventsCount < 0) {
      if (errno == EINTR) {
        continue;
      }
      spdlog::error("{}: epoll_wait failed: {}", kThreadName, strerror(errno));
      break;
    }

    if (eventsCount == 0) {
      spdlog::trace("{} epoll timeout, no events", kThreadName);
    } else {
      spdlog::trace("{} epoll events: {}", kThreadName, eventsCount);
      bool shouldBreak = false;
      for (int i = 0; i < eventsCount; ++i) {
        int eventFd = events[i].data.fd;

        if (eventFd == m_stopEventFd) {
          // Shutdown requested.
          uint64_t tmp;
          ::read(m_stopEventFd, &tmp, sizeof(tmp));
          shouldBreak = true;
          break;
        }

        if (eventFd == m_fd && (events[i].events & EPOLLIN)) {
          handleReadEventPipe();
        }
      }
      if (shouldBreak)
        break;
    }
  }

  // close pipe
  if (m_eventPollFd >= 0) {
    close(m_eventPollFd);
    m_eventPollFd = -1;
  }
  // close generalized pipe
  if (m_stopEventFd >= 0) {
    close(m_stopEventFd);
    m_stopEventFd = -1;
  }

  goose_fifo_close();

  spdlog::info("{} stopped", kThreadName);
}

void PipeEventWorker::stopBefore() {
  spdlog::info("{} Wake epoll_wait immediately..", kThreadName);
  if (m_stopEventFd >= 0) {
    // Wake epoll_wait immediately.
    uint64_t v = 1;
    ::write(m_stopEventFd, &v, sizeof(v));
  }
  m_waitCv.notify_all();
}

void PipeEventWorker::stopAfter() {
  std::lock_guard lk(m_waitMutex);
  m_stopped = true;
  m_hasData = true;
}

std::uint64_t PipeEventWorker::droppedEvents() const {
  return m_droppedEvents.load(std::memory_order_relaxed);
}

void PipeEventWorker::handleReadEventPipe() {
  while (true) {
    char buf[256];
    ssize_t r = ::read(m_fd, buf, sizeof(buf));
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      spdlog::error("Error reading from pipe: {}", strerror(errno));
      return;
    }
    if (r == 0)
      return;

    spdlog::trace("{} Read {} bytes from pipe: '{}'", kThreadName, r, std::string_view(buf, r));

    // Parse chunk into LogBookEvent objects using semicolon-based records.
    parseEventsFromString(std::string(buf, r));
  }
}

void PipeEventWorker::parseEventsFromString(const std::string& input) {
  // Pattern:
  //  (\d{3})   -> first number (3 digits)
  //  ;(\d{3})  -> semicolon + second number (3 digits)
  //  ;([^;]+)  -> semicolon + timestamp (any chars until next ';')
  //  ;         -> trailing semicolon (record terminator)
  static const std::regex eventRegex(R"((\d{3});(\d{3});([^;]+);)", std::regex::optimize);

  try {
    auto begin = std::sregex_iterator(input.begin(), input.end(), eventRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
      const std::smatch& match = *it;

      // match[0]: whole match "020;016;2025-12-06 22:38:14.416;"
      // match[1]: "020"
      // match[2]: "016"
      // match[3]: "2025-12-06 22:38:14.416"
      const std::string& eventIdStr = match[1].str();
      const std::string& arrayIndex = match[2].str();
      const std::string& timestamp = match[3].str();

      events::LogBookEvent ev;
      ev.eventId = static_cast<int>(std::strtol(eventIdStr.c_str(), nullptr, 10));
      ev.arrayIndex1Based = static_cast<int>(std::strtol(arrayIndex.c_str(), nullptr, 10));
      ev.timestampStr = timestamp;

      spdlog::debug("{} Sending event ID={} K={} TS={} to summator", kThreadName, ev.eventId, ev.arrayIndex1Based,
                    ev.timestampStr);
      auto postMan = GooseWorker::instance().makeSender(true);
      postMan.Send(ev);
    }
  } catch (std::exception& ex) {
    spdlog::error("{} exception: {}", kThreadName, ex.what());
  }
}

void PipeEventWorker::simulateEvent(const events::LogBookEvent& ev) const {
  auto postMan = GooseWorker::instance().makeSender(true);
  postMan.Send(ev);
}