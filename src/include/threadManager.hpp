#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "workerBase.hpp"

class ThreadManager: public workerBase {
 public:
  ThreadManager() = default;
  ~ThreadManager() override {
    stopAllThreads();
  }

  /**
   * @brief Adds a worker thread to the manager and starts it.
   * @param worker A shared pointer to the worker thread.
   */
  void addThread(std::shared_ptr<workerBase> worker) {
    if (!worker) return;
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    if (worker->start()) {
      m_threads.push_back(std::move(worker));
    }
  }

  /**
   * @brief Stops and removes all managed threads.
   */
  void stopAllThreads() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    for (auto& worker : m_threads) {
      worker->stopAndWait();
    }
    m_threads.clear();
  }

  /**
   * @brief Stops and removes the last added thread.
   */
  void stopLastThread() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    if (!m_threads.empty()) {
      m_threads.back()->stopAndWait();
      m_threads.pop_back();
    }
  }

  /**
   * @brief Gets the number of managed threads.
   * @return The number of threads.
   */
  size_t getThreadCount() const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    return m_threads.size();
  }

 protected:
  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      // Implement the thread's main work here
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work
    }
  }

  [[nodiscard]] bool isReadyToStart() noexcept override {
    // Implement any necessary checks before starting the thread
    return true;
  }

 private:
  std::vector<std::shared_ptr<workerBase>> m_threads;
  mutable std::mutex m_threadsMutex;
};
