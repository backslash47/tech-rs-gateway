// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

/** @brief Metadata constants for the emulated ST-505 module, not controller menu IDs.
 * Opaque compatibility fields retain their existing bytes without assigning
 * undocumented meanings. Network addresses and MAC-derived identity are supplied
 * by the gateway at runtime.
 */
namespace tech_rs_gateway::st505_profile {
namespace tag {
constexpr uint16_t VERSION = 0x0015;
constexpr uint16_t IDENTITY = 0x0014;
constexpr uint16_t COMPATIBILITY_0028 = 0x0028;
constexpr uint16_t MODULE_MODEL = 0x002a;
constexpr uint16_t COMPATIBILITY_001E = 0x001e;
constexpr uint16_t MAC_SUFFIX = 0x002b;
constexpr uint16_t DHCP_RELATED = 0x0001;
constexpr uint16_t IPV4_ADDRESS = 0x0002;
constexpr uint16_t SUBNET_MASK = 0x0003;
constexpr uint16_t GATEWAY = 0x0004;
constexpr uint16_t DNS = 0x0005;
constexpr uint16_t COMPATIBILITY_000A = 0x000a;
} // namespace tag

// Version bytes are a compatibility value, not the gateway software version.
inline constexpr std::array<uint8_t, 2> VERSION_BYTES{0x13, 0x12};
inline constexpr std::array<uint8_t, 4> COMPATIBILITY_0028_BYTES{6, 0, 0, 0};
// Module model 505 encoded as a little-endian uint32.
inline constexpr std::array<uint8_t, 4> MODULE_MODEL_BYTES{0xf9, 1, 0, 0};
inline constexpr std::array<uint8_t, 1> COMPATIBILITY_001E_BYTES{1};
inline constexpr std::array<uint8_t, 1> DHCP_RELATED_BYTES{1};
inline constexpr std::array<uint8_t, 1> COMPATIBILITY_000A_BYTES{1};

constexpr size_t MAC_BYTES = 6;
constexpr size_t IDENTITY_BYTES = 8;
constexpr size_t MAC_SUFFIX_OFFSET = 2;
constexpr size_t MAC_SUFFIX_BYTES = 4;
} // namespace tech_rs_gateway::st505_profile
