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

//-----------------------------------------------------------------------------
// includes
//-----------------------------------------------------------------------------
#include "ReadXmlConfig.hpp"

#include <syslog.h>

#include <array>
#include <cassert>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "goose_publish.h"
#include "slaves.h"
#include "spdlog/spdlog.h"
#include "strutil.h"

// Provide a portable replacement for GNU typeof used in this file (C++ only)
#include <sstream>
#include <type_traits>

#include "Daemon.hpp"
#include "goose_emit.h"
#include "goose_sim.h"
#ifndef typeof
#define typeof(x) std::remove_reference<decltype(x)>::type
#endif

// Helper macros to compute runtime offsets when indexing into struct arrays
#ifndef OFFSET_ARRAY
#define OFFSET_ARRAY(struct_t, field, idx) \
  (offsetof(struct_t, field) + (size_t)(((idx) < 0) ? 0 : (idx)) * sizeof(((struct_t*)0)->field[0]))
#endif
#ifndef OFFSET_ARRAY_MEMBER
#define OFFSET_ARRAY_MEMBER(struct_t, arrayfield, idx, member)                                                  \
  (offsetof(struct_t, arrayfield) + (size_t)(((idx) < 0) ? 0 : (idx)) * sizeof(((struct_t*)0)->arrayfield[0]) + \
   offsetof(typeof(((struct_t*)0)->arrayfield[0]), member))
#endif

// Provide std::size compatibility for GCC 7.x
#if defined(__GNUC__) && (__GNUC__ < 8)
namespace compat {
template <typename T, std::size_t N>
constexpr std::size_t size(const T (&)[N]) noexcept {
  return N;
}
}  // namespace compat
#else
namespace compat {
using std::size;
}
#endif

namespace {
// Case-insensitive starts-with to avoid unsafe length mismatches
bool startsWithCaseInsensitive(const char* text, const char* prefix) {
  if (!text || !prefix)
    return false;
  while (*text && *prefix) {
    unsigned char a = static_cast<unsigned char>(*text);
    unsigned char b = static_cast<unsigned char>(*prefix);
    if (std::toupper(a) != std::toupper(b))
      return false;
    ++text;
    ++prefix;
  }
  return *prefix == '\0';
}
}  // namespace

// clang-format off
#define DBG_IF_COND logConfigParams.goosePublish
#define DBG_IF_VERBOSE (logConfigParams.gooseVerbose && DBG_IF_COND)

#define GOO_TIME_DEF_INTERVAL       50
#define GOO_TIME_DEF_MULTIPLAYER    2
#define GOO_TIME_DEF_MAXINTERVAL    4000
// clang-format on

struct mask_t {
  const char* desc;
  int val;
};

const std::array<mask_t, 16> kmasks = {{
    {"EMR", LOG_EMERG},
    {"0", LOG_EMERG},
    {"ALERT", LOG_ALERT},
    {"1", LOG_ALERT},
    {"CRIT", LOG_CRIT},
    {"2", LOG_CRIT},
    {"ERROR", LOG_ERR},
    {"3", LOG_ERR},
    {"WARN", LOG_WARNING},
    {"4", LOG_WARNING},
    {"NOTICE", LOG_NOTICE},
    {"5", LOG_NOTICE},
    {"INFO", LOG_INFO},
    {"6", LOG_INFO},
    {"DEBUG", LOG_DEBUG},
    {"7", LOG_DEBUG},
}};

static const gocfg_translate_t qualityTypeTranslation[] = {
    {"wit", POSQUALITY_WITHOUT}, {"no", POSQUALITY_WITHOUT}, {"aft", POSQUALITY_AFTER}, {"bef", POSQUALITY_BEFORE}};

static const gocfg_translate_t fcdaTypeTranslation[] = {
    {"bool", DA_BOOLEAN},    {"bitstring2", DA_BITSTRING2},
    {"dpos", DA_BITSTRING2}, {"int32u", DA_UNSIGNED},
    {"int32", DA_INTEGER},   {"float", DA_FLOAT},
    {"mag", DA_MAG},         {"sps", DO_SPS},
    {"dps", DO_DPS},         {"ins", DO_INS},
    {"mv", DO_MV},
};

/**
 * @brief   :
 **/
enum parKeyWord_e {
  P_null_key = 0,
  P_devid,
  P_procimage,
  // structure slave_pi
  P_vBE_32,
  P_vBEF_32,
  P_vBAF_128,
  P_vBA_8,
  P_y_BE,
  P_y_BEF,
  P_y_BAF_8,
  P_y_BA,
  P_y_LED,
  P_aUIg_8,
  P_wUIg_8,
  P_dwUIg_4,
  P_val_UI_Up_4,
  P_val_UI_Ip_4,
  P_val_UI_Us_4,
  P_val_UI_Is_4,
  P_val_UI_U12p,
  P_val_UI_U12p_r,
  P_val_UI_U12p_i,
  P_val_UI_wU12,
  P_val_UI_U23p,
  P_val_UI_U23p_r,
  P_val_UI_U23p_i,
  P_val_UI_wU23,
  P_val_UI_U31p,
  P_val_UI_U31p_r,
  P_val_UI_U31p_i,
  P_val_UI_wU31,
  P_val_UI_dw_4,
  P_val_UI_P_4,
  P_val_UI_Q_4,
  P_val_UI_S_4,
  P_val_UI_Pg,
  P_val_UI_Qg,
  P_val_UI_Sg,
  P_val_UI_cosphi,
  P_extrema_U_max_4,
  P_extrema_U_min_4,
  P_extrema_U_av_4,
  P_extrema_I_max_4,
  P_extrema_I_av_4,
  P_extrema_P_max_4,
  P_extrema_P_min_4,
  P_extrema_P_av_4,
  P_extrema_Q_max_4,
  P_extrema_Q_min_4,
  P_extrema_Q_av_4,
  P_extrema_P_max_all,
  P_extrema_P_min_all,
  P_extrema_P_av_all,
  P_extrema_Q_max_all,
  P_extrema_Q_min_all,
  P_extrema_Q_av_all,
  P_extrema_S_max_all,
  P_extrema_S_av_all,
  P_extrema_U_LL_max_3,
  P_max_key,
};

