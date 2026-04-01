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

namespace iec61850 {

/**
 * @brief IEC 61850 Validity enumeration
 */
enum class Validity : uint16_t { Good = 0x0000, Invalid = 0x4000, Reserved = 0x8000, Questionable = 0xC000 };

/**
 * @brief IEC 61850 Source enumeration
 */
enum class Source : uint16_t { Process = 0x0000, Substituted = 0x0020 };

/**
 * @brief Modern C++ class for IEC 61850 Quality attributes (13 bits).
 *
 * The 13 bits are represented internally as a uint16_t,
 * but can be accessed as two octets as required by the standard.
 */
class Quality {
  enum Mask : uint16_t {
    ValidityMask = 0xC000,
    OverflowMask = 0x2000,
    OutofRangeMask = 0x1000,
    BadReferenceMask = 0x0800,
    OscillatoryMask = 0x0400,
    FailureMask = 0x0200,
    OldDataMask = 0x0100,
    InconsistentMask = 0x0080,
    InaccurateMask = 0x0040,
    SourceMask = 0x0020,
    TestMask = 0x0010,
    OperatorBlockedMask = 0x0008,
    AllFlagsMask = 0xFFF8
  };

 public:
  enum Flags : uint16_t {
    Overflow = OverflowMask,
    OutofRange = OutofRangeMask,
    BadReference = BadReferenceMask,
    Oscillatory = OscillatoryMask,
    Failure = FailureMask,
    OldData = OldDataMask,
    Inconsistent = InconsistentMask,
    Inaccurate = InaccurateMask,
    Test = TestMask,
    OperatorBlocked = OperatorBlockedMask
  };

  /**
   * @brief Default constructor. Initializes quality to Invalid.
   */
  Quality() : m_value(static_cast<uint16_t>(Validity::Invalid)) {}

  /**
   * @brief Construct from a 16-bit value.
   * @param value The 16-bit value.
   */
  explicit Quality(uint16_t value) : m_value(static_cast<uint16_t>(value & AllFlagsMask)) {}

  /**
   * @brief Construct from two octets.
   * @param octet0 The first octet (MSB).
   * @param octet1 The second octet (LSB).
   */
  Quality(uint8_t octet0, uint8_t octet1) {
    m_value = static_cast<uint16_t>(((static_cast<uint16_t>(octet0) << 8) | octet1) & AllFlagsMask);
  }

  // Accessors
  /**
   * @brief Get the internal 16-bit value.
   * @return The 16-bit value.
   */
  [[nodiscard]] uint16_t getValue() const {
    return m_value;
  }

  /**
   * @brief Get one of the two octets.
   * @param index The index of the octet (0 or 1).
   * @return The octet value.
   */
  [[nodiscard]] uint8_t getOctet(int index) const {
    if (index == 0)
      return static_cast<uint8_t>((m_value >> 8) & 0xFF);
    if (index == 1)
      return static_cast<uint8_t>(m_value & 0xFF);
    return 0;
  }

  // Validity
  /**
   * @brief Get the validity attribute.
   * @return The validity value.
   */
  [[nodiscard]] Validity getValidity() const {
    return static_cast<Validity>(m_value & ValidityMask);
  }

  /**
   * @brief Set the validity attribute.
   * @param v The validity value to set.
   */
  void setValidity(Validity v) {
    m_value = (m_value & ~ValidityMask) | static_cast<uint16_t>(v);
  }

  /**
   * @brief Check if validity is Good.
   * @return True if Good, false otherwise.
   */
  [[nodiscard]] bool isGood() const {
    return getValidity() == Validity::Good;
  }

  /**
   * @brief Check if validity is Invalid.
   * @return True if Invalid, false otherwise.
   */
  [[nodiscard]] bool isInvalid() const {
    return getValidity() == Validity::Invalid;
  }

  /**
   * @brief Check if validity is Questionable.
   * @return True if Questionable, false otherwise.
   */
  [[nodiscard]] bool isQuestionable() const {
    return getValidity() == Validity::Questionable;
  }

  // Source
  /**
   * @brief Get the source attribute.
   * @return The source value.
   */
  [[nodiscard]] Source getSource() const {
    return static_cast<Source>(m_value & SourceMask);
  }

  /**
   * @brief Set the source attribute.
   * @param s The source value to set.
   */
  void setSource(Source s) {
    m_value = (m_value & ~SourceMask) | static_cast<uint16_t>(s);
  }

  // Flags
  /**
   * @brief Check if a specific flag is set.
   * @param f The flag to check.
   * @return True if the flag is set, false otherwise.
   */
  [[nodiscard]] bool isFlagSet(Flags f) const {
    return (m_value & f) != 0;
  }

  /**
   * @brief Set or clear a specific flag.
   * @param f The flag to modify.
   * @param active True to set the flag, false to clear it.
   */
  void setFlag(Flags f, bool active = true) {
    if (active)
      m_value |= f;
    else
      m_value &= ~f;
  }

  // Named flag accessors
  /**
   * @brief Check if the Overflow flag is set.
   * @return True if Overflow is set.
   */
  [[nodiscard]] bool hasOverflow() const {
    return isFlagSet(Overflow);
  }

  /**
   * @brief Set the Overflow flag.
   * @param b True to set, false to clear.
   */
  void setOverflow(bool b) {
    setFlag(Overflow, b);
  }

  /**
   * @brief Check if the OutofRange flag is set.
   * @return True if OutofRange is set.
   */
  [[nodiscard]] bool hasOutofRange() const {
    return isFlagSet(OutofRange);
  }

  /**
   * @brief Set the OutofRange flag.
   * @param b True to set, false to clear.
   */
  void setOutofRange(bool b) {
    setFlag(OutofRange, b);
  }

