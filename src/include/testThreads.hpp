#pragma once

#include <iostream>
#include <memory>
#include <string>

#include "dataTypes.hpp"
#include "taskBase.hpp"

namespace messaging {

class ObserverIncomingThread : public TaskBase {
 public:
  ObserverIncomingThread() : TaskBase() {}

  void setNext(Sender sender) { m_next = std::move(sender); }
  void setDirectToProcessing(Sender sender) { m_directToProcessing = std::move(sender); }

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
      std::this_thread::sleep_for(std::chrono::seconds(2));
      FileEvent event{"file_" + std::to_string(++count) + ".txt", 1024ULL * count};
      std::cout << "[ObserverIncomingThread] Generated event: " << event.fileName << std::endl;
      if (m_next.has_value()) {
        m_next->Send(event);
      }
      if (m_directToProcessing.has_value()) {
        DirectEvent de{count, "Direct message from Observer to Processing " + std::to_string(count)};
        std::cout << "[ObserverIncomingThread] Sending DirectEvent to ProcessingThread (counter=" << count << ")" << std::endl;
        m_directToProcessing->Send(de);
      }
    }
  }

 private:
  std::optional<Sender> m_next;
  std::optional<Sender> m_directToProcessing;
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
        std::cout << "[ApplicationObserverThread] Received: " << fe.fileName << std::endl;
        
        // Mirror back to ObserverIncomingThread
        if (m_mirror.has_value()) {
          MirrorEvent me{++mirrorCounter, "Mirrored feedback for " + fe.fileName};
          std::cout << "[ApplicationObserverThread] Mirroring back to ObserverIncomingThread (counter=" << mirrorCounter << ")" << std::endl;
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
        std::cout << "[ProcessingThread] Received: " << ae.eventDescription << std::endl;
        ProcessedData pd{"DATA-" + std::to_string(ae.originalFile.fileSize), 42, ae};
        if (m_next.has_value()) {
          m_next->Send(pd);
        }
      } else if (auto directWrapped = std::dynamic_pointer_cast<MessageWrapper<DirectEvent>>(msg)) {
        DirectEvent& de = directWrapped->contents_;
        std::cout << "[ProcessingThread] Received DirectEvent: " << de.info 
                  << " (counter=" << de.counter << ")" << std::endl;
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
        std::cout << "[TransformationThread] Received data ID: " << pd.dataId << std::endl;
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
        std::cout << "[ResultThread] Received: " << td.transformationType << " with value " << td.value << std::endl;
        FinalResult fr{true, "Chain completed for " + td.originalData.originalAppEvent.originalFile.fileName, td};
        std::cout << "[ResultThread] FINAL RESULT: " << fr.summary << std::endl;
        std::cout << "----------------------------" << std::endl;
      }
    }
  }
};

} // namespace messaging
