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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <vector>

template <typename T>
/**
 * @class SpscRingBuffer
 * @brief A single-producer, single-consumer circular ring buffer implementation.
 *
 * This class provides a lock-free, single-producer, single-consumer ring buffer
 * for efficient data transfer in concurrent environments. The capacity of the
 * buffer must be a power-of-two for proper operation.
 *
 * @tparam T The type of elements stored in the ring buffer.
 */
class SpscRingBuffer {
  /**
   * @brief Default constructor for the SpscRingBuffer class.
   *
   * Initializes the ring buffer with default arguments.
   * Sets the initial capacity to zero, mask to zero, head and tail pointers to zero,
   * and marks the buffer as uninitialized.
   */
 public:
  SpscRingBuffer() = default;

  /**
   * Initializes the ring buffer with the given capacity.
   *
   * @param capacityPowerOfTwo The capacity of the buffer, must be a power of two.
   * @return True if initialization is successful, false if the capacity is zero or not a power of two.
   *
   * @details This method performs the following:
   * - Validates that the provided capacity is a power of two and not zero.
   * - Resizes the internal buffer to match the given capacity.
   * - Calculates the mask value for index wrapping.
   * - Resets internal indices (head and tail) to the initial state.
   * - Marks the buffer as initialized.
   *
   * Proper initialization with a valid capacity is required before performing any operations on the buffer.
   */
  [[nodiscard]] bool init(std::size_t capacityPowerOfTwo) {
    if (capacityPowerOfTwo == 0)
      return false;

    if ((capacityPowerOfTwo & (capacityPowerOfTwo - 1)) != 0)
      return false;

    m_buffer.resize(capacityPowerOfTwo);
    m_capacity = capacityPowerOfTwo;
    m_mask = capacityPowerOfTwo - 1;
    m_head.store(0, std::memory_order_relaxed);
    m_tail.store(0, std::memory_order_relaxed);
    m_initialized = true;
    return true;
  }

  /**
   * Attempts to push a value into the ring buffer.
   *
   * @param value The value to be inserted into the buffer.
   * @return True if the value is successfully pushed into the buffer,
   *         false if the buffer is full or not initialized.
   *
   * The function performs lock-free operations to add the specified
   * value to the ring buffer. It calculates the next position in the
   * circular buffer, and if the buffer is not full (determined by checking
   * head and tail positions), it inserts the value and updates the
   * head pointer atomically.
   *
   * If the buffer has not been initialized or if it is full, the function
   * will return false without modifying the buffer.
   */
  [[nodiscard]] bool push(const T& value) {
    if (!m_initialized)
      return false;

    std::size_t head = m_head.load(std::memory_order_relaxed);
    std::size_t next = (head + 1) & m_mask;

    if (next == m_tail.load(std::memory_order_acquire)) {
      // Buffer is full.
      return false;
    }

    m_buffer[head] = value;
    m_head.store(next, std::memory_order_release);
    return true;
  }

  /**
   * Removes an element from the front of the buffer.
   *
   * @param[out] out Reference to a variable where the removed element will be stored.
   * @return True if an element was successfully removed, false if the buffer is empty or uninitialized.
   *
   * This method first checks if the buffer is initialized. Next, it verifies if the buffer is empty
   * by comparing the tail and head indices. If the buffer is not empty, the method retrieves the
   * element at the current tail index, calculates the next tail index based on the buffer's mask size,
   * and updates the tail to the next position. The operation uses memory order semantics to
   * ensure proper synchronization in a single-producer single-consumer context.
   */
  [[nodiscard]] bool pop(T& out) {
    if (!m_initialized)
      return false;

    std::size_t tail = m_tail.load(std::memory_order_relaxed);
    if (tail == m_head.load(std::memory_order_acquire)) {
      // Buffer is empty.
      return false;
    }

    out = m_buffer[tail];
    std::size_t next = (tail + 1) & m_mask;
    m_tail.store(next, std::memory_order_release);
    return true;
  }