  /**
   * @brief Check if the BadReference flag is set.
   * @return True if BadReference is set.
   */
  [[nodiscard]] bool hasBadReference() const {
    return isFlagSet(BadReference);
  }

  /**
   * @brief Set the BadReference flag.
   * @param b True to set, false to clear.
   */
  void setBadReference(bool b) {
    setFlag(BadReference, b);
  }

  /**
   * @brief Check if the Oscillatory flag is set.
   * @return True if Oscillatory is set.
   */
  [[nodiscard]] bool hasOscillatory() const {
    return isFlagSet(Oscillatory);
  }

  /**
   * @brief Set the Oscillatory flag.
   * @param b True to set, false to clear.
   */
  void setOscillatory(bool b) {
    setFlag(Oscillatory, b);
  }

  /**
   * @brief Check if the Failure flag is set.
   * @return True if Failure is set.
   */
  [[nodiscard]] bool hasFailure() const {
    return isFlagSet(Failure);
  }

  /**
   * @brief Set the Failure flag.
   * @param b True to set, false to clear.
   */
  void setFailure(bool b) {
    setFlag(Failure, b);
  }

  /**
   * @brief Check if the OldData flag is set.
   * @return True if OldData is set.
   */
  [[nodiscard]] bool hasOldData() const {
    return isFlagSet(OldData);
  }

  /**
   * @brief Set the OldData flag.
   * @param b True to set, false to clear.
   */
  void setOldData(bool b) {
    setFlag(OldData, b);
  }

  /**
   * @brief Check if the Inconsistent flag is set.
   * @return True if Inconsistent is set.
   */
  [[nodiscard]] bool hasInconsistent() const {
    return isFlagSet(Inconsistent);
  }

  /**
   * @brief Set the Inconsistent flag.
   * @param b True to set, false to clear.
   */
  void setInconsistent(bool b) {
    setFlag(Inconsistent, b);
  }

  /**
   * @brief Check if the Inaccurate flag is set.
   * @return True if Inaccurate is set.
   */
  [[nodiscard]] bool hasInaccurate() const {
    return isFlagSet(Inaccurate);
  }

  /**
   * @brief Set the Inaccurate flag.
   * @param b True to set, false to clear.
   */
  void setInaccurate(bool b) {
    setFlag(Inaccurate, b);
  }

  /**
   * @brief Check if the Test flag is set.
   * @return True if Test is set.
   */
  [[nodiscard]] bool hasTest() const {
    return isFlagSet(Test);
  }

  /**
   * @brief Set the Test flag.
   * @param b True to set, false to clear.
   */
  void setTest(bool b) {
    setFlag(Test, b);
  }

  /**
   * @brief Check if the OperatorBlocked flag is set.
   * @return True if OperatorBlocked is set.
   */
  [[nodiscard]] bool hasOperatorBlocked() const {
    return isFlagSet(OperatorBlocked);
  }

  /**
   * @brief Set the OperatorBlocked flag.
   * @param b True to set, false to clear.
   */
  void setOperatorBlocked(bool b) {
    setFlag(OperatorBlocked, b);
  }

  /**
   * @brief Read quality from a 16-bit value.
   * @param value The 16-bit value. Only bits 3-15 are used.
   */
  void fromUint16(uint16_t value) {
    m_value = static_cast<uint16_t>(value & AllFlagsMask);
  }

  /**
   * @brief Write quality to a 16-bit value.
   * @return The 16-bit value.
   */
  [[nodiscard]] uint16_t toUint16() const {
    return m_value;
  }

  /**
   * @brief Read quality from a 13-bit BitString (IEC 61850).
   *
   * In IEC 61850, BitString13 is represented as 2 octets.
   * Bits are mapped from MSB to LSB starting from octet 0.
   *
   * @param data Pointer to 2 octets of data.
   */
  void fromBitString13(const uint8_t data[2]) {
    m_value = static_cast<uint16_t>(((static_cast<uint16_t>(data[0]) << 8) | data[1]) & AllFlagsMask);
  }

  /**
   * @brief Write quality to a 13-bit BitString (IEC 61850).
   *
   * @param data Pointer to 2 octets where data will be written.
   */
  void toBitString13(uint8_t data[2]) const {
    data[0] = static_cast<uint8_t>((m_value >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(m_value & 0xFF);
  }

  /**
   * @brief Write quality to a 13-bit GOOSE BitString (same as goo_emitQuality).
   *
   * @param data Pointer to 2 octets where data will be written.
   */
  void toGooseBitString13(uint8_t data[2]) const {
    data[0] = static_cast<uint8_t>((m_value >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>((m_value & 0xFF) << 3);
  }

  /**
   * @brief Read quality from a 13-bit GOOSE BitString.
   *
   * @param data Pointer to 2 octets of data.
   */
  void fromGooseBitString13(const uint8_t data[2]) {
    m_value = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | (data[1] >> 3));
    m_value &= AllFlagsMask;
  }

  /**
   * @brief Equality operator.
   * @param other The other quality to compare with.
   * @return True if both qualities are equal.
   */
  bool operator==(const Quality& other) const {
    return m_value == other.m_value;
  }

  /**
   * @brief Inequality operator.
   * @param other The other quality to compare with.
   * @return True if both qualities are not equal.
   */
  bool operator!=(const Quality& other) const {
    return !(*this == other);
  }

  /**
   * @brief Clear all flags and optionally set validity.
   * @param v The validity to set (defaults to Good).
   */
  void clear(Validity v = Validity::Good) {
    m_value = static_cast<uint16_t>(v);
  }

 private:
  uint16_t m_value;
};

}  // namespace iec61850
