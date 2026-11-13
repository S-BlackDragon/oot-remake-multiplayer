"""Read and change the remake's saved game (slot_NN/progress.sav) offline, in place.

Layout (little-endian; daurosk's tools/autotest/save_flags.py reads the same): magic 0x01020304, a version word,
the pool offset, zeros to 0x20, then a table of 8-byte records up to the pool. A record {0, t} opens type t (0 to
0x20 in order, every one present); any other record is {flag hash, value}. The value is the flag itself for the
inline types (Bool, Int, Enum = the murmur3 hash of the value's name, UInt, Float) and an offset into the pool for
the others (String32 = 32 bytes, String64 = 64 bytes, Vector3 = three floats). The record after {0, 0x20} is
{hash, offset of the 64-bit keys}: the keys that are true, as u64, ascending, ended by a 0 u64, at the end of the
file. There is no checksum.

Type ids are the save's own (the order of TotK's save format, which this one follows); GameDataList's type names map
onto them in GDL_TYPE. Nothing here knows any flag name: names are hashed by the caller (murmur3_32).

Changes keep the file's shape: every value stays where it is, a string keeps its buffer; only the 64-bit key list
may grow or shrink, and it is the last thing in the file.
"""
import struct

MAGIC = 0x01020304
TABLE = 0x20
END = 0x20
M32 = 0xFFFFFFFF

# The save's type ids (only the ones this tool reads or writes are named).
BOOL, INT, FLOAT, ENUM, VECTOR3, STRING32, STRING64, UINT = 0, 2, 4, 6, 10, 14, 16, 20
INLINE = {BOOL, INT, FLOAT, ENUM, UINT}
STRING_SIZE = {STRING32: 32, STRING64: 64}
TYPE_NAMES = {BOOL: "bool", INT: "int", FLOAT: "float", ENUM: "enum", VECTOR3: "vector3", STRING32: "string32",
              STRING64: "string64", UINT: "uint"}
# GameDataList type name -> save type id (for checking a name against the romfs).
GDL_TYPE = {"Bool": BOOL, "Int": INT, "Float": FLOAT, "Enum": ENUM, "Vector3": VECTOR3, "String32": STRING32,
            "String64": STRING64, "UInt": UINT}


class SaveError(ValueError):
    pass


def murmur3_32(text, seed=0):
    """MurmurHash3_x86_32, seed 0: the game's flag and enum value hash (daurosk protocol::flag_hash)."""
    data = text.encode("utf-8") if isinstance(text, str) else bytes(text)
    c1, c2 = 0xCC9E2D51, 0x1B873593
    h = seed & M32
    n = len(data)
    body = n - (n & 3)

    def rotl(x, r):
        return ((x << r) | (x >> (32 - r))) & M32

    for i in range(0, body, 4):
        k = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16) | (data[i + 3] << 24)
        k = (k * c1) & M32
        k = rotl(k, 15)
        k = (k * c2) & M32
        h ^= k
        h = rotl(h, 13)
        h = (h * 5 + 0xE6546B64) & M32
    k = 0
    tail = n & 3
    if tail == 3:
        k ^= data[body + 2] << 16
    if tail >= 2:
        k ^= data[body + 1] << 8
    if tail >= 1:
        k ^= data[body]
        k = (k * c1) & M32
        k = rotl(k, 15)
        k = (k * c2) & M32
        h ^= k
    h ^= n
    h ^= h >> 16
    h = (h * 0x85EBCA6B) & M32
    h ^= h >> 13
    h = (h * 0xC2B2AE35) & M32
    h ^= h >> 16
    return h


