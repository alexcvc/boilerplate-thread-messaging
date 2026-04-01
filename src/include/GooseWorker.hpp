#pragma once

#include <chrono>

#include "DataTypes.hpp"
#include "Receiver.hpp"
#include "Sender.hpp"
#include "WorkerBase.hpp"
#include "goose_com_t.h"
#include "slaves.h"

// Worker that publishes GOOSE messages based on new SummatorState values.
class GooseWorker : public WorkerBase, public messaging::Receiver {
 public:
  static GooseWorker& instance() {
    static GooseWorker s_instance;
    return s_instance;
  }

  [[nodiscard]] bool isReadyToStart() noexcept override;

  [[nodiscard]] std::chrono::milliseconds WaitIntervalMs() const {
    return m_waitIntervalMs;
  }

  void setWaitIntervalMs(const std::chrono::milliseconds& pollIntervalMs) {
    m_waitIntervalMs = pollIntervalMs;
  }

  /**
   * @brief Provides access to the slave process interface.
   * @return A constant reference to the `slave_pi` instance representing the slave process interface.
   */
  [[nodiscard]] const slave_pi& slaveProcessImage() const {
    return m_pi;
  }

  [[nodiscard]] slave_pi& slaveProcessImage() {
    return m_pi;
  }

 private:
  GooseWorker() = default;
  ~GooseWorker() override = default;

  GooseWorker(const GooseWorker&) = delete;
  GooseWorker& operator=(const GooseWorker&) = delete;
  GooseWorker(GooseWorker&&) = delete;
  GooseWorker& operator=(GooseWorker&&) = delete;

  void processSlavePi(const SlaveProcessImage& value);
  int32_T* getTargetPointer(uint32_t eventId, int index);
  void updateQuality(int32_T* target, int32_T newValue);
  void CheckDataAndSendGoose(const std::chrono::system_clock::time_point& eventTimeStamp);
  bool processEvent(const events::LogBookEvent& event);

  // Main thread loop
  void run(const compat::stop_token& stopToken) override;
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<messaging::MessageBase>&) override;

  slave_pi m_pi{};                                      ///< process image
  std::chrono::milliseconds m_waitIntervalMs = 1000ms;  ///< wait interval
  iec61850::Quality m_quality;                          ///< quality of data
};