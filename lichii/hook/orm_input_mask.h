// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: pad buttons the game must not see for now (oot-remake
// docs/COMUNICACION.md). While the quick-chat button is held, the d-pad
// belongs to the quick messages (orm_quick_chat.h), not to the game's
// d-pad items and ocarina. The hook sets the mask once a frame; the patched
// NPad::RequestPadStateUpdate (patch 0101-orm-quick-chat-input, hid_core) takes it off
// what the guest reads. The hook's own reads (GetNpadButtons) still see every
// button, scripted ones included. Header only, any thread; standard C++.
#pragma once

#include <atomic>
#include <cstdint>

namespace OrmInputMask {

// NpadButton bits (hid_core/hid_types.h) the guest does not get.
inline std::atomic<std::uint64_t> g_guest{0};

inline void Set(std::uint64_t bits) {
    g_guest.store(bits, std::memory_order_relaxed);
}

inline std::uint64_t Guest() {
    return g_guest.load(std::memory_order_relaxed);
}

} // namespace OrmInputMask
