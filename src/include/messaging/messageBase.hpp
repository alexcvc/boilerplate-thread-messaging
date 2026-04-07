#pragma once

#include <utility>

namespace messaging {

/**
 * @defgroup messaging Messaging framework
 * @brief Lightweight, thread-safe intra-process messaging utilities.
 *
 * Components typically derive from `Receiver` to own an inbound
 * `MessageQueue` and expose a `Sender` (via `Receiver::makeSender`) so other
 * components can post strongly-typed payloads. Payloads are type-erased inside
 * the queue using `MessageBase`/`MessageWrapper<T>` and restored by
 * `dynamic_pointer_cast` on the consumer side.
 */

/**
 * @file
 * @ingroup messaging
 * @brief Base types for all messages passed through the messaging system.
 *
 * This is a non-owning, polymorphic base that allows storing heterogeneous
 * message payloads in a single queue via type-erasure. Concrete payloads are
 * wrapped by the templated `MessageWrapper<T>`.
 *
 * Typical usage:
 * - A `Sender` pushes a value (e.g. a struct) to a `MessageQueue`.
 * - The queue stores it as `std::shared_ptr<MessageBase>`.
 * - A `Receiver` pops a `std::shared_ptr<MessageBase>` and downcasts to
 *   `MessageWrapper<T>` if it expects payload of type `T`.
 */
struct MessageBase {
  virtual ~MessageBase() = default;
};

/**
 * @brief Wrapper that type-erases a concrete payload `T` behind `MessageBase`.
 *
 * \tparam T The type of the message to wrap.
 *
 * Example:
 * ```cpp
 * struct Tick { int value; };
 * messaging::MessageQueue q;
 * q.push(Tick{42});
 * auto base = q.wait();
 * if (auto tick = std::dynamic_pointer_cast<messaging::MessageWrapper<Tick>>(base)) {
 *   // Access the payload via `contents_`.
 *   int v = tick->contents_.value; // 42
 * }
 * ```
 */
template <typename T>
struct MessageWrapper : MessageBase {
  using value_type = T;

  value_type contents_;  ///< The actual message contents.

  /**
   * @brief Constructor to initialize the wrapped message.
   * @param contents The message contents.
   *
   * Perfect-forwards the provided payload into the wrapper. The payload is
   * copied or moved depending on the argument category.
   */
  template <typename U>
  explicit MessageWrapper(U&& contents) : contents_(std::forward<U>(contents)) {}
};

}  // namespace messaging