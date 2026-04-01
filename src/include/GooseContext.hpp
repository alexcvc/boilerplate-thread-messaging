/* SPDX-License-Identifier: A-EBERLE */
/****************************************************************************\
**                   _        _____ _               _                      **
**                  / \      | ____| |__   ___ _ __| | ___                 **
**                 / _ \     |  _| | '_ \ / _ \ '__| |/ _ \                **
**                / ___ \ _  | |___| |_) |  __/ |  | |  __/                **
**               /_/   \_(_) |_____|_.__/ \___|_|  |_|\___|                **
**                                                                         **
*****************************************************************************
** Copyright (c) 2010 - 2024 A. Eberle GmbH & Co. KG. All rights reserved. **
\****************************************************************************/

#pragma once
#include <cstdint>

#include "goose_com_t.h"

enum class ForceMethod : uint8_t { NONE, FORCE_SEND_STATUS, FORCE_COMMANDS };

inline ForceMethod operator|(ForceMethod a, ForceMethod b) {
  return static_cast<ForceMethod>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

inline ForceMethod& operator|=(ForceMethod& a, ForceMethod b) {
  a = a | b;
  return a;
}

inline bool HasFlag(ForceMethod value, ForceMethod flag) {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0u;
}

/// service GOOSE function
uint32_t GooseServiceFunc(void* param);

/// Pre-Configure GOOSE
[[nodiscard]] bool PreConfigGoose(GooseAppContext& appContext) noexcept;

/// Starts GOOSE processing
[[nodiscard]] bool StartGoose(GooseAppContext& appContext) noexcept;

/// Shuts down and cleans up GOOSE-related resources.
void ShutdownGoose(GooseAppContext& appContext) noexcept;

/// force subscriber service
[[nodiscard]] ForceMethod HowToForceSubscribeService() noexcept;