static const gocfg_translate_t dataTranslation[] = {
    {"devid", P_devid},
    {"pi", P_procimage},
    {"vBE_32", P_vBE_32},
    {"vBEF_32", P_vBEF_32},
    {"vBAF_128", P_vBAF_128},
    {"vBA_8", P_vBA_8},
    {"y_BE_", P_y_BE},
    {"y_BEF_", P_y_BEF},
    {"y_BAF_8", P_y_BAF_8},
    {"y_BA_", P_y_BA},
    {"y_LED", P_y_LED},
    {"aUIg_8", P_aUIg_8},
    {"wUIg_8", P_wUIg_8},
    {"dwUIg_4", P_dwUIg_4},
    {"val_UI_Up_4", P_val_UI_Up_4},
    {"val_UI_Ip_4", P_val_UI_Ip_4},
    {"val_UI_Us_4", P_val_UI_Us_4},
    {"val_UI_Is_4", P_val_UI_Is_4},
    {"val_UI_U12p", P_val_UI_U12p},
    {"val_UI_U12p_r", P_val_UI_U12p_r},
    {"val_UI_U12p_i", P_val_UI_U12p_i},
    {"val_UI_wU12", P_val_UI_wU12},
    {"val_UI_U23p", P_val_UI_U23p},
    {"val_UI_U23p_r", P_val_UI_U23p_r},
    {"val_UI_U23p_i", P_val_UI_U23p_i},
    {"val_UI_wU23", P_val_UI_wU23},
    {"val_UI_U31p", P_val_UI_U31p},
    {"val_UI_U31p_r", P_val_UI_U31p_r},
    {"val_UI_U31p_i", P_val_UI_U31p_i},
    {"val_UI_wU31", P_val_UI_wU31},
    {"val_UI_dw_4", P_val_UI_dw_4},
    {"val_UI_P_4", P_val_UI_P_4},
    {"val_UI_Q_4", P_val_UI_Q_4},
    {"val_UI_S_4", P_val_UI_S_4},
    {"val_UI_Pg", P_val_UI_Pg},
    {"val_UI_Qg", P_val_UI_Qg},
    {"val_UI_Sg", P_val_UI_Sg},
    {"val_UI_cosphi", P_val_UI_cosphi},
    {"extrema_U_max_4", P_extrema_U_max_4},
    {"extrema_U_min_4", P_extrema_U_min_4},
    {"extrema_U_av_4", P_extrema_U_av_4},
    {"extrema_I_max_4", P_extrema_I_max_4},
    {"extrema_I_av_4", P_extrema_I_av_4},
    {"extrema_P_max_4", P_extrema_P_max_4},
    {"extrema_P_min_4", P_extrema_P_min_4},
    {"extrema_P_av_4", P_extrema_P_av_4},
    {"extrema_Q_max_4", P_extrema_Q_max_4},
    {"extrema_Q_min_4", P_extrema_Q_min_4},
    {"extrema_Q_av_4", P_extrema_Q_av_4},
    {"extrema_P_max_all", P_extrema_P_max_all},
    {"extrema_P_min_all", P_extrema_P_min_all},
    {"extrema_P_av_all ", P_extrema_P_av_all},
    {"extrema_Q_max_all", P_extrema_Q_max_all},
    {"extrema_Q_min_all", P_extrema_Q_min_all},
    {"extrema_Q_av_all", P_extrema_Q_av_all},
    {"extrema_S_max_all", P_extrema_S_max_all},
    {"extrema_S_av_all", P_extrema_S_av_all},
    {"extrema_U_LL_max_3", P_extrema_U_LL_max_3},
};

namespace {
constexpr std::pair<DataType, const char*> kDataTypeNames[] = {
    {Bit0, "Bit0"},
    {Bit1, "Bit1"},
    {Bit2, "Bit2"},
    {Bit3, "Bit3"},
    {Bit4, "Bit4"},
    {Bit5, "Bit5"},
    {Bit6, "Bit6"},
    {Bit7, "Bit7"},
    {Bit8, "Bit8"},
    {Bit9, "Bit9"},
    {Bit10, "Bit10"},
    {Bit11, "Bit11"},
    {Bit12, "Bit12"},
    {Bit13, "Bit13"},
    {Bit14, "Bit14"},
    {Bit15, "Bit15"},
    {Bit16, "Bit16"},
    {Bit17, "Bit17"},
    {Bit18, "Bit18"},
    {Bit19, "Bit19"},
    {Bit20, "Bit20"},
    {Bit21, "Bit21"},
    {Bit22, "Bit22"},
    {Bit23, "Bit23"},
    {Bit24, "Bit24"},
    {Bit25, "Bit25"},
    {Bit26, "Bit26"},
    {Bit27, "Bit27"},
    {Bit28, "Bit28"},
    {Bit29, "Bit29"},
    {Bit30, "Bit30"},
    {Bit31, "Bit31"},

    {Bit8_0, "Bit8_0"},
    {Bit8_1, "Bit8_1"},
    {Bit8_2, "Bit8_2"},
    {Bit8_3, "Bit8_3"},
    {Bit8_4, "Bit8_4"},
    {Bit8_5, "Bit8_5"},
    {Bit8_6, "Bit8_6"},
    {Bit8_7, "Bit8_7"},

    {Int8, "Int8"},
    {Int8U, "Int8U"},
    {Int16, "Int16"},
    {Int16U, "Int16U"},
    {Int32, "Int32"},
    {Int32U, "Int32U"},
    {Boolean, "Boolean"},
    {DoublePoint, "DoublePoint"},
    {Enum, "Enum"},
    {Float32, "Float32"},

    {PString, "PString"},
    {PVariant, "PVariant"},

    {QualityT, "QualityT"},
    {TimeStampT, "TimeStampT"},
    {IecInt128T, "IecInt128T"},

    {Bit16_0, "Bit16_0"},
    {Bit16_1, "Bit16_1"},
    {Bit16_2, "Bit16_2"},
    {Bit16_3, "Bit16_3"},
    {Bit16_4, "Bit16_4"},
    {Bit16_5, "Bit16_5"},
    {Bit16_6, "Bit16_6"},
    {Bit16_7, "Bit16_7"},
    {Bit16_8, "Bit16_8"},
    {Bit16_9, "Bit16_9"},
    {Bit16_10, "Bit16_10"},
    {Bit16_11, "Bit16_11"},
    {Bit16_12, "Bit16_12"},
    {Bit16_13, "Bit16_13"},
    {Bit16_14, "Bit16_14"},
    {Bit16_15, "Bit16_15"},

    {Ignore, "Ignore"},
};
}  // namespace

std::string goose::ReadXmlConfig::GetDataTypeString(DataType type) {
  for (const auto& entry : kDataTypeNames) {
    if (entry.first == type) {
      return entry.second;
    }
  }
  return "Unknown";
}

std::string goose::ReadXmlConfig::GetGooseQualityUseKindString(GooseQualityUseKind q) {
  switch (q) {
    case POSQUALITY_WITHOUT:
      return "POSQUALITY_WITHOUT";
    case POSQUALITY_AFTER:
      return "POSQUALITY_AFTER";
    case POSQUALITY_BEFORE:
      return "POSQUALITY_BEFORE";
    default:
      return "UnknownGooseQualityUseKind";
  }
}