class Save:
    """A parsed progress.sav. `data` is a bytearray; `slots[(type, hash)]` is the offset of the record's value word;
    `keys` the sorted list of 64-bit keys that are true; `keys_at` where that list starts."""

    def __init__(self, raw):
        self.data = bytearray(raw)
        self._parse()

    def _parse(self):
        d = self.data
        if len(d) < TABLE or struct.unpack_from("<I", d, 0)[0] != MAGIC:
            raise SaveError("not a saved game (magic)")
        pool = struct.unpack_from("<I", d, 8)[0]
        if not TABLE <= pool <= len(d):
            raise SaveError("bad pool offset")
        self.pool = pool
        slots, kind, at = {}, None, TABLE
        while at + 8 <= pool:
            first, second = struct.unpack_from("<II", d, at)
            if first == 0:
                expected = 0 if kind is None else kind + 1
                if second != expected:
                    raise SaveError(f"type {second:#x} where {expected:#x} was due")
                if second == END:
                    if at + 16 > pool:
                        raise SaveError("the end record's pair runs into the pool")
                    self.end_pair_at = at + 8
                    self.slots = slots
                    self._parse_keys(struct.unpack_from("<I", d, at + 12)[0])
                    self._check_pool_values()
                    return
                kind = second
            elif kind is None:
                raise SaveError("an entry before the first type")
            else:
                if (kind, first) in slots:
                    raise SaveError(f"{first:#x} twice in type {kind}")
                slots[(kind, first)] = at + 4
            at += 8
        raise SaveError("no end record before the pool")

    def _parse_keys(self, off):
        d = self.data
        if not self.pool <= off <= len(d) - 8:
            raise SaveError("64-bit key list outside the pool")
        keys, at = [], off
        while True:
            if at + 8 > len(d):
                raise SaveError("64-bit key list not ended by 0")
            k = struct.unpack_from("<Q", d, at)[0]
            if k == 0:
                break
            keys.append(k)
            at += 8
        if keys != sorted(set(keys)):
            raise SaveError("64-bit keys not ascending")
        if at + 8 != len(d):
            raise SaveError("bytes after the 64-bit key list (the list must end the file)")
        self.keys_at = off
        self.keys = keys

    def _check_pool_values(self):
        """Strings and vectors this tool may write must have their own buffer inside the pool."""
        seen = {}
        for (kind, h), at in self.slots.items():
            size = STRING_SIZE.get(kind) or (12 if kind == VECTOR3 else 0)
            if not size:
                continue
            off = struct.unpack_from("<I", self.data, at)[0]
            if not (self.pool <= off and off + size <= self.keys_at):
                raise SaveError(f"{h:#x}: value outside the pool")
            if off in seen:
                raise SaveError(f"{h:#x} and {seen[off]:#x} share one buffer")
            seen[off] = h

    # ---------------------------------------------------------------- reads

    def has(self, kind, h):
        return (kind, h) in self.slots

    def _at(self, kind, h):
        at = self.slots.get((kind, h))
        if at is None:
            raise SaveError(f"{TYPE_NAMES.get(kind, kind)} {h:#010x}: not in this save")
        return at

    def get(self, kind, h):
        """The value: int for Bool (0/1), Int (signed), UInt and Enum (a hash); float; str; (x, y, z)."""
        at = self._at(kind, h)
        d = self.data
        if kind == BOOL:
            return 1 if d[at] else 0
        if kind == INT:
            return struct.unpack_from("<i", d, at)[0]
        if kind in (UINT, ENUM):
            return struct.unpack_from("<I", d, at)[0]
        if kind == FLOAT:
            return struct.unpack_from("<f", d, at)[0]
        off = struct.unpack_from("<I", d, at)[0]
        if kind in STRING_SIZE:
            raw = bytes(d[off:off + STRING_SIZE[kind]])
            return raw.split(b"\0", 1)[0].decode("ascii", "replace")
        if kind == VECTOR3:
            return struct.unpack_from("<3f", d, off)
        raise SaveError(f"type {kind}: not read by this tool")

    def get_key(self, key):
        return 1 if key in self.keys else 0

    # ---------------------------------------------------------------- writes

    def set(self, kind, h, value):
        """Writes one value in place. Returns the byte range written (start, end)."""
        at = self._at(kind, h)
        d = self.data
        if kind == BOOL:
            d[at] = 1 if value else 0  # the low byte holds the value (save_flags.py keeps the other three)
            return at, at + 1
        if kind == INT:
            if not -2**31 <= int(value) < 2**31:
                raise SaveError(f"{h:#x}: {value} does not fit an Int")
            struct.pack_into("<i", d, at, int(value))
            return at, at + 4
        if kind in (UINT, ENUM):
            if not 0 <= int(value) <= M32:
                raise SaveError(f"{h:#x}: {value} does not fit a UInt")
            struct.pack_into("<I", d, at, int(value))
            return at, at + 4
        if kind == FLOAT:
            struct.pack_into("<f", d, at, float(value))
            return at, at + 4
        off = struct.unpack_from("<I", d, at)[0]
        if kind in STRING_SIZE:
            size = STRING_SIZE[kind]
            raw = str(value).encode("ascii")
            if len(raw) >= size:
                raise SaveError(f"{h:#x}: {value!r} is longer than {size - 1} characters")
            d[off:off + size] = raw + b"\0" * (size - len(raw))
            return off, off + size
        if kind == VECTOR3:
            x, y, z = (float(v) for v in value)
            struct.pack_into("<3f", d, off, x, y, z)
            return off, off + 12
        raise SaveError(f"type {kind}: not written by this tool")

    def set_key(self, key, value):
        """Adds (value 1) or removes (0) a 64-bit key; the list stays ascending and ends the file."""
        if not 0 < key < 2**64:
            raise SaveError(f"64-bit key {key:#x} out of range")
        keys = set(self.keys)
        keys.add(key) if value else keys.discard(key)
        self.keys = sorted(keys)
        blob = b"".join(struct.pack("<Q", k) for k in self.keys) + b"\0" * 8
        del self.data[self.keys_at:]
        self.data += blob

    def to_bytes(self):
        out = bytes(self.data)
        Save(out)  # still a whole save
        return out


def read(path):
    with open(path, "rb") as f:
        return Save(f.read())
