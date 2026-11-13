#pragma once
// The game's own flag store (gmd::GameDataMgr), read and written over guest
// memory. Layout and functions: docs/flags.md. Bool, Int, UInt and (opted in)
// Enum flags by name hash, and the 64-bit-key flags
// (chests and other placed actors, keyed by the placement's 64-bit hash).
//
// Writes go into the game's own write queues, as the game's setters do, so
// the game applies them on its next GameData update with its own bookkeeping
// (saved copy, save-file mask). Reads see a queued write that is not applied
// yet, as the game's 0x20fd90 does.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/game_memory.hpp"

namespace coop::host {

// Guest memory that can also be written. The emulator implements it.
class WritableGameMemory : public GameMemory {
 public:
  // Copies `data` to guest address `address`. False when any byte cannot be
  // written. Must not throw.
  virtual bool write(std::uint64_t address, std::span<const std::uint8_t> data) = 0;
  // Atomically: when the u32 at `address` equals `expected`, stores
  // `desired`. Returns the value that was there (equal to `expected` on
  // success), nothing when it cannot be accessed. The game's threads use
  // AArch64 CAS on the same words, so this must be atomic against them (or
  // run while the guest is paused).
  virtual std::optional<std::uint32_t> compare_exchange_u32(std::uint64_t address,
                                                            std::uint32_t expected,
                                                            std::uint32_t desired) = 0;
};

// Reads and writes flags in the running game. Holds no state: every call
// follows the manager pointer again, so it is safe across scene changes and
// before the GameData module is up (calls then fail).
class GameFlags {
 public:
  explicit GameFlags(GameMemory& memory) : memory_(memory) {}

  // The flag's value, or nothing when the manager is not up, the hash is not
  // in that table, or memory cannot be read. Hash 0 is never a flag.
  std::optional<bool> get_bool(std::uint32_t hash);
  std::optional<std::int32_t> get_int(std::uint32_t hash);
  // A UInt flag (type 20; 0x57a4e0 reads it, 0x57af30 sets it). A hash only in
  // the game's fallback UInt table (M + 0x800, not read) is not found.
  std::optional<std::uint32_t> get_uint(std::uint32_t hash);
  // An Enum flag (type 6, M + 0x250; 0x7e73d0 reads it, 0x3e0980 sets it):
  // the u32 it holds, the Murmur3 hash of the value's name (docs/flags.md,
  // "Enum flags"). Not checked against the flag's set here; GameFlagStore
  // does that.
  std::optional<std::uint32_t> get_enum(std::uint32_t hash);
  // The values an Enum flag may take, as the game lists them (their name
  // hashes, in GameDataList order): the entry's count at +0x28 and array at
  // +0x30. Nothing when the flag is not found or the list looks wrong.
  std::optional<std::vector<std::uint32_t>> enum_values(std::uint32_t hash);
  // A 64-bit-key flag (Bool64bitKey). A key not in the table is nothing,
  // not false: the table holds every key the game knows.
  std::optional<bool> get_key64(std::uint64_t key);
  // Element `index` of a BoolArray flag. Read only.
  std::optional<bool> get_bool_array(std::uint32_t hash, std::uint32_t index);
  // A String64 flag (for example PlayerStatus.EquipWeapon, an actor name).
  // Read only, from the table as 0x3758110 reads it; a write still in the
  // game's queue is not seen until the game applies it. Nothing when the
  // text is not 0-terminated within its buffer or not printable ASCII.
  std::optional<std::string> get_string64(std::uint32_t hash);

  // Queues a write as the game's setter does. False when the flag is
  // unknown, the queue is full, the game applied the queue while this was
  // being written (try again next frame), or memory fails.
  static bool set_bool(WritableGameMemory& memory, std::uint32_t hash, bool value);
  static bool set_int(WritableGameMemory& memory, std::uint32_t hash, std::int32_t value);
  static bool set_uint(WritableGameMemory& memory, std::uint32_t hash, std::uint32_t value);
  // Queues an Enum value (the value name's hash) as the game's Enum setter
  // 0x3e0980 does: an 8-byte slot {value, hash} in the Enum table's queue.
  static bool set_enum(WritableGameMemory& memory, std::uint32_t hash, std::uint32_t value);
  static bool set_key64(WritableGameMemory& memory, std::uint64_t key, bool value);

  // Test only (tools/autotest warp): writes a String64 flag's text straight
  // into its buffer, 0-terminated, without the game's queue, so the live
  // value changes at once. False when the flag is unknown or the text does
  // not fit (at most 63 bytes, printable ASCII).
  static bool set_string64_now(WritableGameMemory& memory, std::uint32_t hash,
                               std::string_view text);

  // Which typed table (the save type ids, 0 to 25) holds `hash`, for finding
  // a flag's type. Nothing when none does.
  std::optional<int> find_type(std::uint32_t hash);

  // The sizes (slots) of the write queues our sets use: Bool, Int, UInt, Enum
  // and 64-bit keys. Half of each is ours (issue 175); a queue of fewer than
  // 2 slots gets no write of ours. Nothing before the manager is up.
  struct QueueSizes {
    std::uint32_t boolean = 0, int32 = 0, uint32 = 0, enumeration = 0, key64 = 0;
  };
  std::optional<QueueSizes> queue_sizes();

  // Where the live value of a flag is, for writes that go straight into it
  // without the game's queue (the saved copy keeps the old value, so a save
  // does not keep them): a Bool's u8 (entry +0x16), an Int's s32 (+0x1c), an
  // Enum's u32 (type 6, M + 0x250, entries of 0x38, value at +0x1c: the
  // Murmur3 hash of the value's name; read from the game's Enum getter
  // main+0x7e73d0, oot-remake docs/CAIDO_REVIVIR.md P2). Nothing when the
  // flag is not in that table. ORM "downed, not dead" (coop/host/downed.hpp).
  enum class Live : std::uint8_t { bool8, int32, enum32 };
  std::optional<std::uint64_t> live_address(Live kind, std::uint32_t hash);

 private:
  GameMemory& memory_;
};

// FlagStore over the game's tables, for FlagWatcher. A named key with index
// 0: a Bool flag (kind boolean) or else an Int flag (kind int32). Index > 0:
// an element of a BoolArray flag, read only. A key64 key: a Bool64bitKey
// flag (kind boolean) in table 0, the game's one table; any other table is
// unknown.
//
// Enum flags only when opted in (`enums`, from a list's enum: lines; null or
// empty by default, which is today's store): a named key with index 0 whose
// hash `enums` holds is an Enum flag, kind uint32, the value's hash. A value
// outside the flag's set is refused on write and reads as unknown, so a
// misread sends nothing instead of garbage. A story step (EnumFlags::steps)
// never goes back: a write of a value earlier in the list than the one the
// game holds is not written and reports success, so FlagWatcher takes it as
// applied, sees the game's later value on its next read and sends that on.
// `enums` must outlive the store.
class GameFlagStore final : public FlagStore {
 public:
  explicit GameFlagStore(WritableGameMemory& memory, const EnumFlags* enums = nullptr)
      : memory_(memory), enums_(enums) {}
  std::optional<protocol::FlagEntry> read(FlagKey key) override;
  bool write(const protocol::FlagEntry& entry) override;
  // The queue this flag's writes go to has half its slots taken or more.
  bool busy(FlagKey key) override;

 private:
  WritableGameMemory& memory_;
  const EnumFlags* enums_;
};

}  // namespace coop::host
