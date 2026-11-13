// The downed gate cave in Ocarina of Time's main (build e0f3ccfaf50065ae), at
// main+0x3c46800, after the AI hold's table (coop_hold.s, up to 0x3c46790).
// Patched in only with LICHII_COOP_DOWNED=1 (oot-remake docs/CAIDO_REVIVIR.md).
//
// Link's update (main+0x4aaa80) starts the game over event once his death is
// pending (bit 6 of this+0x1c34) and no event runs, every frame until it
// starts: at main+0x4ae6bc it calls `bl 0x4aa890` (start an event by name;
// x0 the event component, x1 the name) and goes on at 0x4afbd4 only when bit
// 0 of the answer is set; with 0 it skips the start, as when no death is
// pending. The patch makes that call `bl 0x3c46800`: when the u32 the
// emulator keeps at main+0x3c46880 is 0 (gate open) this tail-branches to
// 0x4aa890 with x0, x1 and x30 untouched, so the game's call returns straight
// to its caller; when it is nonzero (gate closed) it answers 0 and starts
// nothing, and the game asks again next frame.
// Only x16 is used (IP0, free across a call). The cave never stores: main's
// code is not writable by the game; the emulator writes the word.
//
// Bytes: coop_downed.h (kCave). To rebuild them (LLVM on Windows; zig's clang
// and lld from the ziglang Python package do the same):
//   clang --target=aarch64-none-elf -c coop_downed.s -o gate.o
//   ld.lld gate.o -o gate.elf --section-start=.text=0x3c46800 -e 0 --defsym=gate_word=0x3c46880 --defsym=start_event=0x4aa890
//   llvm-objcopy -O binary gate.elf gate.bin

    .text
    .globl _start
_start:
    adr   x16, gate_word
    ldr   w16, [x16]
    cbnz  w16, closed
    b     start_event
closed:
    mov   w0, wzr
    ret
