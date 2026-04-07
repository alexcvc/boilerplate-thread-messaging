#pragma once

#include <functional>
#include <memory>
#include <typeinfo>

#include "messaging/messageQueue.hpp"

namespace messaging {

/**
 * @file
 * @ingroup messaging
 * @brief `Sender` endpoint that pushes messages into a `MessageQueue`.
 *
 * Semantics:
 * - `Sender` is a lightweight handle with a non-owning pointer to a
 *   `MessageQueue`. The queue must outlive the `Sender` (typically provided by
 *   a `Receiver`).
 * - `Send(msg)` will copy the provided payload and wrap it into a
 *   `MessageWrapper<T>` before enqueuing.
 * - If no queue is associated, an error is logged and the message is dropped.
 * - Optionally, the `Sender` can invoke a post-send callback (used by
 *   `Receiver::makeSender(true)`) to notify the receiver immediately after
 *   enqueueing.
 *
 * Example:
 * ```cpp
 * struct Command { int code; };
 *
 * class Consumer : public messaging::Receiver {
 *  public:
 *   void loopOnce() {
 *     if (auto base = messageQueue().try_pop()) {
 *       if (auto cmd = std::dynamic_pointer_cast<messaging::MessageWrapper<Command>>(base)) {
 *         spdlog::info("got command {}", cmd->contents_.code);
 *       }
 *     }
 *   }
 * };
 *
 * Consumer c;
 * messaging::Sender tx = c.makeSender();
 * tx.Send(Command{7});
 * c.loopOnce();
 * ```
 */
class Sender {
 public:
  using MessageQueuePtr = MessageQueue*;

  /// @brief Default-constructs a Sender with no associated message queue.
  Sender() = default;

  /// @brief Copy constructor
  Sender(const Sender& other) = default;

  /// @brief Copy assignment operator
  Sender& operator=(const Sender& other) = default;

  /// @brief Move constructor
  Sender(Sender&& other) noexcept = default;

  /// @brief Move assignment operator
  Sender& operator=(Sender&& other) noexcept = default;

  /**
   * @brief Constructs a Sender object with the provided message queue pointer.
   * @param queue A pointer to the MessageQueue object to associate with the sender.
   *              This queue will be used for pushing messages. The pointer is
   *              non-owning and must remain valid for the lifetime of this Sender.
   */
  explicit Sender(MessageQueuePtr queue) : messageQueuePtr_(queue) {}

  /**
   * @brief Constructs a Sender object with the provided message queue reference.
   * @param queue A reference to the MessageQueue object to associate with the sender.
   *              This queue will be used for pushing messages.
   */
  explicit Sender(MessageQueue& queue) : messageQueuePtr_(&queue) {}

  /// @brief Constructs a Sender bound to a queue and an optional post-notify callback.
  explicit Sender(MessageQueuePtr queue,
                  std::function<void(const std::type_info&, const std::shared_ptr<MessageBase>&)> postNotify)
      : messageQueuePtr_(queue), postNotify_(std::move(postNotify)) {}

  /**
   * @brief Sends a message to the associated message queue, if available.
   * @tparam Message The type of the message to be sent.
   * @param message The message to send.
   *
   * Thread-safe if the associated queue is thread-safe. If the `Sender` has no
   * associated queue, logs an error and returns without sending.
   */
  template <typename Message>
  void Send(const Message& message) {
    const char* typeName = typeid(Message).name();

    if (!ensureQueueAvailable(typeName)) {
      return;
    }

    // Construct the wrapper once so we can both enqueue and notify with the same instance
    auto wrapped = std::make_shared<MessageWrapper<Message>>(message);
    messageQueuePtr_->push(wrapped);

    // Optional post-send notification to receiver
    if (postNotify_) {
      postNotify_(typeid(Message), wrapped);
    }
  }

 private:
  /// @brief Thread-safe message queue used for sending messages (non-owning).
  MessageQueuePtr messageQueuePtr_{nullptr};
  /// @brief Optional callback to notify after posting a message.
  std::function<void(const std::type_info&, const std::shared_ptr<MessageBase>&)> postNotify_{};

  /// @brief Ensures that a message queue is available before sending.
  /// @param messageTypeName The name of the message type being sent.
  /// @return True if the queue is available, false otherwise.
  bool ensureQueueAvailable(const char* messageTypeName) const {
    if (messageQueuePtr_ != nullptr) {
      return true;
    }

    spdlog::error("Sender: no queue associated with the sender of message {}", messageTypeName);
    return false;
  }
};

}  // namespace messaging