/* SPDX-License-Identifier: MIT */
//
// Copyright (c) 2024 Alexander Sacharov <a.sacharov@gmx.de>
//               All rights reserved.
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

/** @file
 *  @brief Implementation of the TaskManager class.
 *
 *  This file contains the implementation of the TaskManager class.
 *  The TaskManager class is responsible for managing a set of threads
 *  that execute a given function.
 *  Example usage:
 *
 *  TaskManager controller;
 *
 *   controller.StartThread([](const TaskManager& controller, std::stop_token stopToken) {
 *   while (!stopToken.stop_requested()) {
 *     std::this_thread::sleep_for(std::chrono::milliseconds(10));
 *     spdlog::info("Working in thread...");
 *   }
 *   spdlog::warn("Thread is stopping.");
 * });
 *
 */

#pragma once

#include <map>
#include <stop_token>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace cppsl::threading {
class TaskManager {
  std::atomic_uint32_t m_taskId{0};
  std::map<uint32_t, std::jthread> m_threads;
  std::vector<std::stop_source> m_stopSources;

 public:
  /** Default constructor. */
  TaskManager() = default;

  /** Destructor.
   *
   *  Stops all threads that are currently running.
   */
  ~TaskManager() {
    StopAllTasks();
  }

  /** Starts a new thread.
   *
   *  This function starts a new thread that will execute the given function.
   *
   *  @param function The function that the thread will execute.
   *  @param index The index of the thread.
   */
  template <typename Function, typename... Args>
  void StartTask(Function&& func, Args&&... args) {
    auto id = m_taskId++;
    std::stop_source stopSource;
    m_threads.emplace(std::piecewise_construct, std::forward_as_tuple(id),
                      std::forward_as_tuple(
                          [&func, &args...](const TaskManager& manager, std::stop_token stopToken) {
                            func(manager, stopToken, args...);
                          },
                          std::cref(*this), stopSource.get_token()));
    m_stopSources.emplace_back(std::move(stopSource));
  }

  /** Stops all threads.
   *
   *  This function stops all threads that are currently running.
   */
  [[maybe_unused]] void StopAllTasks() {
    for (auto& stopSource : m_stopSources) {
      stopSource.request_stop();
    }
    for (auto& [id, thread] : m_threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    m_threads.clear();
    m_stopSources.clear();
  }

  /** Stops the thread with the given ID.
   *
   *  @param id The ID of the thread to stop.
   *  @return true if the thread was stopped, false if the ID was invalid.
   */
  bool StopTask(uint32_t id) {
    if (id >= m_taskId) {
      return false;
    }

    m_stopSources[id].request_stop();
    m_stopSources.erase(m_stopSources.begin() + id);

    if (auto it = m_threads.find(id); it != m_threads.end()) {
      it->second.request_stop();
      if (it->second.joinable()) {
        it->second.join();
      }
      m_threads.erase(it);
    }
  }
};
}  // namespace cppsl::threading