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

#include "GooseContext.hpp"

#include <chrono>

#include "ApplicationWorker.hpp"
#include "ReadXmlConfig.hpp"
#include "goose_com_t.h"
#include "goose_core.h"
#include "goose_decode.h"
#include "goose_encode.h"
#include "spdlog/spdlog.h"

using namespace std::chrono_literals;

const std::string kThreadName = "Worker Goose:";
const std::chrono::milliseconds MIN_DELAY_TO_CHECK = 200ms;

// -----------------------------------------------------------------------------
// Subscriber timer helpers based on MONOTONIC clock (Time_getTimeStampInMs)
// We use GooseSubscriber::timer as an opaque storage for a monotonic timestamp
// in milliseconds (stored in timer.t_msDay). timer.t_day1984 is kept at 0
// for these helpers and is not used for calendar conversions here.
// -----------------------------------------------------------------------------
static Time_msTime getSubscriberTimerMs(const GooseSubscriber* subscriber) noexcept {
  if (subscriber == nullptr) {
    return 0u;
  }
  return static_cast<Time_msTime>(subscriber->timer.t_msDay);
}

static void setSubscriberTimerNowMonotonic(GooseSubscriber* subscriber) noexcept {
  if (subscriber == nullptr) {
    return;
  }
  const Time_msTime nowMs = Time_getTimeStampInMs();  // CLOCK_MONOTONIC
  subscriber->timer.t_msDay = static_cast<long>(nowMs);
  subscriber->timer.t_day1984 = 0L;
}

static bool isSubscriberTimerNullMonotonic(const GooseSubscriber* subscriber) noexcept {
  if (subscriber == nullptr) {
    return true;
  }
  return (subscriber->timer.t_msDay == 0L && subscriber->timer.t_day1984 == 0L);
}

static bool isSubscriberTimedOutMonotonic(const GooseSubscriber* subscriber, Time_intervalInMs timeoutMs) noexcept {
  if (subscriber == nullptr) {
    return false;
  }
  const Time_msTime startMs = getSubscriberTimerMs(subscriber);
  if (startMs == 0u) {
    // Never started -> not yet timed out
    return false;
  }
  return Time_isTimedOutInMs(startMs, timeoutMs);
}

/**
 * @brief Processes the GOOSE service by managing subscriber status, retransmission,
 *        and timeout handling.
 *
 * @param param Pointer to the GooseAppContext structure, which contains the
 *        application context, GOOSE context, subscriber list, and other related data.
 * @return The calculated service interval (in milliseconds) as a 32-bit unsigned integer.
 */