/**
 * @brief: state/debug functions
 **/
std::string goose::ReadXmlConfig::GetGooseFcdaKindString(GooseFcdaKind type) {
  switch (type) {
    case DA_BOOLEAN:
      return "BOOLEAN";
    case DA_BITSTRING2:
      return "BITSTRING2";
    case DA_UNSIGNED:
      return "UNSIGNED";
    case DA_INTEGER:
      return "INTEGER";
    case DA_FLOAT:
      return "FLOAT";
    case DA_MAG:
      return "MAG";
    case DO_SPS:
      return "SPS";
    case DO_DPS:
      return "DPS";
    case DO_INS:
      return "INS";
    case DO_MV:
      return "MV";
    default:
      break;
  }
  return "unknow???";
}

static bool tryTranslateType(const char* str, const gocfg_translate_t* table, size_t len, int& outType, int defVal) {
  outType = defVal;
  if (!str || !*str) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (startsWithCaseInsensitive(str, table[i].name)) {
      outType = table[i].nType;
      return true;
    }
  }
  spdlog::warn("Unknown type '{}'", str);
  return false;
}

/**
 * @brief Tokenizes the given input string into a list of substrings based on the specified delimiter.
 *
 * @param input The string to be tokenized.
 * @param delims The character or string used to split the input into tokens.
 * @return A vector of strings containing the generated tokens.
 */
static std::vector<std::string_view> tokenize(std::string_view input, std::string_view delims) {
  std::vector<std::string_view> tokens;
  size_t start = 0;
  auto is_delim = [delims](char c) {
    return delims.find(c) != std::string_view::npos;
  };

  while (start < input.size()) {
    // skip leading delimiters
    while (start < input.size() && is_delim(input[start]))
      ++start;
    if (start >= input.size())
      break;

    size_t end = start;
    while (end < input.size() && !is_delim(input[end]))
      ++end;

    tokens.emplace_back(input.data() + start, end - start);
    start = end;
  }
  return tokens;
}

/**
 * Translates the provided data lexeme and updates the specified member structure accordingly.
 * @param pMember Pointer to a `GooseDataSetMember` structure that will be updated based on the translated lexemes.
 *                Must be a valid pointer.
 * @param source A C-string containing the source data to be parsed and translated. The string
 *               should include lexemes separated by a predefined set of delimiters.
 * @return `true` if the translation and updates were successful, `false` otherwise.
 */
