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

#include <memory>
#include <typeinfo>

#include "messaging/MessageQueue.hpp"
#include "messaging/Sender.hpp"

namespace messaging {
/**
 * @file
 * @ingroup messaging
 * @brief Represents a receiver endpoint that holds an inbound `MessageQueue`.
 *
 * Typical usage pattern:
 * - Derive your worker/component from `Receiver`.
 * - In the worker loop, call `messageQueue().wait()` / `wait_for(...)` /
 *   `try_pop()` to consume incoming messages and downcast to the expected
 *   `MessageWrapper<T>`.
 * - Expose a `Sender` to other components via `makeSender()` so they can
 *   push messages into your queue.
 *
 * Example:
 * ```cpp
 * struct Tick { int value; };
 *
 * class MyWorker : public messaging::Receiver {
 *   void run() {
 *     while (true) {
 *       auto base = messageQueue().wait();
 *       if (auto tick = std::dynamic_pointer_cast<messaging::MessageWrapper<Tick>>(base)) {
 *         // handle tick->contents_
 *       }
 *     }
 *   }
 * };
 *
 * MyWorker w;
 * auto tx = w.makeSender();
 * tx.Send(Tick{123});
 * ```
 */
class Receiver {
  MessageQueue messageQueue_;  ///< Message queue used for handling messages in the receiver.

 protected:
  /**
   * @brief Access the underlying inbound `MessageQueue`.
   */
  [[nodiscard]] MessageQueue& messageQueue() {
    return messageQueue_;
  }

  /**
   * @brief Generic post-receive notification hook (type-erased).
   *
   * Called by `Sender` right after a message of a particular type has been
   * pushed into this receiver's queue, if the `Sender` was created with
   * notifications enabled. Default implementation is a no-op.
   *
   * Override this in derived receivers to observe or react to posted messages
   * without blocking on the queue.
   */
  virtual void onPostMessageReceived(const std::type_info& /*messageTypeInfo*/,
                                     const std::shared_ptr<MessageBase>& /*message*/) = 0;

  /**
   * @brief Convenience template that downcasts to the typed wrapper and calls the type-erased hook.
   *
   * This helper allows derived classes to call into the base hook with a typed message if needed.
   */
  template <typename MessageType>
  void onPostMessageReceived(const std::shared_ptr<MessageWrapper<MessageType>>& message) {
    onPostMessageReceived(typeid(MessageType), message);
  }

 public:
  Receiver() = default;
  ~Receiver() = default;

  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;
  Receiver(Receiver&&) = delete;
  Receiver& operator=(Receiver&&) = delete;

  /**
   * @brief Creates a `Sender` object for sending messages to the associated message queue.
   *
   * This method provides an interface to create a `Sender` bound to the receiver's `MessageQueue`.
   * If `notifyOnPost` is set to true, the `Sender` will notify the receiver immediately after a
   * message is posted by invoking the `onPostMessageReceived` callback.
   *
   * @param notifyOnPost Determines whether the receiver should be notified when a message is posted.
   *                     If true, a callback is attached to the `Sender` to invoke `onPostMessageReceived`
   *                     in the receiver. Defaults to true.
   * @return A `Sender` object configured with the receiver's `MessageQueue` and, optionally, with
   *         a post-notification callback.
   */
  [[nodiscard]] Sender makeSender(bool notifyOnPost = true) {
    if (!notifyOnPost) {
      return Sender(&messageQueue_);
    }
    auto cb = [this](const std::type_info& ti, const std::shared_ptr<MessageBase>& msg) {
      this->onPostMessageReceived(ti, msg);
    };
    return Sender(&messageQueue_, std::move(cb));
  }
};
}  // namespace messaging