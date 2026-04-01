#include "ShmProcImageWorker.hpp"

#include "GooseWorker.hpp"
#include "spdlog/spdlog.h"

static const std::string kThreadName = "Worker SHM:";

void ShmProcImageWorker::run(const compat::stop_token& stopToken) {
  auto onStop = [&]() {
    m_waitCv.notify_all();
  };
  // set wakeup in case of a requested stop
  [[maybe_unused]] compat::stop_callback<decltype(onStop)> stopCb(stopToken, onStop);

  spdlog::info("{} started", kThreadName);

  // clear initiation
  m_isInitialized = false;

  while (true) {
    if (stopToken.stop_requested()) {
      spdlog::trace("stop {} requested", kThreadName);
      break;
    } else {
      std::unique_lock<std::mutex> lock(m_waitMutex);
      auto result = m_waitCv.wait_for(lock, m_waitIntervalMs);
      if (result == std::cv_status::no_timeout) {
        // worker woken up
        handleWaitNotification();
        spdlog::trace("{} wait notified", kThreadName);
      } else {
        handleTimeoutNotification();
      }
    }
  }

  spdlog::info("{} stopped", kThreadName);
}

void ShmProcImageWorker::handleWaitNotification() {
  // This function is called whenever m_waitCv is notified without timeout.
}

bool ShmProcImageWorker::isReadyToStart() noexcept {
  // read data from SHM
  if (m_waitIntervalMs == std::chrono::milliseconds::zero()) {
    spdlog::warn("{} wait interval is zero, skipping start", kThreadName);
    return false;
  }
  return true;
}
void ShmProcImageWorker::handleTimeoutNotification() {
  slaves slave;

  const int result = update_slave_from_shm(&slave);
  if (result != 0) {
    ++m_readErrorCounter;
    if (m_readErrorCounter >= kMaxSequentialReadErrors) {
      spdlog::error("Failed to update slave from shared memory, error code: {}", result);
      // reset counter
      m_readErrorCounter = 0;
      m_slaveState.quality.clear(iec61850::Validity::Invalid);
      if (m_isInitialized) {
        m_slaveState.quality.setOldData(true);
      }
    } else {
      // skip this read
      return;
    }
  } else {
    m_readErrorCounter = 0;
    m_isInitialized = true;

    if (slave.slave_pi.vBAF[beh::BAF_ERROR_INDEX] != 0) {
      // Error set
      m_slaveState.quality.clear(iec61850::Validity::Invalid);
    } else {
      m_slaveState.quality.clear(iec61850::Validity::Good);
    }
    // data from a process
    m_slaveState.quality.setSource(iec61850::Source::Process);

    // set last time of update
    m_slaveState.lastUpdate = std::chrono::system_clock::now();
    // copy last PI
    memcpy(&m_slaveState.slavePi, &slave.slave_pi, sizeof(slave_pi));
  }

  // prepare process image for sending
  auto postMan = GooseWorker::instance().makeSender(true);

  postMan.Send(m_slaveState);
}