static bool translateDataLexeme(GooseDataSetMember* pMember, const char* source) {
  // Introduce constants for readability
  static constexpr const char* kDelimiters = ";$.";
  std::string_view data = source;  // keep the original intact

  if (!pMember || !source || !strlen(source)) {
    return false;
  }

  // Small helpers to reduce duplication (extracted lambdas)
  auto parseIndexFromToken = [](const char* token) -> int {
    char* bracket = strchr(const_cast<char*>(token), '[');
    if (bracket && *(++bracket)) {
      return static_cast<int>(str_to_uint_safe(bracket));
    }
    return -1;
  };

  auto validateIndex = [](int idx, int maxInclusive, const char* token, const char* full) -> int {
    if (idx > maxInclusive) {
      DBG_WARN("Index %d in %s for %s is not valid. Set 0 by default", idx, token, full);
      return 0;
    }
    return idx;
  };

  auto setDevObjectScalar = [&](size_t offset, size_t size, DataType type) {
    pMember->devObject.offset = offset;
    pMember->devObject.size = static_cast<int>(size);
    pMember->devObject.type = type;
    pMember->lastValue.val.type = type;
  };

  auto setDevObjectArray = [&](size_t offset, size_t elemSize, DataType type) {
    pMember->devObject.offset = offset;
    pMember->devObject.size = static_cast<int>(elemSize);
    pMember->devObject.type = type;
    pMember->lastValue.val.type = type;
  };

  // Renaming for clarity
  size_t index{0};
  char* colonPos;

  // Tokenize
  for (std::string_view token : tokenize(data, kDelimiters)) {
    int key{-1};
    if (!tryTranslateType(token.data(), dataTranslation, compat::size(dataTranslation), key, P_null_key)) {
      spdlog::warn("lexeme \"{}\" is unknown in {}", token.data(), source);
      return false;
    }
    auto strVal = const_cast<char*>(token.data());
    switch (key) {
      case P_devid: {
        colonPos = strchr(strVal, ':');
        if (!colonPos || *(++colonPos) == 0) {
          DBG_WARN("colon is not found in %s for %s", token, source);
          return false;
        }
        pMember->devId = strdup_or_null_if_empty(colonPos);
        break;
      }

      case P_procimage: {
        colonPos = strchr(strVal, ':');
        if (!colonPos || *(++colonPos) == 0) {
          DBG_WARN("colon is not found in %s for %s", token, source);
          return false;
        }
        // second-level key after "pi:"
        if (!tryTranslateType(colonPos, dataTranslation, compat::size(dataTranslation), key, P_null_key)) {
          DBG_WARN("lexeme \"%s\" is unknown in %s", token, source);
          return false;
        }

        // Parse optional index [n] from original token
        int idxParsed = parseIndexFromToken(strVal);
        index = static_cast<size_t>((idxParsed < 0) ? 0 : idxParsed);

        switch (key) {
          case P_vBE_32: {
            idxParsed = validateIndex(static_cast<int>(index), 31, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, vBE, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.vBE[0])), Int32U);
            pMember->devObject.isEvent = true;
            pMember->devObject.eventIdComing = cas1_LOG_BE_coming;
            pMember->devObject.eventIdGoing = cas1_LOG_BE_going;
            break;
          }
          case P_vBEF_32: {
            idxParsed = validateIndex(static_cast<int>(index), 31, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, vBEF, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.vBEF[0])), Int32U);
            pMember->devObject.isEvent = true;
            pMember->devObject.eventIdComing = cas1_LOG_BEF_coming;
            pMember->devObject.eventIdGoing = cas1_LOG_BEF_going;
            break;
          }
          case P_vBAF_128: {
            idxParsed = validateIndex(static_cast<int>(index), 127, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, vBAF, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.vBAF[0])), Int32U);
            pMember->devObject.isEvent = true;
            pMember->devObject.eventIdComing = cas1_LOG_BAF_coming;
            pMember->devObject.eventIdGoing = cas1_LOG_BAF_going;
            break;
          }
          case P_vBA_8: {
            idxParsed = validateIndex(static_cast<int>(index), 7, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, vBA, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.vBA[0])), Int32U);
            pMember->devObject.isEvent = true;
            pMember->devObject.eventIdComing = cas1_LOG_BA_coming;
            pMember->devObject.eventIdGoing = cas1_LOG_BA_going;
            break;
          }
          case P_y_BE: {
            setDevObjectScalar(offsetof(slave_pi, y_BE), sizeof(typeof(((SLAVES*)0)->slave_pi.y_BE)), Int32U);
            break;
          }
          case P_y_BEF: {
            setDevObjectScalar(offsetof(slave_pi, y_BEF), sizeof(typeof(((SLAVES*)0)->slave_pi.y_BEF)), Int32U);
            break;
          }
          case P_y_BAF_8: {
            idxParsed = validateIndex(static_cast<int>(index), 7, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, y_BAF, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.y_BAF[0])), Int32U);
            break;
          }
          case P_y_BA: {
            setDevObjectScalar(offsetof(slave_pi, y_BA), sizeof(typeof(((SLAVES*)0)->slave_pi.y_BA)), Int32U);
            break;
          }
          case P_y_LED: {
            setDevObjectScalar(offsetof(slave_pi, y_LED), sizeof(typeof(((SLAVES*)0)->slave_pi.y_LED)), Int32U);
            break;
          }
          case P_aUIg_8: {
            idxParsed = validateIndex(static_cast<int>(index), 7, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, aUIg, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.aUIg[0])), Float32);
            break;
          }
          case P_wUIg_8: {
            idxParsed = validateIndex(static_cast<int>(index), 7, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, wUIg, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.wUIg[0])), Float32);
            break;
          }
          case P_dwUIg_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, dwUIg, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.dwUIg[0])), Float32);
            break;
          }
          case P_val_UI_Up_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.Up, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Up[0])), Float32);
            break;
          }
          case P_val_UI_Ip_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.Ip, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Ip[0])), Float32);
            break;
          }
          case P_val_UI_Us_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.Us, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Us[0])), Float32);
            break;
          }
          case P_val_UI_Is_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.Is, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Is[0])), Float32);
            break;
          }
          case P_val_UI_U12p: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U12p), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U12p)),
                               Float32);
            break;
          }
          case P_val_UI_U12p_r: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U12p_r), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U12p_r)),
                               Float32);
            break;
          }
          case P_val_UI_U12p_i: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U12p_i), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U12p_i)),
                               Float32);
            break;
          }
          case P_val_UI_wU12: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.wU12), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.wU12)),
                               Float32);
            break;
          }
          case P_val_UI_U23p: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U23p), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U23p)),
                               Float32);
            break;
          }
          case P_val_UI_U23p_r: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U23p_r), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U23p_r)),
                               Float32);
            break;
          }
          case P_val_UI_U23p_i: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U23p_i), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U23p_i)),
                               Float32);
            break;
          }
          case P_val_UI_wU23: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.wU23), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.wU23)),
                               Float32);
            break;
          }
          case P_val_UI_U31p: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U31p), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U31p)),
                               Float32);
            break;
          }
          case P_val_UI_U31p_r: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U31p_r), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U31p_r)),
                               Float32);
            break;
          }
          case P_val_UI_U31p_i: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.U31p_i), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.U31p_i)),
                               Float32);
            break;
          }
          case P_val_UI_wU31: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.wU31), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.wU31)),
                               Float32);
            break;
          }
          case P_val_UI_dw_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.dw, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.dw[0])), Float32);
            break;
          }
          case P_val_UI_P_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.P, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.P[0])), Float32);
            break;
          }
          case P_val_UI_Q_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.Q, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Q[0])), Float32);
            break;
          }
          case P_val_UI_S_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY(slave_pi, val_UI.S, static_cast<int>(index)),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.S[0])), Float32);
            break;
          }
          case P_val_UI_Pg: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.Pg), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Pg)),
                               Float32);
            break;
          }
          case P_val_UI_Qg: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.Qg), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Qg)),
                               Float32);
            break;
          }
          case P_val_UI_Sg: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.Sg), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.Sg)),
                               Float32);
            break;
          }
          case P_val_UI_cosphi: {
            setDevObjectScalar(offsetof(slave_pi, val_UI.cosphi), sizeof(typeof(((SLAVES*)0)->slave_pi.val_UI.cosphi)),
                               Float32);
            break;
          }
          case P_extrema_U_max_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.U_max, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.U_max[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_U_min_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.U_min, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.U_min[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_U_av_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.U_av, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.U_av[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_I_max_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.I_max, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.I_max[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_I_av_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.I_av, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.I_av[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_P_max_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.P_max, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.P_max[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_P_min_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.P_min, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.P_min[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_P_av_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.P_av, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.P_av[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_Q_max_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.Q_max, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.Q_max[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_Q_min_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.Q_min, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.Q_min[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_Q_av_4: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.Q_av, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.Q_av[0].Value_f32)), Float32);
            break;
          }
          case P_extrema_P_max_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.P_max_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.P_max_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_P_min_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.P_min_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.P_min_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_P_av_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.P_av_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.P_av_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_Q_max_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.Q_max_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.Q_max_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_Q_min_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.Q_min_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.Q_min_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_Q_av_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.Q_av_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.Q_av_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_S_max_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.S_max_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.S_max_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_S_av_all: {
            setDevObjectScalar(offsetof(slave_pi, extrema.S_av_all.Value_f32),
                               sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.S_av_all.Value_f32)), Float32);
            break;
          }
          case P_extrema_U_LL_max_3: {
            idxParsed = validateIndex(static_cast<int>(index), 3, strVal, source);
            index = static_cast<size_t>(idxParsed);
            setDevObjectArray(OFFSET_ARRAY_MEMBER(slave_pi, extrema.U_LL_max, static_cast<int>(index), Value_f32),
                              sizeof(typeof(((SLAVES*)0)->slave_pi.extrema.U_LL_max[0].Value_f32)), Float32);
            break;
          }
          default:
            DBG_WARN("data is unknown in %s for %s", token, source);
            return false;
        }

        pMember->devObject.index = static_cast<int>(index);
        break;
      }

      default:
        spdlog::warn("data is unknown in {} for {}", token, source);
        return false;
    }
  }
  return true;
}

/**
 * fills simulation data from XML into simulation cintext
 *
 * @details    fills simulation data from XML into simulation cintext
 *
 * @param[in]  ...
 * @return     true if successfully, otherwise false if an error occurred
 */
