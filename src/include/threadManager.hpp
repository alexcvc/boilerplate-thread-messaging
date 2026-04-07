#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

#include "taskBase.hpp"
#include "testThreads.hpp"
#include "workerBase.hpp"

class ThreadManager : public workerBase {
 public:
  ThreadManager() = default;
  ~ThreadManager() override { terminateAllThreads(); }

  /**
   * @brief Initializes the test thread chain (5 threads).
   */
  void initTestChain() {
    // Create the 5 test threads
    auto observerIncoming = std::make_shared<messaging::ObserverIncomingThread>();
    auto appObserver = std::make_shared<messaging::ApplicationObserverThread>();
    auto processing = std::make_shared<messaging::ProcessingThread>();
    auto transformation = std::make_shared<messaging::TransformationThread>();
    auto result = std::make_shared<messaging::ResultThread>();

    // Keep a handle for command dispatch
    m_observerIncoming = observerIncoming;

    // Link them
    observerIncoming->setNext(appObserver->makeSender());
    observerIncoming->setDirectToProcessing(processing->makeSender());
    appObserver->setNext(processing->makeSender());
    appObserver->setMirror(observerIncoming->makeSender());
    processing->setNext(transformation->makeSender());
    transformation->setNext(result->makeSender());

    // Add to ThreadManager
    addTask(observerIncoming);
    addTask(appObserver);
    addTask(processing);
    addTask(transformation);
    addTask(result);

    std::cout << "[ThreadManager] Test chain initialized with 5 threads." << std::endl;
  }

  /**
   * @brief Sends a raw command to ObserverIncomingThread via its SpScRingBuffer.
   *        ThreadManager is the single producer; the observer thread is the consumer.
   * @param cmd  StopObserving{N}  — pause event generation for N seconds.
   *             StartObserving{N} — (re)start event generation after N seconds.
   *             StressMode{}      — switch to 100 ms interval for 20 s, then revert.
   *             SetNormalMode{}   — immediately exit stress; set interval to 1 s.
   * @return false if the command buffer is full or the chain was not initialised.
   */
  bool sendObserverCommand(const ObserverCommand& cmd) {
    if (!m_observerIncoming) {
      std::cerr << "[ThreadManager] sendObserverCommand: chain not initialised." << std::endl;
      return false;
    }
    return m_observerIncoming->sendCommand(cmd);
  }

  /**
   * @brief Activates stress mode: FileEvent every 100 ms for 20 s, then auto-reverts to 2 s normal.
   * @return false if the chain was not initialised or the command buffer is full.
   */
  bool sendStressModeCommand() {
    return sendObserverCommand({ObserverCommand::Type::StressMode, 0});
  }

  /**
   * @brief Immediately exits stress mode and sets the event interval to 1 s.
   * @return false if the chain was not initialised or the command buffer is full.
   */
  bool sendNormalModeCommand() {
    return sendObserverCommand({ObserverCommand::Type::SetNormalMode, 0});
  }

  /**
   * @brief Adds a task thread to the manager and starts it.
   * @param task A shared pointer to the task.
   */
  void addTask(std::shared_ptr<messaging::TaskBase> task) {
    if (!task) return;
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    if (task->start()) {
      m_tasks.push_back(std::move(task));
    }
  }

  /**
   * @brief Creates and adds a task thread with a lambda run function.
   * @param runFunc The lambda function to run in the thread.
   * @return A shared pointer to the created task.
   */
  std::shared_ptr<messaging::Task> addTask(messaging::Task::RunFunction runFunc) {
    auto task = std::make_shared<messaging::Task>(std::move(runFunc));
    addTask(task);
    return task;
  }

  /**
   * @brief Restarts all managed threads that are not currently running.
   */
  void restartAllThreads() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    for (auto& task : m_tasks) {
      if (!task->isRunning()) {
        if (task->start()) {
          std::cout << "[ThreadManager] Restarted thread." << std::endl;
        }
      }
    }
  }

  /**
   * @brief Stops all managed threads but keeps them in the manager.
   */
  void stopAllThreads() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    for (auto& task : m_tasks) {
      task->stopAndWait();
    }
  }

  /**
   * @brief Stops all managed threads and removes them from the manager.
   */
  void terminateAllThreads() {
    stopAllThreads();
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    m_tasks.clear();
  }

  /**
   * @brief Stops and removes the last added thread.
   */
  void stopLastThread() {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    if (!m_tasks.empty()) {
      m_tasks.back()->stopAndWait();
      m_tasks.pop_back();
    }
  }

  /**
   * @brief Gets the number of managed threads.
   * @return The number of threads.
   */
  size_t getThreadCount() const {
    std::lock_guard<std::mutex> lock(m_threadsMutex);
    return m_tasks.size();
  }

 protected:
  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      // Implement the thread's main work here
      std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Simulate work
    }
  }

  [[nodiscard]] bool isReadyToStart() noexcept override {
    // Implement any necessary checks before starting the thread
    return true;
  }

 private:
  std::vector<std::shared_ptr<messaging::TaskBase>> m_tasks;
  mutable std::mutex m_threadsMutex;
  std::shared_ptr<messaging::ObserverIncomingThread> m_observerIncoming;
};
