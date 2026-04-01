#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>

#include "threading/JThread.hpp"

using namespace std::chrono_literals;

class WorkerBase {
 public:
  WorkerBase() = default;
  virtual ~WorkerBase() = default;

  // Non-copyable
  WorkerBase(const WorkerBase&) = delete;
  WorkerBase& operator=(const WorkerBase&) = delete;

  [[nodiscard]] bool start() noexcept {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      return false;

    m_stopRequested.store(false, std::memory_order_relaxed);

    // Check / prepare before really starting the thread
    if (!isReadyToStart()) {
      return false;
    }

    try {
      m_thread = compat::JThread([this](const compat::stop_token& stopToken) {
        // start
        m_running.store(true, std::memory_order_relaxed);
        // run
        run(stopToken);
        // stop
        m_running.store(false, std::memory_order_relaxed);
      });

    } catch (...) {
      m_running.store(false, std::memory_order_relaxed);
      return false;
    }

    return true;
  }

  /**
   * @brief Stops the worker thread, performing pre-stop and post-stop actions.
   *
   * @tparam PreStopFn Type of the pre-stop callback function.
   * @tparam PostStopFn Type of the post-stop callback function.
   * @param preStop A callable object executed before the stop request is processed.
   * @param postStop A callable object executed after the stop request is processed.
   */
  template <typename PreStopFn, typename PostStopFn>
  void stop(PreStopFn&& preStop, PostStopFn&& postStop) noexcept {
    // User-supplied "pre-stop" lambda
    std::forward<PreStopFn>(preStop)();

    // stop thread
    stop();

    // User-supplied "post-stop" lambda
    std::forward<PostStopFn>(postStop)();
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
  virtual void run(const compat::stop_token& stopToken) = 0;

  // members
  std::mutex m_waitMutex;            ///< Mutex used in wait-related
  std::condition_variable m_waitCv;  ///< wait-related condition

 private:
  std::atomic<bool> m_running{false};        ///< Indicates whether the worker thread is currently running.
  std::atomic<bool> m_stopRequested{false};  ///< indicates whether a stop request has been made for the worker.
  compat::JThread m_thread;                  ///< Worker thread used for executing the main worker loop
};