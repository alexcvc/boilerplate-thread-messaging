#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <mutex>
#include "dataTypes.hpp"
#include "taskBase.hpp"
#include "threading/spScRingBuffer.hpp"
#include "memoryUtils.hpp"

namespace messaging {

/**
 * @brief Thread-safe print to std::cout using the shared mutex from utils.
 */
template<typename... Args>
void safe_print(Args&&... args) {
    std::lock_guard<std::mutex> lock(utils::getLogMutex());
    (std::cout << ... << args) << std::endl;
}

class ObserverIncomingThread : public TaskBase {
 public:
  ObserverIncomingThread() : TaskBase() {
    [[maybe_unused]] bool ok = m_cmdBuffer.init(8);
  }

  void setNext(Sender sender) { m_next = std::move(sender); }
  void setDirectToProcessing(Sender sender) { m_directToProcessing = std::move(sender); }

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
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>& msg) override {
    if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<MirrorEvent>>(msg)) {
      MirrorEvent& me = wrapped->contents_;
      std::cout << "[ObserverIncomingThread] Received MirrorEvent: " << me.message
                << " (counter=" << me.counter << ")" << std::endl;
    }
  }

  bool isReadyToStart() noexcept override { return true; }

  void run(std::stop_token stopToken) override {
    int count = 0;
    while (!stopToken.stop_requested()) {
      // Block for the current interval, firing immediately if a command arrives.
      ObserverCommand cmd;
      if (m_cmdBuffer.wait_pop_for(cmd, m_interval)) {
        applyCommand(cmd);
      }

      // Auto-exit stress mode after 20 s → return to 2 s normal
      if (m_mode == ObserverMode::Stress &&
          std::chrono::steady_clock::now() >= m_stressUntil) {
        m_mode = ObserverMode::Normal;
        m_interval = std::chrono::milliseconds(2000);
        utils::printMemoryUsage("After Stress (Auto)");
        safe_print("[ObserverIncomingThread] Stress mode ended, reverting to normal (2 s).");
      }

      if (std::chrono::steady_clock::now() < m_pauseUntil) {
        safe_print("[ObserverIncomingThread] Observing paused, skipping event.");
        continue;
      }

      FileEvent event{"file_" + std::to_string(++count) + ".txt", 1024ULL * count};
      safe_print("[ObserverIncomingThread] Generated event: ", event.fileName,
                " [", modeLabel(), " / ", m_interval.count(), " ms]");
      if (m_next.has_value()) {
        m_next->Send(event);
      }
      if (m_directToProcessing.has_value()) {
        DirectEvent de{count, "Direct message from Observer to Processing " + std::to_string(count)};
        safe_print("[ObserverIncomingThread] Sending DirectEvent to ProcessingThread (counter=", count, ")");
        m_directToProcessing->Send(de);
      }
    }
  }

 private:
  enum class ObserverMode { Normal, Stress };

  void applyCommand(const ObserverCommand& cmd) {
    switch (cmd.type) {
      case ObserverCommand::Type::StopObserving:
        m_pauseUntil = std::chrono::steady_clock::now() + std::chrono::seconds(cmd.durationSec);
        safe_print("[ObserverIncomingThread] Command: StopObserving for ", cmd.durationSec, " s.");
        break;
      case ObserverCommand::Type::StartObserving:
        m_pauseUntil = std::chrono::steady_clock::now() + std::chrono::seconds(cmd.durationSec);
        safe_print("[ObserverIncomingThread] Command: StartObserving in ", cmd.durationSec, " s.");
        break;
      case ObserverCommand::Type::StressMode:
        m_mode = ObserverMode::Stress;
        m_interval = std::chrono::milliseconds(100);
        m_stressUntil = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        safe_print("[ObserverIncomingThread] Stress mode activated: 100 ms interval for 20 s.");
        break;
      case ObserverCommand::Type::SetNormalMode:
        m_mode = ObserverMode::Normal;
        m_interval = std::chrono::milliseconds(1000);
        safe_print("[ObserverIncomingThread] Normal mode set: 1 s interval.");
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
  void setNext(Sender sender) { m_next = std::move(sender); }
  void setMirror(Sender sender) { m_mirror = std::move(sender); }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {
    wakeUp();
  }

  bool isReadyToStart() noexcept override { return true; }

  void run(std::stop_token stopToken) override {
    int mirrorCounter = 0;
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<FileEvent>>(msg)) {
        FileEvent& fe = wrapped->contents_;
        safe_print("[ApplicationObserverThread] Received: ", fe.fileName);
        
        // Mirror back to ObserverIncomingThread
        if (m_mirror.has_value()) {
          MirrorEvent me{++mirrorCounter, "Mirrored feedback for " + fe.fileName};
          safe_print("[ApplicationObserverThread] Mirroring back to ObserverIncomingThread (counter=", mirrorCounter, ")");
          m_mirror->Send(me);
        }

        AppEvent ae{"App1", "Processing file " + fe.fileName, fe};
        if (m_next.has_value()) {
          m_next->Send(ae);
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
  void setNext(Sender sender) { m_next = std::move(sender); }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {
    wakeUp();
  }

  bool isReadyToStart() noexcept override { return true; }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<AppEvent>>(msg)) {
        AppEvent& ae = wrapped->contents_;
        safe_print("[ProcessingThread] Received: ", ae.eventDescription);
        ProcessedData pd{"DATA-" + std::to_string(ae.originalFile.fileSize), 42, ae};
        if (m_next.has_value()) {
          m_next->Send(pd);
        }
      } else if (auto directWrapped = std::dynamic_pointer_cast<MessageWrapper<DirectEvent>>(msg)) {
        DirectEvent& de = directWrapped->contents_;
        safe_print("[ProcessingThread] Received DirectEvent: ", de.info, 
                  " (counter=", de.counter, ")");
      }
    }
  }

 private:
  std::optional<Sender> m_next;
};

class TransformationThread : public TaskBase {
 public:
  TransformationThread() : TaskBase() {}
  void setNext(Sender sender) { m_next = std::move(sender); }

 protected:
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {
    wakeUp();
  }

  bool isReadyToStart() noexcept override { return true; }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<ProcessedData>>(msg)) {
        ProcessedData& pd = wrapped->contents_;
        safe_print("[TransformationThread] Received data ID: ", pd.dataId);
        TransformedData td{"MathTransform", static_cast<double>(pd.processingResult) * 1.5, pd};
        if (m_next.has_value()) {
          m_next->Send(td);
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
  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {
    wakeUp();
  }

  bool isReadyToStart() noexcept override { return true; }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (auto wrapped = std::dynamic_pointer_cast<MessageWrapper<TransformedData>>(msg)) {
        TransformedData& td = wrapped->contents_;
        safe_print("[ResultThread] Received: ", td.transformationType, " with value ", td.value);
        FinalResult fr{true, "Chain completed for " + td.originalData.originalAppEvent.originalFile.fileName, td};
        safe_print("[ResultThread] FINAL RESULT: ", fr.summary);
        safe_print("----------------------------");
      }
    }
  }
};

} // namespace messaging
