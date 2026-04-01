/* SPDX-License-Identifier: A-EBERLE */
/****************************************************************************\
**                   _        _____ _               _                      **
**                  / \      | ____| |__   ___ _ __| | ___                 **
**                 / _ \     |  _| | '_ \ / _ \ '__| |/ _ \                **
**                / ___ \ _  | |___| |_) |  __/ |  | |  __/                **
**               /_/   \_(_) |_____|_.__/ \___|_|  |_|\___|                **
**                                                                         **
*****************************************************************************
** Copyright (c) 2010 - 2025 A. Eberle GmbH & Co. KG. All rights reserved. **
\****************************************************************************/

#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

/**
 * @brief Provides a thread-safe queue for concurrent data access and manipulation.
 *
 * This class facilitates thread-safe storage and retrieval of elements using internal
 * mutex and condition variable mechanisms. It ensures safe access for multiple threads
 * by providing synchronized operations for push, pop, and peek functionalities.
 */
template <typename T>
class ThreadSafeQueue {
  /**
   * Default constructor that initializes an empty ThreadSafeQueue.
   * Ensures the underlying data structures are properly configured for usage.
   *
   * @return A newly constructed ThreadSafeQueue object.
   */
 public:
  ThreadSafeQueue() = default;

  /**
   * Adds the specified value to the queue in a thread-safe manner.
   *
   * @param value The value to be added to the queue.
   */
  void push(const T& value) {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_queue.push(value);
    }
    m_cv.notify_one();
  }

  /**
   * Pushes an element into the thread-safe queue. This function moves the given value
   * into the queue and notifies one waiting thread that an element has been added.
   *
   * @param value The element to be moved into the queue.
   */
  void push(T&& value) {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_queue.push(std::move(value));
    }
    m_cv.notify_one();
  }

  /**
   * Attempts to remove and return the front element of the queue.
   * If the queue is empty, returns an empty std::optional.
   * @return An optional containing the front element of the queue if available,
   *         or an empty optional if the queue is empty.
   */
  std::optional<T> tryPop() {
    std::lock_guard lock(m_mutex);
    if (m_queue.empty())
      return std::nullopt;
    T value = std::move(m_queue.front());
    m_queue.pop();
    return value;
  }

  /**
   * Removes and returns an element from the front of the queue, blocking if necessary until
   * either an element becomes available or the provided stop condition is met.
   *
   * @tparam Predicate
   *     A callable object or function that evaluates to true when the stopping condition is met.
   *     The predicate is called repeatedly while waiting.
   * @param stopPredicate
   *     A callable object or function that defines the stopping condition and will be evaluated
   *     inside the blocking wait mechanism.
   * @return
   *     An optional containing the removed element if an element was successfully popped from
   *     the queue. Returns an empty optional if the queue is empty and the stop predicate is met.
   */
  template <typename Predicate>
  std::optional<T> waitPop(Predicate&& stopPredicate) {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] {
      return !m_queue.empty() || stopPredicate();
    });

    if (m_queue.empty())
      return std::nullopt;

    T value = std::move(m_queue.front());
    m_queue.pop();
    return value;
  }

  /**
   * Checks if the thread-safe queue is empty.
   * @return True if the queue is empty, false otherwise.
   */
  bool empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty();
  }

 private:
  mutable std::mutex m_mutex;    ///< Mutex to synchronize access to the shared resources within the class.
  std::condition_variable m_cv;  ///< Condition variable used to synchronize access to the queue
  std::queue<T> m_queue;         ///< The underlying container used to store the elements of the queue.
};