static bool fillSimRecord(pugi::xml_node& nodeSim, GooseAppContext* applContext, GooseDataSetMember* member) {
  auto record = new GooseSimRecord{};
  record->member = member;

  record->next = nullptr;

  // <Sim activate="1" type="Real" probability="50" delta="1" min="0" max="100"/>
  record->typeCode = sim_lookupSimRecType(nodeSim.attribute("type").as_string());
  if (record->typeCode == -1) {
    spdlog::warn("Translate sim type in member {} failure\n", member->desc);
    delete record;
    return false;
  }

  record->enabled = true;
  record->delta = nodeSim.attribute("delta").as_uint();
  record->interval = nodeSim.attribute("probability").as_uint();

  switch (record->typeCode) {
    case unsignedType:
      record->lo.u = nodeSim.attribute("min").as_uint();
      record->hi.u = nodeSim.attribute("max").as_uint();
      switch (member->devObject.size) {
        case 1:
          record->varVal.type = Int8U;
          break;
        case 2:
          record->varVal.type = Int16U;
          break;
        case 4:
        default:
          record->varVal.type = Int32U;
          break;
      }
      break;
    case integerType:
      record->lo.l = nodeSim.attribute("min").as_int();
      record->hi.l = nodeSim.attribute("max").as_int();
      switch (member->devObject.size) {
        case 1:
          record->varVal.type = Int8;
          break;
        case 2:
          record->varVal.type = Int16;
          break;
        case 4:
        default:
          record->varVal.type = Int32;
          break;
      }
      break;
    case floatType:
      record->lo.f = nodeSim.attribute("min").as_uint();
      record->hi.f = nodeSim.attribute("max").as_float();
      record->varVal.type = Float32;
      break;
    case bitsType:
      record->lo.u = nodeSim.attribute("min").as_uint() & 0x03;
      record->hi.u = nodeSim.attribute("max").as_uint() & 0x03;
      record->varVal.type = DoublePoint;
      break;
    case boolType:
      record->lo.u = nodeSim.attribute("min").as_uint(false);
      record->hi.u = nodeSim.attribute("max").as_uint(false);
      record->varVal.type = Boolean;
      break;
    default:
      break;
  }

  // initialize
  sim_setOnceTypes(record);

  // create context
  if (applContext->simContext == nullptr) {
    applContext->simContext = sim_newContext();
    if (applContext->simContext == nullptr) {
      spdlog::warn("Ops! Create sim context - out of memory\n");
      delete record;
      return false;
    }
  }

  if (DBG_IF_COND) {
    sim_displaySimRec(record);
  }

  /* add to list */
  record->next = nullptr;
  if (applContext->simContext->simulatedData != nullptr) {
    applContext->simContext->simulatedData->next = record;
  }
  applContext->simContext->simulatedData = record;

  return true;
}

/**
 * @brief    : reads the new dataset from XML
 *   <DataSet name="TEMPLATEA/LLN0$GoDsScalar">
 *	    <FCDA name="" type="BOOL" qualPos="Next">
 *		    <Data data="lexemes" factor="0"/>
 *		    <DeadBand activate="0" db="0" min="0" max="0" zeroDb="0"/>
 *	    </FCDA>
 * @arguments : node, context, this dataset and its subscribe
 * @return   : not -1 if successfully, otherwise -1
 **/
bool fillDataSet(pugi::xml_node& nodeDataset, GoosePublisher* pPublisher) {
  for (pugi::xml_node nodeFCDA = nodeDataset.child("FCDA"); nodeFCDA; nodeFCDA = nodeFCDA.next_sibling("FCDA")) {
    /* search DO */
    auto nodeData = nodeFCDA.child("Data");
    if (nodeData.empty()) {
      spdlog::warn("Search Dataset/FCDA/Data fault: {} {} ({})\n", pPublisher->gse.id, pPublisher->gse.cbRef,
                   pPublisher->desc);
      return false;
    }

    /* search <DeadBand activate="1" db="2000" min="9000" max="10100" zeroDb="5000"/> */
    auto nodeDeadBand = nodeFCDA.child("DeadBand");
    if (nodeDeadBand.empty()) {
      spdlog::info("Node DeadBand not found in: {} {} ({})\n", pPublisher->gse.id, pPublisher->gse.cbRef,
                   pPublisher->desc);
    }

    auto pMember = new GooseDataSetMember();
    if (pMember == nullptr) {
      spdlog::warn("Create Dataset member fault: {} {} ({})\n", pPublisher->gse.id, pPublisher->gse.cbRef,
                   pPublisher->desc);
      return false;
    }
    InitDataSetMember(pPublisher->dataset, pMember);

    pMember->desc = strdup_or_null_if_empty(nodeFCDA.attribute("desc").as_string());
    if (pMember->desc == nullptr) {
      spdlog::warn("Desc attribute in FCDA desc='' is empty (mandatory text) in {} {} ({})\n", pPublisher->gse.id,
                   pPublisher->gse.cbRef, pPublisher->desc);
      return false;
    }

    int translationResult;
    const char* value = nodeFCDA.attribute("qualPos").as_string();
    if (!tryTranslateType(value, qualityTypeTranslation, compat::size(qualityTypeTranslation), translationResult,
                          POSQUALITY_WITHOUT)) {
      spdlog::warn("Translate posQuality:{} failure in {} {} ({})\n", value, pPublisher->gse.id, pPublisher->gse.cbRef,
                   pPublisher->desc);
      return false;
    }
    pMember->qualPos = static_cast<GooseQualityUseKind>(translationResult);

    value = nodeFCDA.attribute("type").as_string();
    if (!tryTranslateType(value, fcdaTypeTranslation, compat::size(fcdaTypeTranslation), translationResult,
                          FCDATYPE_UNKNOWN)) {
      spdlog::warn("Translate FCDA type:{} failure in {} {} ({})\n", value, pPublisher->gse.id, pPublisher->gse.cbRef,
                   pPublisher->desc);
      return false;
    }
    pMember->fcdaType = static_cast<GooseFcdaKind>(translationResult);

    switch (pMember->fcdaType) {
      case DA_BOOLEAN:
        pMember->emitFunc = goo_emitMemberBoolean;
        break;
      case DA_BITSTRING2:
        pMember->emitFunc = goo_emitMemberBitStringL2;
        break;
      case DA_UNSIGNED:
        pMember->emitFunc = goo_emitMemberUnsigned;
        break;
      case DA_INTEGER:
        pMember->emitFunc = goo_emitMemberInteger;
        break;
      case DA_FLOAT:
        pMember->emitFunc = goo_emitMemberFloat32;
        break;
      case DA_MAG:
        pMember->emitFunc = goo_emitMemberStrMagF32;
        break;
      case DO_SPS:
        pMember->emitFunc = goo_emitMemberSPS;
        break;
      case DO_DPS:
        pMember->emitFunc = goo_emitMemberDPS;
        break;
      case DO_INS:
        pMember->emitFunc = goo_emitMemberINS;
        break;
      case DO_MV:
        pMember->emitFunc = goo_emitMemberMV;
        break;
      default:
        break;
    }

    /*
     * dead band for float
     * <DeadBand activate="1" db="0.1" min="0" max="1000" zeroDb="5.0"/>
     */
    if (nodeDeadBand) {
      pMember->db.activated = nodeDeadBand.attribute("activate").as_bool(false);
      if (pMember->db.activated) {
        pMember->db.threshold = nodeDeadBand.attribute("db").as_double(0);
        pMember->db.zeroThreshold = nodeDeadBand.attribute("zeroDb").as_double(0.001);
        pMember->db.type = nodeDeadBand.attribute("isAbsolute").as_bool(false) ? DB_ABSOLUTE : DB_PERCENTAGE;
        if (pMember->lastValue.val.type == Float32) {
          if (pMember->db.threshold == 0) {
            pMember->db.threshold = 0.1;
          }
          if (pMember->db.zeroThreshold == 0) {
            pMember->db.zeroThreshold = 0.001;
          }
        }
      }
    }

    /*
     * node Data
     * <Data data="devid:A;pi:y_BA" factor="0"/>
     */
    value = nodeData.attribute("data").as_string();
    if (!translateDataLexeme(pMember, value)) {
      spdlog::warn("Translate data '{}' failure in {} {} ({})\n", value, pPublisher->gse.id, pPublisher->gse.cbRef,
                   pPublisher->desc);
      return false;
    }

    /* add to list */
    pMember->prev = nullptr;
    pMember->next = nullptr;
    if (pPublisher->dataset->memberList == nullptr) {
      pPublisher->dataset->memberList = pMember;
    } else {
      GooseDataSetMember* tail = pPublisher->dataset->memberList;
      while (tail->next) {
        tail = tail->next;
      }
      pMember->prev = tail;
      tail->next = pMember;
    }
    pPublisher->dataset->tailMember = pMember;
  }
  return true;
}