uint32_t GooseServiceFunc(void* param) {
  GooseAppContext* ctx = static_cast<GooseAppContext*>(param);
  auto serviceInterval = std::chrono::milliseconds(1);

  SEMAPHORE_TAKE(ctx->gooseContext->sem);

  serviceInterval = std::chrono::milliseconds(goose_retransmitFunc(ctx->gooseContext));

  SEMAPHORE_RELEASE(ctx->gooseContext->sem);

  // if the delay is greater than
  if (serviceInterval > MIN_DELAY_TO_CHECK) {
    ForceMethod forceMethod = HowToForceSubscribeService();

    // helper to apply an operation to all subscribers in the list
    auto forEachSubscriber = [head = ctx->subscriberList](auto&& fn) {
      auto* subscriber = head;
      while (subscriber != nullptr) {
        fn(subscriber);
        subscriber = subscriber->next;
      }
    };

    // test and handle all outside event/task
    if (HasFlag(forceMethod, ForceMethod::FORCE_SEND_STATUS)) {
      spdlog::debug("{} force send the states of subscriber", kThreadName);
      forEachSubscriber([](auto* subscriber) {
        goi_setSubscriberAlarm(subscriber, !subscriber->isOnline);
      });
    }

    if (HasFlag(forceMethod, ForceMethod::FORCE_COMMANDS)) {
      /* send commands */
      spdlog::trace("{} force send the subscriber commands", kThreadName);
      forEachSubscriber([](auto* subscriber) {
        goi_sendForceCmdDataSet(subscriber);
      });
    }

    /*
     * test "silent"-publisher and maybe we have to send timeout alarm
     * NOTE: timeout is now based on MONOTONIC time via Time_getTimeStampInMs /
     *       Time_isTimedOutInMs, but storage is still GooseSubscriber::timer.
     */
    constexpr int kAlarmClearedFlag = 1;
    constexpr int kAlarmSetFlag = 2;

    auto handleSubscriberTimeout = [](GooseSubscriber* subscriber) {
      const bool timerIsNull = isSubscriberTimerNullMonotonic(subscriber);
      const bool isTimedOut =
          isSubscriberTimedOutMonotonic(subscriber, static_cast<Time_intervalInMs>(subscriber->timeOut));

      if (!(timerIsNull || isTimedOut)) {
        return;
      }

      const bool shouldRaiseAlarm = !subscriber->isOnline;
      const int expectedFlag = shouldRaiseAlarm ? kAlarmSetFlag : kAlarmClearedFlag;

      if (subscriber->flagAlarm != expectedFlag) {
        goi_setSubscriberAlarm(subscriber, shouldRaiseAlarm);
      }

      // restart timer using MONOTONIC clock
      setSubscriberTimerNowMonotonic(subscriber);
    };

    forEachSubscriber(handleSubscriberTimeout);
  }
  return serviceInterval.count();
}

namespace {

constexpr const char* kDefaultAdapterName = "eth0";

bool configureAdapter(GooseAppContext& appContext, app::DaemonConfig& daemonConfig) {
  if (daemonConfig.adapterName.empty()) {
    daemonConfig.adapterName = kDefaultAdapterName;
    spdlog::warn("{} Using default adapter name: {}", kThreadName, daemonConfig.adapterName);
  }

  auto* gooseContext = appContext.gooseContext;
  if (gooseContext == nullptr) {
    spdlog::error("{} GOOSE context is null while configuring adapter", kThreadName);
    return false;
  }

  strncpy(gooseContext->adapter_name, daemonConfig.adapterName.c_str(), sizeof(gooseContext->adapter_name) - 1);
  gooseContext->adapter_name[sizeof(gooseContext->adapter_name) - 1] = '\0';

  spdlog::info("{} Using adapter name: {}", kThreadName, gooseContext->adapter_name);
  return true;
}

void setupPublishers(GooseAppContext& appContext) {
  auto* publisher = appContext.publishersList;

  while (publisher != nullptr) {
    uint8_t buffer[GOOSE_MAX_DATA_LENGTH] = {};
    int memberCount = 0;
    int offset = 0;
    int length = 0;

    publisher->userData = appContext.user_data;
    publisher->dataLen = appContext.user_len;

    if (!goo_encodeDataSet(publisher, buffer, sizeof(buffer), &memberCount, &offset, &length)) {
      spdlog::warn("Encode of cbRef={} dataset {} failed", publisher->gse.cbRef, publisher->dataset->name);
    } else {
      const auto createResult = goose_createPublisher(
          appContext.gooseContext, publisher->gse.id, publisher->gse.cbRef, publisher->dataset->name,
          &publisher->gse.addr, &publisher->timingStrategy, publisher->gse.confRev, memberCount, FALSE, &buffer[offset],
          static_cast<ber_length_t>(length), static_cast<void*>(publisher), publisher->gse.appId, publisher->gse.vlanId,
          publisher->gse.vlanPriority);

      if (createResult != GOOSE_SUCCESS) {
        spdlog::warn("Create publisher goId={} cbRef={} failed", publisher->gse.id, publisher->gse.cbRef);
      } else {
        spdlog::info("Created Publisher Id={} CbRef={} DataSet={} Desc={}", publisher->gse.id, publisher->gse.cbRef,
                     publisher->dataset->name, publisher->desc ? publisher->desc : "");
        spdlog::info("    MAC={} AppId={} VLAN Prio={} ID={}",
                     goose::ReadXmlConfig::GetMacAddressString(publisher->gse.addr), publisher->gse.appId,
                     publisher->gse.vlanPriority, publisher->gse.vlanId);
      }
    }

    publisher = publisher->next;
  }
}

void setupSubscribers(GooseAppContext& appContext) {
  appContext.gooseContext->notSupportedResFcn = goi_resNotSupportedCb;
  appContext.gooseContext->notSupportedResParam = nullptr;

  auto* subscriber = appContext.subscriberList;

  while (subscriber != nullptr) {
    if (!goose_createSubscriber(appContext.gooseContext, subscriber->gse.id, subscriber->gse.cbRef,
                                &subscriber->gse.addr, subscriber->gse.appId, subscriber, goi_incommingCb,
                                subscriber)) {
      spdlog::warn("Create subscriber goId={} cbRef={} failed", subscriber->gse.id, subscriber->gse.cbRef);
      subscriber = subscriber->next;
      continue;
    }

    goose_setInitialWaitSubscriber(appContext.gooseContext, subscriber, subscriber->timeOut, subscriber->toleranceMs);

    spdlog::info("Create subscriber goId={} cbRef={} succeeded", subscriber->gse.id, subscriber->gse.cbRef);

    subscriber = subscriber->next;
  }
}

}  // namespace

