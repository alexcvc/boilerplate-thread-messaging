#pragma once

#include <chrono>
#include <string>

#include "DataTypes.hpp"
#include "SpscRingBuffer.hpp"
#include "WorkerBase.hpp"

// Worker responsible for listening to events from a pipe (FIFO or socket)
// and optionally mirroring them into shared memory.
//
// It pushes PipeEvent objects into a dedicated queue. SHM reflection is not
// implemented here and should be done in real code if needed.

class PipeEventWorker : public WorkerBase {
 public:
  static PipeEventWorker& instance() {
    static PipeEventWorker s_instance;
    return s_instance;
  }

  // Optional configuration before start().
  // ringCapacity must be power-of-two. If not called, the default is 4096.
  void configure(std::size_t capacityPowerOfTwo);

  [[nodiscard]] std::chrono::milliseconds WaitIntervalMs() const {
    return m_epollwaitTimeout;
  }

  void setWaitIntervalMs(const std::chrono::milliseconds& pollIntervalMs) {
    m_epollwaitTimeout = pollIntervalMs;
  }

  // Get a number of dropped events due to ring buffer overflow.
  [[nodiscard]] std::uint64_t droppedEvents() const;

  // New overload that always runs derived-specific pre/post logic
  void stop() noexcept {
    WorkerBase::stop(
        [this]() noexcept {
          stopBefore();
        },
        [this]() noexcept {
          stopAfter();
        });
  }

  void simulateEvent(const events::LogBookEvent& ev) const;

 protected:
  // Main thread loop
  void run(const compat::stop_token& stopToken) override;

  [[nodiscard]] bool isReadyToStart() noexcept override;

  void stopBefore();
  void stopAfter();

 private:
  PipeEventWorker() = default;
  ~PipeEventWorker() override = default;

  PipeEventWorker(const PipeEventWorker&) = delete;
  PipeEventWorker& operator=(const PipeEventWorker&) = delete;
  PipeEventWorker(PipeEventWorker&&) = delete;
  PipeEventWorker& operator=(PipeEventWorker&&) = delete;

  std::chrono::milliseconds m_epollwaitTimeout = 1000ms;  // example
  // Handle EPOLLIN on FIFO.
  void handleReadEventPipe();
  void parseEventsFromString(const std::string& input);

  int m_fd{-1};                                   ///< FIFO fd
  int m_eventPollFd{-1};                          ///< epoll fd
  int m_stopEventFd{-1};                          ///< eventfd used for shutdown
  std::atomic<bool> m_running{false};             ///< is task running
  std::string m_linePending;                      ///< buffer for assembling one line
  SpscRingBuffer<events::LogBookEvent> m_ring;    ///< SPSC ring buffer between epoll thread and worker thread.
  std::size_t m_ringCapacityConfig{64};           ///< capacity
  bool m_ringInitialized{false};                  ///< is ring initialized
  std::atomic<std::uint64_t> m_droppedEvents{0};  ///< Dropped events counter when ring buffer is full.
  bool m_hasData{false};                          ///< producer signaled at least once.
  bool m_stopped{false};                          ///< is task stopped
};