/**
 * prints data set
 *
 * @details    prints data set
 *
 * @param[in]  dataset
 * @author     (AS)
 */
static void gooPrintDataset(const GooseDataSet* dataset) {
  GooseDataSetMember* pMember;
  int i = 0;

  pMember = dataset->memberList;
  while (pMember != nullptr) {
    if (pMember->qualPos == POSQUALITY_BEFORE)
      i++;

    if (spdlog::get_level() < spdlog::level::debug) {
      spdlog::debug("    - Member No:{} '{}' data type:{} FCDA type:'{}' {}", i++, pMember->desc,
                    goose::ReadXmlConfig::GetDataTypeString(pMember->lastValue.val.type),
                    goose::ReadXmlConfig::GetGooseFcdaKindString(pMember->fcdaType),
                    (pMember->qualPos == POSQUALITY_AFTER)    ? " 'q after'"
                    : (pMember->qualPos == POSQUALITY_BEFORE) ? " 'q before'"
                                                              : "");

      spdlog::debug("        factor:{} qualPos:{} desc:{}", pMember->factor,
                    goose::ReadXmlConfig::GetGooseQualityUseKindString(pMember->qualPos),
                    pMember->desc ? pMember->desc : "");

      spdlog::debug("        this:{} prev:{} next:{} offset:{} size:{} index:{} type:{}",
                    static_cast<const void*>(pMember), static_cast<const void*>(pMember->prev),
                    static_cast<const void*>(pMember->next), pMember->devObject.offset, pMember->devObject.size,
                    pMember->devObject.index, goose::ReadXmlConfig::GetDataTypeString(pMember->devObject.type));

      spdlog::debug("        deadband:{} type:{} db:{} dbZero:{}", pMember->db.activated ? "activate" : "n.a.",
                    (pMember->db.type == DB_PERCENTAGE) ? "percentage"
                    : (pMember->db.type == DB_ABSOLUTE) ? "absolute"
                                                        : "integral",
                    pMember->db.threshold, pMember->db.zeroThreshold);
    } else {
      spdlog::debug("    - Member No:{} '{}' {} - {}", i++,
                    goose::ReadXmlConfig::GetGooseFcdaKindString(pMember->fcdaType),
                    (pMember->qualPos == POSQUALITY_AFTER)    ? " 'q after'"
                    : (pMember->qualPos == POSQUALITY_BEFORE) ? " 'q before'"
                                                              : "",
                    pMember->desc ? pMember->desc : "");
    }

    if (pMember->qualPos == POSQUALITY_AFTER)
      i++;

    pMember = pMember->next;
  }
}

