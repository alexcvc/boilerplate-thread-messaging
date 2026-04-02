#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

class WorkerBase {
 public:
  WorkerBase() = default;
  virtual ~WorkerBase() = default;

  // Non-copyable
  WorkerBase(const WorkerBase&) = delete;
  WorkerBase& operator=(const WorkerBase&) = delete;

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

    m_stopRequested.store(false, std::memory_order_relaxed);

    const auto markStopped = [this]() noexcept {
      m_running.store(false, std::memory_order_relaxed);
    };

    try {
      m_thread = std::jthread([this, markStopped](std::stop_token stopToken) mutable {
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
                     PostStopHook&& afterStop) noexcept(std::is_nothrow_invocable_v<PreStopHook&> && noexcept(stop()) &&
                                                        std::is_nothrow_invocable_v<PostStopHook&>) {
    auto&& preHook = std::forward<PreStopHook>(beforeStop);
    auto&& postHook = std::forward<PostStopHook>(afterStop);

    std::invoke(preHook);   // user pre-stop hook
    stop();                 // core stop operation
    std::invoke(postHook);  // user post-stop hook
  }

  /**
   * @brief Stops the execution of the associated worker thread if it is running.
   * This operation is noexcept ensuring it does not throw exceptions during execution.
   */
  void stop() noexcept {
    if (m_thread.get_id() != std::thread::id{}) {
      m_stopRequested.store(true, std::memory_order_relaxed);
      m_thread.request_stop();
    }
    wakeUp();
  }

  /**
   * @brief Notifies all waiting threads to wake up.
   */
  void wakeUp() {
    m_waitCv.notify_all();
  }

  /**
   * @brief Checks whether the worker thread is currently running.
   * @return true if the worker thread is running, false otherwise.
   */
  [[nodiscard]] bool isRunning() const noexcept {
    return m_running.load(std::memory_order_relaxed);
  }

  /**
   * @brief Checks whether a stop request has been issued.
   * @return true if a stop request has been made, false otherwise.
   */
  [[nodiscard]] bool isStopRequested() const noexcept {
    return m_stopRequested.load(std::memory_order_relaxed);
  }

 protected:
  /**
   * @brief Determines whether the worker is ready to start.
   * @return true if the worker is ready to start; otherwise, false.
   */
  [[nodiscard]] virtual bool isReadyToStart() noexcept = 0;

  /**
   * Main worker loop implemented by the derived class.
   */
  virtual void run(std::stop_token& stopToken) = 0;

  // members
  std::mutex m_waitMutex;            ///< Mutex used in wait-related
  std::condition_variable m_waitCv;  ///< wait-related condition

 private:
  std::atomic<bool> m_running{false};        ///< Indicates whether the worker thread is currently running.
  std::atomic<bool> m_stopRequested{false};  ///< indicates whether a stop request has been made for the worker.
  std::jthread m_thread;                     ///< Worker thread used for executing the main worker loop
};