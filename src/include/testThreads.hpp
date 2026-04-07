#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "dataTypes.hpp"
#include "memoryUtils.hpp"
#include "taskBase.hpp"
#include "threading/spScRingBuffer.hpp"

namespace messaging {

/**
 * @brief Thread-safe print to std::cout using the shared mutex from utils.
 */
template <typename... Args>
void safe_print(Args&&... args) {
  std::lock_guard<std::mutex> lock(utils::getLogMutex());
  (std::cout << ... << args) << std::endl;
}

class ObserverIncomingThread : public TaskBase {
 public:
  ObserverIncomingThread() : TaskBase() {
    [[maybe_unused]] bool ok = m_cmdBuffer.init(8);
  }

  void setNext(Sender sender) {
    m_next = std::move(sender);
  }
  void setDirectToProcessing(Sender sender) {
    m_directToProcessing = std::move(sender);
  }

  /**
   * @brief Pushes a command into the ring buffer from ThreadManager (producer side).
   *        Thread-safe: may be called from any single producer thread.
   * @return false if the buffer is full.
   */
  bool sendCommand(const ObserverCommand& cmd) {
    if (!m_cmdBuffer.push(cmd))
      return false;
    m_cmdBuffer.notifyOne();
    return true;
  }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {}

  bool isReadyToStart() noexcept override {
    return true;
  }

  void run(std::stop_token stopToken) override {
    int count = 0;
    while (!stopToken.stop_requested()) {
      // Block for the current interval, firing immediately if a command arrives.
      ObserverCommand cmd;
      if (m_cmdBuffer.wait_pop_for(cmd, m_interval)) {
        applyCommand(cmd);
      }

      // Auto-exit stress mode after 30 s → return to 2 s normal
      if (m_mode == ObserverMode::Stress && std::chrono::steady_clock::now() >= m_stressUntil) {
        m_mode = ObserverMode::Normal;
        m_interval = std::chrono::milliseconds(2000);
        utils::printMemoryUsage("After Stress (Auto)");
        safe_print("[OI] → normal  (2s, stress ended)");
      }

      if (std::chrono::steady_clock::now() < m_pauseUntil) {
        continue;
      }

      FileEvent event{"file_" + std::to_string(++count) + ".txt", 1024ULL * count, std::chrono::steady_clock::now()};
      safe_print(utils::currentTimestamp(), "  [OI] #", count, "  [", modeLabel(), "/", m_interval.count(), "ms]");
      if (m_next.has_value()) {
        m_next->Send(event);
      }
      if (m_directToProcessing.has_value()) {
        DirectEvent de{count};
        m_directToProcessing->Send(de);
      }
    }
  }

 private:
  enum class ObserverMode { Normal, Stress };

  void applyCommand(const ObserverCommand& cmd) {
    switch (cmd.type) {
      case ObserverCommand::Type::StopObserving:
        m_pauseUntil = std::chrono::steady_clock::now() + cmd.durationSec;
        safe_print("[OI] pause ", cmd.durationSec.count(), "s");
        break;
      case ObserverCommand::Type::StartObserving:
        m_pauseUntil = std::chrono::steady_clock::now() + cmd.durationSec;
        safe_print("[OI] resume in ", cmd.durationSec.count(), "s");
        break;
      case ObserverCommand::Type::StressMode:
        m_mode = ObserverMode::Stress;
        m_interval = std::chrono::milliseconds(10);
        m_stressUntil = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        safe_print("[OI] → STRESS  (10ms / 30s)");
        break;
      case ObserverCommand::Type::SetNormalMode:
        m_mode = ObserverMode::Normal;
        m_interval = std::chrono::milliseconds(500);
        safe_print("[OI] → normal  (500ms)");
        break;
    }
  }

  const char* modeLabel() const noexcept {
    return m_mode == ObserverMode::Stress ? "STRESS" : "normal";
  }

  std::optional<Sender> m_next;
  std::optional<Sender> m_directToProcessing;
  SpScRingBuffer<ObserverCommand> m_cmdBuffer;
  std::chrono::steady_clock::time_point m_pauseUntil{};
  std::chrono::steady_clock::time_point m_stressUntil{};
  ObserverMode m_mode{ObserverMode::Normal};
  std::chrono::milliseconds m_interval{2000};
};

class ApplicationObserverThread : public TaskBase {
 public:
  ApplicationObserverThread() : TaskBase() {}
  void setNext(Sender sender) {
    m_next = std::move(sender);
  }
  void setMirror(Sender sender) {
    m_mirror = std::move(sender);
  }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {}

  bool isReadyToStart() noexcept override {
    return true;
  }

  void run(std::stop_token stopToken) override {
    int mirrorCounter = 0;
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<FileEvent>>(msg)) {
        FileEvent& fe = wrapped->contents_;
        if (m_mirror.has_value()) {
          m_mirror->Send(MirrorEvent{++mirrorCounter, fe.fileName});
        }
        if (m_next.has_value()) {
          m_next->Send(AppEvent{"App1", fe.fileName, fe});
        }
      }
    }
  }

 private:
  std::optional<Sender> m_next;
  std::optional<Sender> m_mirror;
};

class ProcessingThread : public TaskBase {
 public:
  ProcessingThread() : TaskBase() {}
  void setNext(Sender sender) {
    m_next = std::move(sender);
  }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {}

  bool isReadyToStart() noexcept override {
    return true;
  }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<AppEvent>>(msg)) {
        AppEvent& ae = wrapped->contents_;
        if (m_next.has_value()) {
          m_next->Send(ProcessedData{"DATA-" + std::to_string(ae.originalFile.fileSize), 42, ae});
        }
      }
      // DirectEvent intentionally consumed without forwarding (bypass path, no output)
    }
  }

 private:
  std::optional<Sender> m_next;
};

class TransformationThread : public TaskBase {
 public:
  TransformationThread() : TaskBase() {}
  void setNext(Sender sender) {
    m_next = std::move(sender);
  }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {}

  bool isReadyToStart() noexcept override {
    return true;
  }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<ProcessedData>>(msg)) {
        ProcessedData& pd = wrapped->contents_;
        if (m_next.has_value()) {
          m_next->Send(TransformedData{"MathTransform", static_cast<double>(pd.processingResult) * 1.5, pd});
        }
      }
    }
  }

 private:
  std::optional<Sender> m_next;
};

class ResultThread : public TaskBase {
 public:
  ResultThread() : TaskBase() {}

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {}

  bool isReadyToStart() noexcept override {
    return true;
  }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<TransformedData>>(msg)) {
        TransformedData& td = wrapped->contents_;
        auto recvTime = std::chrono::steady_clock::now();
        auto chainUs = std::chrono::duration_cast<std::chrono::microseconds>(
                           recvTime - td.originalData.originalAppEvent.originalFile.sendTime)
                           .count();
        safe_print(utils::currentTimestamp(), "  [RT] done: ", td.originalData.originalAppEvent.originalFile.fileName,
                   "  val=", td.value, "  chain=", chainUs, "µs");
      }
    }
  }
};

}  // namespace messaging
