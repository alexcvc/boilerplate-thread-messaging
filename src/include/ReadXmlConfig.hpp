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

#include <string>

#include "goose_com_t.h"
#include "goose_publish.h"
#include "logging.h"
#include "pugixml.hpp"

namespace goose {

//----------------------------------------------------------------------------
// Public defines and macros
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Public typedefs, structs, enums, unions and variables
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Public Function Prototypes
//----------------------------------------------------------------------------

class ReadXmlConfig {
 public:
  ReadXmlConfig() = default;
  ReadXmlConfig(const ReadXmlConfig&) = delete;
  ReadXmlConfig& operator=(const ReadXmlConfig&) = delete;
  ~ReadXmlConfig() = default;

  /// Read run-time logging
  [[nodiscard]] bool readCfg(const std::string& fileName, LogConfigParam& logConfig);

  /// reads GOOSE configuration
  [[nodiscard]] bool readCfg(const std::string& fileName, GooseAppContext& context);

  /// reads GOOSE publisher
  [[nodiscard]] bool readCfg(const std::string& fileName, GoosePublisher& publisher);

  /// reads GSE
  [[nodiscard]] static bool readGooseGseConfig(pugi::xml_node& node, link_macAddress_t& macaddr, uint8_t& priority,
                                               unsigned int& vlanId, unsigned int& appId);

  static std::string GetDataTypeString(DataType type);
  static std::string GetGooseQualityUseKindString(GooseQualityUseKind q);
  static std::string GetGooseFcdaKindString(GooseFcdaKind type);
  static std::string GetMacAddressString(const link_macAddress_t& addr);

 private:
  bool readCfg(pugi::xml_node& node, LogConfigParam& logConfig);
  void readPublisherConfig(pugi::xml_node& node, GooseAppContext& context);
  uint8_t resolveLogMask(const char* mask);

  pugi::xml_document m_xmldoc;
};
}  // namespace goose
