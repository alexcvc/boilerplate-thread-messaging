#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

class workerBase {
 public:
  workerBase() = default;
  virtual ~workerBase() = default;

  // Non-copyable, non-movable
  workerBase(const workerBase&) = delete;
  workerBase& operator=(const workerBase&) = delete;
  workerBase(workerBase&&) = delete;
  workerBase& operator=(workerBase&&) = delete;

  /**
   * @brief Starts the execution of the worker thread.
   * @return true if the worker thread was successfully started; false otherwise.
   */
  [[nodiscard]] bool start() noexcept {
    if (!isReadyToStart()) {
      return false;
    }

    bool expectedNotRunning = false;
    if (!m_running.compare_exchange_strong(expectedNotRunning, true, std::memory_order_acq_rel)) {
      return false;
    }

    const auto markStopped = [this]() noexcept {
      m_running.store(false, std::memory_order_release);
    };

    try {
      m_thread = std::jthread([this, markStopped](std::stop_token stopToken) {
        run(stopToken);
        markStopped();
      });
    } catch (...) {
      markStopped();
      return false;
    }

    return true;
  }

  template <typename PreStopHook, typename PostStopHook>
    requires std::invocable<PreStopHook&> && std::invocable<PostStopHook&>
  void stopWithHooks(PreStopHook&& beforeStop,
                     PostStopHook&& afterStop) noexcept(std::is_nothrow_invocable_v<PreStopHook&> &&
                                                        std::is_nothrow_invocable_v<PostStopHook&>) {
    std::invoke(std::forward<PreStopHook>(beforeStop));
    stop();
    std::invoke(std::forward<PostStopHook>(afterStop));
  }

  /**
   * @brief Signals the worker thread to stop. Non-blocking: returns immediately.
   * Call join() or stopAndWait() to block until the thread finishes.
   */
  void stop() noexcept {
    if (m_thread.joinable()) {
      m_thread.request_stop();
    }
    wakeUp();
  }

  /**
   * @brief Blocks until the worker thread has finished execution.
   */
  void join() noexcept {
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  /**
   * @brief Signals stop and blocks until the worker thread finishes.
   */
  void stopAndWait() noexcept {
    stop();
    join();
  }

  /**
   * @brief Notifies all waiting threads to wake up.
   */
  void wakeUp() noexcept {
    m_waitCv.notify_all();
  }

  /**
   * @brief Checks whether the worker thread is currently running.
   * @return true if the worker thread is running, false otherwise.
   */
  [[nodiscard]] bool isRunning() const noexcept {
    return m_running.load(std::memory_order_acquire);
  }

  /**
   * @brief Checks whether a stop request has been issued.
   * @return true if a stop request has been made, false otherwise.
   */
  [[nodiscard]] bool isStopRequested() const noexcept {
    return m_thread.get_stop_token().stop_requested();
  }

 protected:
  /**
   * @brief Determines whether the worker is ready to start.
   * @return true if the worker is ready to start; otherwise, false.
   */
  [[nodiscard]] virtual bool isReadyToStart() noexcept = 0;

  /**
   * @brief Main worker loop implemented by the derived class.
   */
  virtual void run(std::stop_token stopToken) = 0;

  /**
   * @brief Waits until pred() is satisfied, a stop is requested, or the timeout elapses.
   * @return true if pred() became true; false on timeout or stop.
   */
  template <typename Rep, typename Period, typename Pred>
  bool waitFor(std::stop_token stopToken, std::chrono::duration<Rep, Period> timeout, Pred&& pred) {
    std::unique_lock lock(m_waitMutex);
    return m_waitCv.wait_for(lock, timeout,
                             [&] { return stopToken.stop_requested() || std::forward<Pred>(pred)(); });
  }

  // Raw primitives — prefer waitFor() over locking these directly.
  std::mutex m_waitMutex;            ///< Mutex used in wait-related operations.
  std::condition_variable m_waitCv;  ///< Condition variable for wait-related operations.

 private:
  std::atomic<bool> m_running{false};  ///< Indicates whether the worker thread is currently running.
  std::jthread m_thread;               ///< Worker thread used for executing the main worker loop.
};