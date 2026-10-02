#pragma once

#include <array>
#include <cstdint>

// Trust root for .driftpkg addon packages. The matching Ed25519 private key lives in the addon
// repo's gitignored drift-addons/.secrets/addon-signing.key (see packer/config.py) and never
// ships. Rotating it means shipping a new binary, so treat this array as an ABI.

namespace drift::addon {

inline constexpr std::array<std::uint8_t, 32> kSigningPublicKey = {
    0x99, 0x28, 0x9f, 0xca, 0x0d, 0xa5, 0x62, 0x17, 0x63, 0x82, 0x6e,
    0xd6, 0x4e, 0xfd, 0xaf, 0x1b, 0xd9, 0x13, 0x40, 0xdd, 0x96, 0x67,
    0x73, 0x9b, 0x19, 0xdb, 0x95, 0x03, 0xe9, 0x94, 0x00, 0x7f,
};

} // namespace drift::addon
