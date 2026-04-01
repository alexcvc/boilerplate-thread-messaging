#include "GooseWorker.hpp"

#include <algorithm>

#include "ApplicationWorker.hpp"
#include "DataTypes.hpp"
#include "GooseContext.hpp"
#include "TimeUtilities.hpp"
#include "goose_encode.h"
#include "spdlog/spdlog.h"

static const std::string kThreadName = "GOOSE:";

bool GooseWorker::isReadyToStart() noexcept {
  if (m_waitIntervalMs == std::chrono::milliseconds::zero()) {
    spdlog::warn("{} wait interval is zero, skipping start", kThreadName);
    return false;
  }
  return true;
}

void GooseWorker::run(const compat::stop_token& stopToken) {
  auto onStop = [&]() {
    m_waitCv.notify_all();
  };
  // set wakeup in case of a requested stop
  [[maybe_unused]] compat::stop_callback<decltype(onStop)> stopCb(stopToken, onStop);

  spdlog::info("{} started", kThreadName);

  while (true) {
    {
      std::unique_lock lock(m_waitMutex);
      m_waitCv.wait_for(lock, m_waitIntervalMs, [&]() {
        return stopToken.stop_requested() || !messageQueue().empty();
      });
    }

    if (stopToken.stop_requested()) {
      break;
    }

    if (messageQueue().empty()) {
      spdlog::trace("{} no messages in queue", kThreadName);
    } else {
      while (!messageQueue().empty() && !stopToken.stop_requested()) {
        // process all available items in the queue
        auto msg = messageQueue().try_pop();  // shared_ptr<MessageBase>

        if (!msg) {
          spdlog::warn("{} got empty message pointer", kThreadName);
        } else {
          // Check if this message contains a SlaveProcessImage
          if (auto slaveMsg = std::dynamic_pointer_cast<messaging::MessageWrapper<SlaveProcessImage>>(msg)) {
            // Adjust accessor according to MessageWrapper API
            processSlavePi(slaveMsg->contents_);
          } else if (auto slaveMsg = std::dynamic_pointer_cast<messaging::MessageWrapper<events::LogBookEvent>>(msg)) {
            // Adjust accessor according to MessageWrapper API
            const events::LogBookEvent& event = slaveMsg->contents_;
            if (!processEvent(event)) {
              spdlog::warn("{} failed to process LogBook event id {} array index {} time {}", kThreadName,
                           event.eventId, event.arrayIndex1Based, event.timestampStr);
            } else {
              spdlog::trace("{} processed LogBook event id {} array index {} time {}", kThreadName, event.eventId,
                            event.arrayIndex1Based, event.timestampStr);
            }
          } else {
            spdlog::warn("{} received unexpected message type", kThreadName);
          }
        }
      }
    }
  }
  messageQueue().clear();
  spdlog::info("{} stopped", kThreadName);
}

void GooseWorker::onPostMessageReceived(const std::type_info& type,
                                        const std::shared_ptr<messaging::MessageBase>& message) {
  wakeUp();
}

void GooseWorker::processSlavePi(const SlaveProcessImage& value) {
  bool hasChanges = false;

  if (std::memcmp(&value.slavePi, &m_pi, sizeof(slave_pi)) != 0) {
    std::memcpy(&m_pi, &value.slavePi, sizeof(slave_pi));
    hasChanges = true;
  }

  if (m_quality != value.quality) {
    m_quality = value.quality;
    hasChanges = true;
  }

  if (hasChanges) {
    CheckDataAndSendGoose(value.lastUpdate);
  }
}

int32_T* GooseWorker::getTargetPointer(uint32_t eventId, int index) {
  const auto isOneOf = [eventId](uint32_t a, uint32_t b) {
    return eventId == a || eventId == b;
  };

  if (isOneOf(cas1_LOG_BE_coming, cas1_LOG_BE_going))
    return &m_pi.vBE[index];
  if (isOneOf(cas1_LOG_BEF_coming, cas1_LOG_BEF_going))
    return &m_pi.vBEF[index];
  if (isOneOf(cas1_LOG_BA_coming, cas1_LOG_BA_going))
    return &m_pi.vBA[index];
  if (isOneOf(cas1_LOG_BAF_coming, cas1_LOG_BAF_going))
    return &m_pi.vBAF[index];

  return nullptr;
}

void GooseWorker::updateQuality(int32_T* target, int32_T newValue) {
  if (target == &m_pi.vBAF[beh::BAF_ERROR_INDEX]) {
    m_quality.clear(newValue != 0 ? iec61850::Validity::Invalid : iec61850::Validity::Good);
  }
}

bool GooseWorker::processEvent(const events::LogBookEvent& event) {
  constexpr int32_T kComingValue = 1;
  constexpr int32_T kGoingValue = 0;

  const int index = event.arrayIndex1Based - 1;

  auto mappingIt =
      std::find_if(events::kMappings.begin(), events::kMappings.end(), [&event](const events::EventDesc& d) {
        return d.eventId == event.eventId;
      });

  if (mappingIt == events::kMappings.end()) {
    if (event.eventId == cas1_LOG_QU2_start)
      return true;
    spdlog::warn("eventId={}[{}] ts={} : unexpected event", event.eventId, index, event.timestampStr);
    return false;
  }

  const auto& mapping = *mappingIt;
  if (index < 0 || index >= static_cast<int>(mapping.totalIndices)) {
    spdlog::warn("eventId={}[{}] ts={} : index out of range (max {})", event.eventId, index, event.timestampStr,
                 mapping.totalIndices);
    return false;
  }

  auto* target = getTargetPointer(mapping.eventId, index);
  const int32_T newValue = mapping.isComing ? kComingValue : kGoingValue;

  if (target && *target != newValue) {
    *target = newValue;
    updateQuality(target, newValue);
    const auto ts = time_utils::EventTimestampToTimePoint(event.timestampStr);
    CheckDataAndSendGoose(ts);
  }

  return true;
}

void GooseWorker::CheckDataAndSendGoose(const std::chrono::system_clock::time_point& eventTimeStamp) {
  UtcTime utcTs = time_utils::TimePointToUtcTime(eventTimeStamp);

  // goose code
  auto pPublisher = app::ApplicationWorker::instance().gooseAppContext().publishersList;
  while (pPublisher) {
    // update data
    goo_updateGooseData(pPublisher, &m_pi, sizeof(slave_pi), m_quality.getValue(), utcTs);
    if (pPublisher->noChanges > 0) {
      // found changes
      goo_sendGooseData(pPublisher, &utcTs);
      pPublisher->noChanges = 0;
    }
    // next
    pPublisher = pPublisher->next;
  }
}
