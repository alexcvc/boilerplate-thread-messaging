#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "Message.hpp"
#include "spdlog/spdlog.h"

namespace messaging {

/**
 * @file
 * @ingroup messaging
 * @brief A thread-safe message queue.
 *
 * This queue stores messages as `std::shared_ptr<MessageBase>` and provides
 * blocking (`wait`), timed (`wait_for`) and non-blocking (`try_pop`) pop
 * operations. It is intended to be used together with `Sender` and
 * `Receiver`.
 *
 * Thread-safety:
 * - All public methods are thread-safe.
 * - Multiple producers and multiple consumers are supported.
 * - `push` wakes all waiters, each of which will attempt to pop the next item.
 *
 * Usage example:
 * ```cpp
 * struct Tick { int value; };
 * messaging::MessageQueue q;
 * q.push(Tick{1});
 * if (auto base = q.try_pop()) {
 *   if (auto tick = std::dynamic_pointer_cast<messaging::MessageWrapper<Tick>>(base)) {
 *     spdlog::info("tick: {}", tick->contents_.value);
 *   }
 * }
 * ```
 */
class MessageQueue {
  using QueueType = std::queue<std::shared_ptr<MessageBase>>;

  mutable std::mutex m_mutex;                   ///< Mutex for synchronizing access to the queue.
  std::condition_variable m_conditionVariable;  ///< Condition variable for waiting on new messages.
  QueueType m_queue;                            ///< The underlying queue storing messages.

  /**
   * @brief Pop the front element from the queue.
   * @pre Caller must hold m_mutex.
   */
  [[nodiscard]] std::shared_ptr<MessageBase> pop() noexcept {
    if (m_queue.empty()) {
      return {};
    }
    auto result = m_queue.front();
    m_queue.pop();
    return result;
  }

  /**
   * @brief Retrieve the front message from the queue without removing it.
   * @return A shared pointer to the front message if the queue is non-empty, or an empty shared pointer if the queue is
   * empty.
   */
  [[nodiscard]] const std::shared_ptr<MessageBase> front() const noexcept {
    if (m_queue.empty()) {
      return {};
    }
    return m_queue.front();
  }

 public:
  /**
   * @brief Push a new message into the queue.
   *
   * @tparam T The type of the message.
   * @param msg The message to push.
   *
   * Stores a copy of `msg` by wrapping it into `MessageWrapper<T>`.
   * Notifies all waiting consumers.
   */
  template <typename T>
  void push(const T& msg) {
    {
      std::scoped_lock lock(m_mutex);
      m_queue.push(std::make_shared<MessageWrapper<T>>(msg));
    }
    m_conditionVariable.notify_all();
  }

  /**
   * @brief Push a shared pointer to message into the queue.
   *
   * @tparam T The type of the message.
   * @param msgPtr The message to push.
   *
   * This overload allows pushing a pre-constructed `shared_ptr` to a
   * `MessageBase`-derived instance. Ownership is shared with the queue.
   */
  template <typename T>
  void push(std::shared_ptr<T> msgPtr) {
    {
      std::scoped_lock lock(m_mutex);
      m_queue.push(std::move(msgPtr));
    }
    m_conditionVariable.notify_all();
  }

  /**
   * @brief Pop a message from the queue if available.
   * @return A shared pointer to the popped message, or empty if the queue is empty.
   *
   * Non-blocking: returns immediately.
   */
  [[nodiscard]] std::shared_ptr<MessageBase> try_pop() noexcept {
    std::unique_lock lock(m_mutex);
    return pop();
  }

  /**
   * @brief Wait for and pop a message from the queue.
   * @return A shared pointer to the popped message.
   *
   * Blocks the calling thread until an item becomes available. If the waiting
   * thread is woken spuriously, it will continue to wait until the queue is
   * non-empty.
   */
  [[nodiscard]] std::shared_ptr<MessageBase> wait() noexcept {
    std::unique_lock lock(m_mutex);
    m_conditionVariable.wait(lock, [&] {
      spdlog::trace("size of the queue: {}", m_queue.size());
      return !m_queue.empty();
    });
    return pop();
  }

  /**
   * @brief Wait for and pop a message from the queue with a timeout.
   * @param duration The duration to wait for a message.
   * @return A shared pointer to the popped message, or empty if timeout occurred.
   *
   * If the timeout expires without any item being pushed, an empty `shared_ptr`
   * is returned.
   */
  [[nodiscard]] std::shared_ptr<MessageBase> wait_for(const std::chrono::milliseconds& duration) noexcept {
    std::unique_lock lock(m_mutex);
    const bool hasItem = m_conditionVariable.wait_for(lock, duration, [&] {
      return !m_queue.empty();
    });

    if (!hasItem) {
      return {};
    }
    return pop();
  }

  /**
   * @brief Clear the message queue.
   *
   * Discards all pending messages. Concurrent producers/consumers will be
   * synchronized by the internal mutex. No notifications are sent.
   */
  void clear() noexcept {
    std::scoped_lock lock(m_mutex);
    QueueType empty;
    m_queue.swap(empty);
  }

  /**
   * @brief Check if the queue is empty.
   * @return True if the queue is empty, false otherwise.
   */
  [[nodiscard]] bool empty() const noexcept {
    std::scoped_lock lock(m_mutex);
    return m_queue.empty();
  }
};

}  // namespace messaging