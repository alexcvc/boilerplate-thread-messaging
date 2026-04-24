#pragma once

#include <iostream>
#include <memory>
#include <vector>

#include "taskBase.hpp"
#include "testThreads.hpp"
#include "workerBase.hpp"

class ThreadManager : public workerBase {
 public:
  ThreadManager() = default;
  ~ThreadManager() override {
    terminateAllThreads();
  }

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
    // notifyOnPost=false: MessageQueue::push() already calls notify_one(); no second wake needed
    observerIncoming->setNext(appObserver->makeSender(false));
    observerIncoming->setDirectToProcessing(processing->makeSender(false));
    appObserver->setNext(processing->makeSender(false));
    appObserver->setMirror(observerIncoming->makeSender(false));
    processing->setNext(transformation->makeSender(false));
    transformation->setNext(result->makeSender(false));

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
   *             StressMode{}      — switch to 10 ms interval for 30 s, then revert.
   *             SetNormalMode{}   — immediately exit stress; set interval to 500 ms.
   * @return false if the command buffer is full or the chain was not initialised.
   */
  bool sendObserverCommand(const ObserverCommand& cmd) {
    if (!m_observerIncoming) {
      std::cerr << "[TM] sendObserverCommand: chain not initialised." << std::endl;
      return false;
    }
    bool ok = m_observerIncoming->sendCommand(cmd);
    switch (cmd.type) {
      case ObserverCommand::Type::StopObserving:
        std::cout << "[TM] StopObserving " << cmd.durationSec.count() << "s  → "
                  << (ok ? "sent" : "FAILED (buffer full)") << std::endl;
        break;
      case ObserverCommand::Type::StartObserving:
        std::cout << "[TM] StartObserving in " << cmd.durationSec.count() << "s  → "
                  << (ok ? "sent" : "FAILED (buffer full)") << std::endl;
        break;
      default:
        break;
    }
    return ok;
  }

  /**
   * @brief Activates stress mode: FileEvent every 10 ms for 30 s, then auto-reverts to 2 s normal.
   * @return false if the chain was not initialised or the command buffer is full.
   */
  bool sendStressModeCommand() {
    return sendObserverCommand({ObserverCommand::Type::StressMode});
  }

  /**
   * @brief Immediately exits stress mode and sets the event interval to 1 s.
   * @return false if the chain was not initialised or the command buffer is full.
   */
  bool sendNormalModeCommand() {
    return sendObserverCommand({ObserverCommand::Type::SetNormalMode});
  }

  /**
   * @brief Returns the manager's stop source so callers (e.g. main) can request a global stop.
   */
  std::stop_source& stopSource() noexcept { return m_stopSource; }

  /**
   * @brief Adds a task thread to the manager and starts it.
   *        The manager's stop token is forwarded so a global stop also stops this task.
   * @param task A shared pointer to the task.
   */
  void addTask(std::shared_ptr<messaging::TaskBase> task) {
    if (!task)
      return;
    if (task->start(m_stopSource.get_token())) {
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
    m_stopSource = std::stop_source{};
    for (auto& task : m_tasks) {
      if (!task->isRunning()) {
        if (task->start(m_stopSource.get_token())) {
          std::cout << "[ThreadManager] Restarted thread." << std::endl;
        }
      }
    }
  }

  /**
   * @brief Stops all managed threads but keeps them in the manager.
   */
  void stopAllThreads() {
    m_stopSource.request_stop();
    for (auto& task : m_tasks) {
      task->wakeUp();
      task->join();
    }
  }

  /**
   * @brief Stops all managed threads and removes them from the manager.
   */
  void terminateAllThreads() {
    stopAllThreads();
    m_tasks.clear();
  }

  /**
   * @brief Stops and removes the last added thread.
   */
  void stopLastThread() {
    if (!m_tasks.empty()) {
      m_tasks.back()->stopAndWait();
      m_tasks.pop_back();
    }
  }

  /**
   * @brief Gets the number of managed threads.
   * @return The number of threads.
   */
  size_t getThreadCount() const noexcept {
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
  std::stop_source m_stopSource;
  std::vector<std::shared_ptr<messaging::TaskBase>> m_tasks;  // accessed from main thread only
  std::shared_ptr<messaging::ObserverIncomingThread> m_observerIncoming;
};
