#pragma once

#include <functional>

#include "messaging/messageReceiver.hpp"
#include "messaging/messageSender.hpp"
#include "workerBase.hpp"

namespace messaging {

/**
 * @class TaskBase
 * @brief A base class that combines message receiving, sending, and worker thread functionality.
 *
 * This class inherits from:
 * - `messaging::Receiver`: To receive and handle messages via a queue.
 * - `messaging::Sender`: To send messages to other components.
 * - `workerBase`: To provide a dedicated thread of execution.
 */
class TaskBase : public Receiver, public Sender, public workerBase {
 public:
  TaskBase() : Receiver(), Sender(), workerBase() {}
  ~TaskBase() override = default;

  // Task is non-copyable and non-movable due to workerBase and Receiver constraints
  TaskBase(const TaskBase&) = delete;
  TaskBase& operator=(const TaskBase&) = delete;
  TaskBase(TaskBase&&) = delete;
  TaskBase& operator=(TaskBase&&) = delete;

 protected:
  /**
   * @brief From messaging::Receiver: must be implemented by derived classes to react to messages.
   */
  void onPostMessageReceived(const std::type_info& messageTypeInfo,
                             const std::shared_ptr<MessageBase>& message) override = 0;

  /**
   * @brief From workerBase: must be implemented by derived classes to define worker readiness.
   */
  [[nodiscard]] bool isReadyToStart() noexcept override = 0;

  /**
   * @brief From workerBase: must be implemented by derived classes to define the main execution loop.
   */
  void run(std::stop_token stopToken) override = 0;
};

/**
 * @class Task
 * @brief A concrete implementation of TaskBase that uses a lambda for its run loop.
 */
class Task : public TaskBase {
 public:
  using RunFunction = std::function<void(Task&, std::stop_token)>;
  using OnMessageFunction =
      std::function<void(Task&, const std::type_info&, const std::shared_ptr<MessageBase>&)>;

  explicit Task(RunFunction runFunc, OnMessageFunction onMsgFunc = nullptr)
      : TaskBase(), m_runFunc(std::move(runFunc)), m_onMsgFunc(std::move(onMsgFunc)) {
    // By default, a Task should be able to send messages to its own queue.
    // This allows the task to easily post messages to itself if needed.
    static_cast<Sender&>(*this) = makeSender();
  }

  /**
   * @brief Expose messageQueue for the lambda.
   */
  using Receiver::messageQueue;

 protected:
  void onPostMessageReceived(const std::type_info& messageTypeInfo,
                             const std::shared_ptr<MessageBase>& message) override {
    if (m_onMsgFunc) {
      m_onMsgFunc(*this, messageTypeInfo, message);
    }
  }

  [[nodiscard]] bool isReadyToStart() noexcept override {
    return static_cast<bool>(m_runFunc);
  }

  void run(std::stop_token stopToken) override {
    if (m_runFunc) {
      m_runFunc(*this, stopToken);
    }
  }

 private:
  RunFunction m_runFunc;
  OnMessageFunction m_onMsgFunc;
};

}  // namespace messaging
