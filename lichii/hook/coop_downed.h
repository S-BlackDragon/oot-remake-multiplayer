// SPDX-License-Identifier: GPL-3.0-or-later
//
// The downed gate (oot-remake docs/CAIDO_REVIVIR.md 3.1, "A"): a third cave in
// Ocarina's main, on the call that starts the game over event, and the word
// the emulator writes to hold it shut. Only with LICHII_COOP_DOWNED=1; no
// emulator code here.
#pragma once

#include <array>
#include <cstdint>

namespace CoopDowned {

// main+0x4ae6bc is `bl 0x4aa890` in Link's update (main+0x4aaa80): it starts
// the game over event once his death is pending. It becomes `bl kCaveOffset`.
inline constexpr std::uint64_t kHookOffset = 0x4ae6bc;
inline constexpr std::uint32_t kHookOriginal = 0x97fff075;  // bl 0x4aa890
inline constexpr std::uint64_t kStartEvent = 0x4aa890;
inline constexpr std::uint64_t kCaveOffset = 0x3c46800;  // after the AI hold's table (it ends at 0x3c46610)
inline constexpr std::uint64_t kGateOffset = 0x3c46880;  // u32: 0 open, nonzero closed
inline constexpr std::uint64_t kGateEnd = kGateOffset + 4;

constexpr std::uint32_t HookWord() {
    return 0x94000000u |
           static_cast<std::uint32_t>(((kCaveOffset - kHookOffset) >> 2) & 0x3ffffff);
}

// coop_downed.s, assembled at kCaveOffset (the commands are in that file):
// adr x16, gate; ldr w16, [x16]; cbnz w16, +8; b 0x4aa890; mov w0, wzr; ret.
inline constexpr std::array<std::uint8_t, 24> kCave{
    0x10, 0x04, 0x00, 0x10, 0x10, 0x02, 0x40, 0xb9, 0x50, 0x00, 0x00, 0x35,
    0x21, 0x90, 0x21, 0x17, 0xe0, 0x03, 0x1f, 0x2a, 0xc0, 0x03, 0x5f, 0xd6,
};

} // namespace CoopDowned
