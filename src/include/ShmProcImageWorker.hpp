#pragma once

#include <chrono>

#include "DataTypes.hpp"
#include "Quality.hpp"
#include "WorkerBase.hpp"
#include "slaves.h"

class ShmProcImageWorker final : public WorkerBase {
 public:
  static ShmProcImageWorker& instance() {
    static ShmProcImageWorker s_instance;
    return s_instance;
  }

  [[nodiscard]] bool isReadyToStart() noexcept override;

  [[nodiscard]] std::chrono::milliseconds WaitIntervalMs() const {
    return m_waitIntervalMs;
  }

  void setWaitIntervalMs(const std::chrono::milliseconds& pollIntervalMs) {
    m_waitIntervalMs = pollIntervalMs;
  }

 private:
  /// maximum number of sequential read errors allowed
  constexpr static uint32_t kMaxSequentialReadErrors = 5;

  ShmProcImageWorker() = default;
  ~ShmProcImageWorker() override = default;

  ShmProcImageWorker(const ShmProcImageWorker&) = delete;
  ShmProcImageWorker& operator=(const ShmProcImageWorker&) = delete;
  ShmProcImageWorker(ShmProcImageWorker&&) = delete;
  ShmProcImageWorker& operator=(ShmProcImageWorker&&) = delete;

  // Main thread loop
  void run(const compat::stop_token& stopToken) override;
  void handleWaitNotification();
  void handleTimeoutNotification();

  std::chrono::milliseconds m_waitIntervalMs = 200ms;  ///< waiting interval
  SlaveProcessImage m_slaveState{};                    ///< last copy slave process image
  uint32_t m_readErrorCounter{0};                      ///< read error counter
  bool m_isInitialized{false};                         ///< is initialized
};