bool PreConfigGoose(GooseAppContext& appContext) noexcept {
  auto& daemonConfig = app::ApplicationWorker::instance().daemonConfig();

  // reset and initialize the application context
  ResetGooseAppContext(&appContext);

  goose::ReadXmlConfig reader;
  spdlog::info("{} Using GOOSE configuration file: {}", kThreadName, daemonConfig.pathConfigFile);

  if (!reader.readCfg(daemonConfig.pathConfigFile, appContext)) {
    spdlog::error("{} Failed to read GOOSE configuration {}, skipping start", kThreadName, daemonConfig.pathConfigFile);
    return false;
  }

  // create new GOOSE context
  appContext.gooseContext = new goose_context_t();
  if (appContext.gooseContext == nullptr) {
    spdlog::error("{} Unable to malloc Goose context", kThreadName);
    ResetGooseAppContext(&appContext);
    return false;
  }

  if (!configureAdapter(appContext, daemonConfig)) {
    ResetGooseAppContext(&appContext);
    return false;
  }

  return true;
}

bool StartGoose(GooseAppContext& appContext) noexcept {
  // initialize GOOSE core
  goose_initializeCore(appContext.gooseContext);

  // create task parameters
  appContext.pEventTaskParams = EventTaskParamsCreate();
  if (appContext.pEventTaskParams == nullptr) {
    spdlog::error("{} Unable to create Data Link Task Parameters", kThreadName);
    ResetGooseAppContext(&appContext);
    return false;
  }

  if (!DataLinkTasksStart(appContext.pEventTaskParams, GooseServiceFunc, &appContext,
                          appContext.gooseContext->link_context, appContext.gooseContext->sem,
                          appContext.gooseContext->event, GOOSE_THREAD_INTERVAL)) {
    spdlog::error("{} Failed to start Data Link Layer", kThreadName);
    return false;
  }

  // Set up handler functions for responding to queries
  appContext.gooseContext->getElementNumReqFcn = goo_getElemReqCb;
  appContext.gooseContext->getElementNumReqParam = &appContext;
  appContext.gooseContext->getReferenceReqFcn = goo_getReferReqCb;
  appContext.gooseContext->getReferenceReqParam = &appContext;

  setupPublishers(appContext);
  setupSubscribers(appContext);

  return true;
}

void ShutdownGoose(GooseAppContext& appContext) noexcept {
  spdlog::info("{} GOOSE shutdown", kThreadName);

  // shutdown and free all application objects
  ResetGooseAppContext(&appContext);
}

ForceMethod HowToForceSubscribeService() noexcept {
  return ForceMethod::NONE;
}
