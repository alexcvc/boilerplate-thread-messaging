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
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

//----------------------------------------------------------------------------
// C++17 stop_token + jthread compatibility (subset)
//----------------------------------------------------------------------------
namespace compat {
class stop_token;

class StopState {
 public:
  void request_stop() {
    bool expected = false;
    if (m_stopped.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      std::lock_guard<std::mutex> lg(m_mtx);
      for (auto& cb : m_callbacks) {
        if (cb)
          cb();
      }
    }
  }
  bool stop_requested() const noexcept {
    return m_stopped.load(std::memory_order_acquire);
  }

  void add_callback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_stopped.load(std::memory_order_acquire)) {
      // if already stopped, invoke immediately
      if (cb)
        cb();
      return;
    }
    m_callbacks.push_back(std::move(cb));
  }

 private:
  std::atomic<bool> m_stopped{false};
  std::mutex m_mtx;
  std::vector<std::function<void()>> m_callbacks;
};

class stop_source {
 public:
  stop_source() : m_state(std::make_shared<StopState>()) {}
  void request_stop() {
    m_state->request_stop();
  }
  bool stop_requested() const noexcept {
    return m_state->stop_requested();
  }
  std::shared_ptr<StopState> state() const noexcept {
    return m_state;
  }

 private:
  std::shared_ptr<StopState> m_state;
};

class stop_token {
 public:
  stop_token() = default;
  explicit stop_token(std::shared_ptr<StopState> st) : m_state(std::move(st)) {}
  bool stop_requested() const noexcept {
    return m_state && m_state->stop_requested();
  }
  std::shared_ptr<StopState> state() const noexcept {
    return m_state;
  }

 private:
  std::shared_ptr<StopState> m_state;
};

template <typename Callback>
class stop_callback {
 public:
  stop_callback(const stop_token& tok, Callback cb) : m_cb(std::move(cb)) {
    if (auto st = tok.state()) {
      st->add_callback(m_cb);
    } else {
      // no state -> never stops; do nothing
    }
  }

 private:
  Callback m_cb;
};

class JThread {
 public:
  JThread() = default;

  template <class Fn>
  explicit JThread(Fn&& fn) {
    m_source = stop_source{};
    m_thread = std::thread([fn = std::forward<Fn>(fn), tok = stop_token{m_source.state()}]() mutable {
      fn(tok);
    });
  }

  JThread(JThread&& other) noexcept : m_thread(std::move(other.m_thread)), m_source(std::move(other.m_source)) {}

  JThread& operator=(JThread&& other) noexcept {
    if (this != &other) {
      if (m_thread.joinable()) {
        request_stop();
        m_thread.join();
      }
      m_thread = std::move(other.m_thread);
      m_source = std::move(other.m_source);
    }
    return *this;
  }

  ~JThread() {
    if (m_thread.joinable()) {
      request_stop();
      m_thread.join();
    }
  }

  void request_stop() {
    if (m_source.state())
      m_source.request_stop();
  }

  bool joinable() const noexcept {
    return m_thread.joinable();
  }
  void join() {
    if (m_thread.joinable())
      m_thread.join();
  }
  std::thread::id get_id() const noexcept {
    return m_thread.get_id();
  }

 private:
  std::thread m_thread;
  stop_source m_source;
};
}  // namespace compat