namespace goose {

std::string ReadXmlConfig::GetMacAddressString(const link_macAddress_t& addr) {
  if (addr.len == 0) {
    return {};
  }

  std::string result;
  result.reserve(addr.len * 3);

  for (uint8_t i = 0; i < addr.len; ++i) {
    if (i > 0) {
      result += ':';
    }
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", addr.addr[i]);
    result += hex;
  }
  return result;
}

bool ReadXmlConfig::readGooseGseConfig(pugi::xml_node& node, link_macAddress_t& macaddr, uint8_t& priority,
                                       unsigned int& vlanId, unsigned int& appId) {
  auto parseHexUint = [](const char* s, unsigned int& out) -> bool {
    if (!s)
      return false;
    unsigned int val = 0;
    uint8_t nibble = 0;
    const char* ptr = s;
    bool any = false;
    while (ptr && *ptr) {
      if (hexchar_to_nibble(*ptr, &nibble)) {
        val = (val << 4) | nibble;
        any = true;
        ++ptr;
      } else {
        break;
      }
    }
    if (!any)
      return false;
    out = val;
    return true;
  };

  auto parseMac = [](const char* s, link_macAddress_t& mac) -> bool {
    if (!s)
      return false;
    constexpr int kMacOctets = 6;
    int count = 0;
    const uint8_t* in = reinterpret_cast<const uint8_t*>(s);
    uint8_t* outOctet = mac.addr;
    const uint8_t* ptr = in;
    for (; count < kMacOctets; ++count, ++outOctet) {
      ptr = hex_stream_read_byte(const_cast<uint8_t*>(ptr), outOctet);
      if (ptr == nullptr)
        break;
      if (*ptr == '-' || *ptr == ':')
        ++ptr;
    }
    mac.len = static_cast<uint8_t>(count);
    return count == kMacOctets;
  };

  bool sawAny = false;

  for (auto propNode = node.child("P"); propNode; propNode = propNode.next_sibling("P")) {
    const char* typeStr = propNode.attribute("type").as_string();
    const char* value = propNode.child_value();
    if (!typeStr || !value)
      continue;

    if (strcmp(typeStr, "MAC-Address") == 0) {
      if (parseMac(value, macaddr))
        sawAny = true;
    } else if (strcmp(typeStr, "APPID") == 0) {
      unsigned int val = 0;
      if (parseHexUint(value, val)) {
        appId = val;
        sawAny = true;
      }
    } else if (strcmp(typeStr, "VLAN-PRIORITY") == 0) {
      priority = static_cast<uint8_t>(std::atoi(value));
      sawAny = true;
    } else if (strcmp(typeStr, "VLAN-ID") == 0) {
      unsigned int val = 0;
      if (parseHexUint(value, val)) {
        vlanId = val;
        sawAny = true;
      }
    }
  }

  return sawAny;
}

// Parse dotted IPv4 into host-byte-order uint32_t
bool ipv4FromString(const std::string& dotted, uint32_t& out) {
  in_addr addr{};
  if (dotted.empty() || inet_pton(AF_INET, dotted.c_str(), &addr) != 1)
    return false;
  out = ntohl(addr.s_addr);
  return true;
}

uint8_t ReadXmlConfig::resolveLogMask(const char* mask) {
  if (!mask || !strlen(mask)) {
    return (-1);
  }
  std::string normalized(mask);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
  for (auto& item : kmasks) {
    if (normalized.rfind(item.desc, 0) == 0) {
      return (item.val);
    }
  }
  return LOG_EMERG;
}

bool ReadXmlConfig::readCfg(const std::string& fileName, LogConfigParam& logConfig) {
  try {
    pugi::xml_parse_result result = m_xmldoc.load_file(fileName.c_str());
    if (result.status != pugi::status_ok) {
      spdlog::error("ReadXmlConfig: failed to load XML file {}: {}", fileName, result.description());
      return false;
    }
    // Find logging node
    auto node = m_xmldoc.child("Software").child("Logging");
    if (node.empty()) {
      spdlog::error("ReadXmlConfig: logging node not found");
      return false;
    }
    return readCfg(node, logConfig);
  } catch (std::exception e) {
    spdlog::error("ReadXmlConfig: failed to load XML file {}: {}", fileName, e.what());
    return false;
  }
}

bool ReadXmlConfig::readCfg(pugi::xml_node& node, LogConfigParam& logConfig) {
  if (node.empty()) {
    return false;
  }
  logConfig = LogConfigParam();

  const char* value = node.attribute("level").as_string();
  uint8_t mask = resolveLogMask(value);
  if (mask == -1) {
    spdlog::error("ReadXmlConfig: invalid log level: {}", value);
  } else {
    logConfig.level = mask;
  }

  logConfig.port = node.attribute("port").as_uint(512);
  value = node.attribute("ip").as_string("0.0.0.0");
  if (value != nullptr) {
    if (!ipv4FromString(value, logConfig.serverIp)) {
      spdlog::error("ReadXmlConfig: invalid ip address: {}", value);
    }
  }
  logConfig.syslog = node.attribute("syslog").as_bool(false);
  logConfig.withtime = node.attribute("withtime").as_bool(false);
  logConfig.common = node.attribute("common").as_bool(false);
  logConfig.param = node.attribute("param").as_bool(false);
  logConfig.error = node.attribute("error").as_bool(false);
  logConfig.event = node.attribute("event").as_bool(false);
  logConfig.time = node.attribute("time").as_bool(false);
  logConfig.image = node.attribute("image").as_bool(false);
  logConfig.commands = node.attribute("commands").as_bool(false);
  logConfig.gooseVerbose = node.attribute("gooseVerbose").as_bool(false);
  logConfig.gooseSubscribe = node.attribute("goi").as_bool(false);
  logConfig.goosePublish = node.attribute("goo").as_bool(false);
  logConfig.gooseEvent = node.attribute("goevents").as_bool(false);

  return true;
}

bool ReadXmlConfig::readCfg(const std::string& fileName, GooseAppContext& context) {
  try {
    pugi::xml_parse_result result = m_xmldoc.load_file(fileName.c_str());
    if (result.status != pugi::status_ok) {
      spdlog::error("ReadXmlConfig: failed to load XML file {}: {}", fileName, result.description());
      return false;
    }
    // Find logging node Settings
    auto node = m_xmldoc.child("Settings").child("Software").child("Logging");
    if (node.empty()) {
      spdlog::error("ReadXmlConfig: logging node not found");
      return false;
    }
    readCfg(node, logConfigParams);

    InitGooseAppContext(&context);

    // search GOOSE
    auto nodeGoose = m_xmldoc.child("Settings").child("Protocol").child("GOOSE");
    if (nodeGoose.empty()) {
      spdlog::error("ReadXmlConfig: node GOOSE not found in XML");
      return false;
    }
    if (!nodeGoose.attribute("activate").as_bool(false)) {
      spdlog::info("GOOSE is deactivated in XML");
      return true;
    }

    // readout all publisher
    for (auto nodePublisher = nodeGoose.child("GOOSEOutgoing").child("Publisher"); nodePublisher;
         nodePublisher = nodePublisher.next_sibling()) {
      if (nodePublisher.attribute("activate").as_bool(false) == false) {
        spdlog::debug("GOOSE publisher is deactivated in XML");
        continue;
      }
      readPublisherConfig(nodePublisher, context);
    }
    if (context.publishersList == nullptr) {
      spdlog::info("No active GOOSE publisher found in XML");
    }
    return true;

  } catch (std::exception e) {
    spdlog::error("ReadXmlConfig: failed to load XML file {}: {}", fileName, e.what());
    return false;
  }
}

void ReadXmlConfig::readPublisherConfig(pugi::xml_node& node, GooseAppContext& context) {
  std::string textBuffer;
  const char* value;
  GoosePublisher* pPublisher = nullptr;

  /* TimeStrategy */
  auto nodeTimeStrategy = node.child("TimeStrategy");
  if (!nodeTimeStrategy) {
    spdlog::warn("GOOSE: Node Publisher/TimeStrategy not found!");
    return;
  }

  auto nodeArithmetic = nodeTimeStrategy.child("Arithmetic");
  if (nodeArithmetic.empty()) {
    spdlog::warn("GOOSE: Node Publisher/TimeStrategy/Arithmetic not found!");
    return;
  }

  auto nodeProfile = nodeTimeStrategy.child("Profile");
  if (nodeProfile.empty()) {
    spdlog::warn("GOOSE: Node Publisher/TimeStrategy/Profile not found!");
    return;
  }

  auto nodeGseControl = node.child("GSEControl");
  if (nodeGseControl.empty()) {
    spdlog::warn("GOOSE: Node Publisher/GSEControl not found!");
    return;
  }

  /* GSE */
  auto nodeGse = node.child("GSE");
  if (nodeGse.empty()) {
    spdlog::warn("GOOSE: Node Publisher/GSE not found!");
    return;
  }

  auto nodeAddress = nodeGse.child("Address");
  if (nodeAddress == nullptr) {
    spdlog::warn("GOOSE: Node Publisher/GSE/Address not found!");
    return;
  }

  auto nodeDataset = node.child("DataSet");
  if (nodeDataset == nullptr) {
    spdlog::warn("GOOSE: Node Publisher/DataSet not found!");
    return;
  }

  // all nodes exist. Create a new publisher
  pPublisher = new GoosePublisher();
  if (pPublisher == nullptr) {
    spdlog::error("No memory for Publisher");
    return;
  }

  // initialization of publisher
  InitPublisher(&context, pPublisher);

  pPublisher->desc = strdup_or_null_if_empty(node.attribute("desc").as_string());

  auto timeStrategy = nodeTimeStrategy.attribute("transmStrategy").as_uint();
  switch (timeStrategy) {
    /* ------------------------------
     * geometric
     * ------------------------------ */
    case GOOSE_TIMING_GEOMETRIC_PROGRESSION:
      pPublisher->timingStrategy.strategyType = GOOSE_TIMING_GEOMETRIC_PROGRESSION;
      pPublisher->timingStrategy.ari.firstInterval =
          nodeArithmetic.attribute("firstInterval").as_uint(GOO_TIME_DEF_INTERVAL);
      pPublisher->timingStrategy.ari.multiplier =
          nodeArithmetic.attribute("multiplier").as_uint(GOO_TIME_DEF_MULTIPLAYER);
      pPublisher->timingStrategy.ari.maxInterval =
          nodeArithmetic.attribute("maxInterval").as_uint(GOO_TIME_DEF_MAXINTERVAL);
      break;

      /* ------------------------------
       * Case value2
       * ------------------------------ */
    case GOOSE_TIMING_INTERVAL_SEQUENCE: {
      char name[10];
      int i;

      pPublisher->timingStrategy.strategyType = GOOSE_TIMING_INTERVAL_SEQUENCE;
      pPublisher->timingStrategy.pro.currentIndex = 0;
      pPublisher->timingStrategy.pro.numProfiles = nodeArithmetic.attribute("numbInt").as_uint(6);

      for (i = 0;
           i < pPublisher->timingStrategy.pro.numProfiles && i < compat::size(pPublisher->timingStrategy.pro.profiles);
           ++i) {
        // name
        sprintf(name, "int%d", i);
        pPublisher->timingStrategy.pro.profiles[i] = nodeArithmetic.attribute(name).as_uint(GOO_TIME_DEF_INTERVAL * i);
        if (!pPublisher->timingStrategy.pro.profiles[i]) {
          break;
        }
      }
      break;
    }

      /* ------------------------------
       * Default case
       * ------------------------------ */
    default:
      spdlog::warn("unexpected GOOSE time strategy {} in XML file", timeStrategy);
      pPublisher->timingStrategy.strategyType = GOOSE_TIMING_GEOMETRIC_PROGRESSION;
      pPublisher->timingStrategy.ari.firstInterval = GOO_TIME_DEF_INTERVAL;
      pPublisher->timingStrategy.ari.multiplier = GOO_TIME_DEF_MULTIPLAYER;
      pPublisher->timingStrategy.ari.maxInterval = GOO_TIME_DEF_MAXINTERVAL;
      break;
  }

  /* Decode GSEControl
        <GSEControl name="IED_ACTRL/LLN0$GO$goCb1" appID="GoAppId1" confRev="1"/>
    */
  value = nodeGseControl.attribute("name").as_string();
  if (value == nullptr || !strlen(value)) {
    spdlog::warn("Decoding GSEControl fault: Empty CbRef\n");
    deleteGoosePublisher(pPublisher);
    return;
  }
  strncpy(pPublisher->gse.cbRef, value, sizeof(pPublisher->gse.cbRef));

  value = nodeGseControl.attribute("appID").as_string();
  if (value == nullptr || !strlen(value)) {
    spdlog::warn("Decoding GSEControl fault: Empty appID. \n");
    deleteGoosePublisher(pPublisher);
    return;
  }
  strncpy(pPublisher->gse.id, value, sizeof(pPublisher->gse.id));

  pPublisher->gse.confRev = nodeGseControl.attribute("confRev").as_uint(0);

  /* Decode the address information from GSE-Address
        <Address> - <P...
    */
  if (!readGooseGseConfig(nodeAddress, pPublisher->gse.addr, pPublisher->gse.vlanPriority, pPublisher->gse.vlanId,
                          pPublisher->gse.appId)) {
    spdlog::warn("Decoding GSE address error\n");
    deleteGoosePublisher(pPublisher);
    return;
  }

  // Decode dataset
  pPublisher->dataset = new GooseDataSet();
  if (pPublisher->dataset == nullptr) {
    spdlog::warn("No DataSet found in Publisher for: {} {} ({})\n", pPublisher->gse.id, pPublisher->gse.cbRef,
                 pPublisher->desc);
    deleteGoosePublisher(pPublisher);
    return;
  }

  InitDataSet(pPublisher, pPublisher->dataset);

  // name is mandatory
  pPublisher->dataset->name = strdup_or_null_if_empty(nodeDataset.attribute("name").as_string());
  if (!pPublisher->dataset->name) {
    spdlog::warn("DataSet name is empty. Failure for: {} {} ({})\n", pPublisher->gse.id, pPublisher->gse.cbRef,
                 pPublisher->desc);
    deleteGoosePublisher(pPublisher);
    return;
  }
  if (!fillDataSet(nodeDataset, pPublisher)) {
    spdlog::warn("Decoding DataSet fault for: {} {} ({})\n", pPublisher->gse.id, pPublisher->gse.cbRef,
                 pPublisher->desc);
    deleteGoosePublisher(pPublisher);
    return;
  }

  // set data to publisher
  pPublisher->next = nullptr;
  if (context.publishersList == nullptr) {
    context.publishersList = pPublisher;
  } else {
    // find the last publisher in a list and to add a new one
    auto p = context.publishersList;
    while (p) {
      if (p->next == nullptr) {
        p->next = pPublisher;
        break;
      }
      p = p->next;
    }
  }
  // output publisher data
  if (spdlog::get_level() <= spdlog::level::debug) {
    spdlog::debug("GOOSE Publisher: {}", pPublisher->desc);
    spdlog::debug("  - CBRef   : {}", pPublisher->gse.cbRef);
    spdlog::debug("  - GOOSE.ID: {}", pPublisher->gse.id);
    spdlog::debug("  - APPID   : {} [0x{:04X}]", pPublisher->gse.appId, pPublisher->gse.appId);
    spdlog::debug("  - MAC     : {}", GetMacAddressString(pPublisher->gse.addr));
    spdlog::debug("  - VLANPrio: {}", pPublisher->gse.vlanPriority);
    spdlog::debug("  - VLAN ID : {}", pPublisher->gse.vlanId);
    spdlog::debug("  - Conf.Rev: {}", pPublisher->gse.confRev);
    spdlog::debug("  - DataSet : {}", pPublisher->dataset->name);
    if (spdlog::get_level() < spdlog::level::debug) {
      gooPrintDataset(pPublisher->dataset);
    }
  }
}

};  // namespace goose