  /**
   * @brief Waits for an element to become available in the buffer and pops it within the specified timeout.
   *
   * This method blocks the current thread and waits until a new element is available in the buffer
   * or the specified timeout duration has elapsed. If successful, it pops the front element of the buffer
   * and assigns it to the provided output variable.
   *
   * @tparam T The type of elements stored in the buffer.
   * @tparam Rep An arithmetic type representing the number of ticks for the duration.
   * @tparam Period A std::ratio representing the tick period for the duration.
   *
   * @param out The reference where the popped element will be stored if the operation succeeds.
   * @param timeout The maximum duration to wait for an element to become available.
   *
   * @return `true` if the operation succeeded and an element was popped;
   *         `false` if the timeout was reached, the buffer became uninitialized, or a spurious wakeup occurred.
   */
  template <class Rep, class Period>
  [[nodiscard]] bool wait_pop_for(T& out, const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock<std::mutex> lock(m_waitMutex);

    if (!m_initialized) {
      return false;
    }

    const auto pred = [this]() noexcept {
      // Wake when a buffer becomes non-empty or a buffer is deinitialized.
      return !m_initialized || !empty();
    };

    if (!m_waitCv.wait_for(lock, timeout, pred)) {
      // timeout
      return false;
    }

    if (!m_initialized || empty()) {
      // deinitialized or spurious wakeup
      return false;
    }

    return pop(out);
  }

  /// @brief Notify one waiting consumer (e.g. after a successful push).
  void notifyOne() noexcept {
    m_waitCv.notify_one();
  }

  /// @brief Notify all waiting consumers (e.g. on shutdown / deinit).
  void notifyAll() noexcept {
    m_waitCv.notify_all();
  }

  /**
   * Checks whether the ring buffer is empty.
   *
   * @return True if the buffer is not initialized, or if the head and tail indices are equal,
   *         indicating that the buffer is empty. Returns false otherwise.
   */
  [[nodiscard]] bool empty() const {
    if (!m_initialized)
      return true;

    return m_tail.load(std::memory_order_acquire) == m_head.load(std::memory_order_acquire);
  }

  /**
   * Checks if the ring buffer is full.
   *
   * @return True if the buffer is full, false otherwise.
   *         If the buffer is not initialized, returns false.
   *
   * This function calculates the next position of the head pointer
   * in the circular buffer. If this next position matches the current
   * position of the tail pointer, it indicates the buffer is full.
   */
  [[nodiscard]] bool full() const {
    if (!m_initialized)
      return false;

    std::size_t head = m_head.load(std::memory_order_acquire);
    std::size_t next = (head + 1) & m_mask;
    return next == m_tail.load(std::memory_order_acquire);
  }

  /**
   * Retrieves the total capacity of the ring buffer.
   *
   * The capacity corresponds to the size of the buffer and is determined
   * during initialization using a power-of-two value. If the buffer has not
   * been initialized, this function will return 0.
   *
   * @return The maximum number of elements the buffer can hold.
   */
  [[nodiscard]] std::size_t capacity() const {
    return m_capacity;
  }

 private:
  /**
   * Internal storage for elements. Resized to `m_capacity` during initialization.
   * Elements are stored in contiguous order and accessed by indices wrapped with `m_mask`.
   */
  std::vector<T> m_buffer;

  /**
   * The total number of slots in the ring buffer (set at initialization).
   * Must be a power of two for correct wrapping behavior. Remains constant
   * for the lifetime of the buffer after successful init.
   */
  std::size_t m_capacity{0};

  /**
   * Bitmask derived from `m_capacity` (`m_capacity - 1`) used to wrap indices
   * via bitwise AND instead of modulo. Only valid when the buffer is initialized
   * with a power-of-two capacity.
   */
  std::size_t m_mask{0};

  /**
   * Atomic head index used by the producer. Points to the current writing position;
   * after a successful push, the head is advanced (using the appropriate memory order).
   * Single-producer semantics assumed.
   */
  std::atomic<std::size_t> m_head{0};

  /**
   * Atomic tail index used by the consumer. Points to the current read position;
   * after a successful pop, the tail is advanced (using the appropriate memory order).
   * Single-consumer semantics assumed.
   */
  std::atomic<std::size_t> m_tail{0};

  /**
   * Flag indicating whether `init` has been called successfully. Guard used
   * by public methods to prevent operations on an uninitialized buffer.
   */
  bool m_initialized{false};

  mutable std::mutex m_waitMutex;    ///< A mutex used to synchronize access to shared resources in waiting operations.
  std::condition_variable m_waitCv;  ///< condition
};