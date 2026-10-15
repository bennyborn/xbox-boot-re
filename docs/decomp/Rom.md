# Rom: from the flash dump to the unpacked kernel

How `bios_complex_4627_retail.bin` — the 1 MB "Complex 4627 Retail (1.0)" image,
a modified BIOS, not a stock dump — becomes the kernel that runs at
0x80010000.
The tool for it: `tools/rom/unpack.py`, checked in Ghidra with
`tools/ghidra/import_2bl.py` and `tools/ghidra/import_kernel.py`.
All values below are also in `out/rom/unpack.json` of a run.

## Addresses

- **ROM**: offsets refer to one 256 KiB bank. The last bank lies at
  0xFFFC0000, the end of the ROM therefore at 0x100000000
  ([xboxdevwiki: Flash ROM][fr]).
- **MCPX**: addresses 0xFFFFFE00–0xFFFFFFFF, offset in the 512-byte dump =
  address − 0xFFFFFE00 ([xboxdevwiki: MCPX ROM][mx]).
- **2BL**: addresses from 0x400000 = 2BL offset + 0x400000, as in the Ghidra
  project `bl2`. Only the preamble before paging is switched on runs at
  0x90000 + offset; it is cited with that address.
- **Kernel**: virtual addresses from the image base 0x80010000, see
  `Overview.md`.

## Sequence

```
rom_bank   = rom[-0x40000:]                       # all four banks equal
bl         = rc4(mcpx[0x1A5:0x1B5], rom_bank[0x39E00:0x3FE00])
assert u32(bl, 0x5FE4) == 0x7854794A
kernel_key = bl[0x8C:0x9C]
data_size, head_size, kernel_size = u32(bl,0x5FDC), u32(bl,0x5FE0), u32(bl,0x5FE8)
data_off   = 0x40000 - 0x6200 - data_size
kernel_off = data_off - kernel_size
check sha1(kernel_key + sha1(kernel_key, sizes and bytes...)) == bl[0x5FEC:0x6000]
packed     = rc4(kernel_key, rom_bank[kernel_off:data_off])
image      = lzx_chunks(packed, window = 0x20000)   # to 0x80010000
```

## 1. The ROM

The dump is 1 MiB in size and consists of four equal banks of 256 KiB
(measured, `unpack.json`: `rom_banks: 4`; so also
[xboxdevwiki: BIOS][bios]). SHA-256 of the dump:
`3d5fae186cb31b5a6331155e532ef63e493afd85d7af8576fb52c95d0ce0362c`.

Occupied ranges of a bank:

| Bank offset | Length | Content | Evidence |
|---|---|---|---|
| 0x00000 | 0xD33 | Head with start tables, checked along in the 2BL | 2BL 0x5FE0, 0x4007D0 |
| 0x057A2 | 0x33C5E | Kernel, encrypted and packed | 2BL 0x5FE8, 0x4007D0 |
| 0x39400 | 0xA00 | Data block, not encrypted | 2BL 0x5FDC, 0x4007D0 |
| 0x39E00 | 0x6000 | 2BL, encrypted | [xboxdevwiki: BIOS][bios]; MCPX loop |
| 0x3FE00 | 0x200 | covered by the MCPX | [xboxdevwiki: MCPX ROM][mx] |

What the data block contains and what the kernel needs it for is not
determined here. It lies unchanged in `out/rom/kernel_data.bin`.

## 2. MCPX: decrypting the 2BL

The MCPX dump `mcpx_1.0.bin` has the MD5 `d49c52a4102f6df7bcf8d0617ac475ed`,
which the [README of XboxBiosTool][xbt] gives for "MCPX v1.0", matching BIOS
versions below 4817.

The MCPX decrypts the 2BL with RC4. The key is 16 bytes at MCPX offset
0x1A5, the source is ROM 0xFFFF9E00, the destination 0x90000, the length
0x6000 ([xboxdevwiki: Boot Process][bp], section RC4). After that it checks
the signature and jumps (MCPX 0xFFFFFF81–0xFFFFFF92, looked up in the dump):

```
0xFFFFFF81  mov eax, [0x95FE4]
0xFFFFFF86  cmp eax, 0x7854794A
0xFFFFFF8B  jne 0xFFFFFF94          ; error path
0xFFFFFF8D  mov eax, [0x90000]
0xFFFFFF92  jmp eax
```

In the decrypted 2BL the value 0x7854794A stands at 0x5FE4 and the entry
point 0x9024C at 0x0000.

## 3. 2BL: preamble at 0x90000

| Address | What happens |
|---|---|
| 0x9024C–0x902B6 | cache off, set MTRR 0x200–0x20F and 0x2FF, as in [xboxdevwiki: Boot Process][bp] |
| 0x902C3–0x902D7 | DS, ES, SS = 0x10; ESP = 0x400000; FS, GS = 0 |
| 0x902DA–0x902E9 | copies 0x1800 dwords from 0x90000 to 0x400000 |
| 0x902EB–0x9035D | page directory at 0xF000: 4 MiB pages for 0–256 MiB twice (at 0 and 0x80000000), 0xC0000000 onto the directory itself, 0xFFC00000 onto the flash, 0xFD000000–0xFDFFFFFF without cache |
| 0x90373–0x9037F | MSR 0x277 = 0x0007010600070106 |
| 0x90386–0x903A1 | CR4 \|= 0x610, CR3 = 0xF000, CR0 \|= 0x80010020 (paging on) |
| 0x903A6–0x903B1 | ESP = 0x80400000, call of 0x40061D |

## 4. 2BL: main part 0x40061D

```
pci_write(0x80000880, 2)            ; hide the MCPX ROM
0x400553()
fill 0x80090000, 0x1800 dwords, 0xCCCCCCCC   ; erase the old copy of the 2BL
0x400980(0xFD000000)
0x400F14()
load_kernel(key = 0x40008C)         ; 0x4007D0, section 5
if not 0x4029D9(0x80010000): halt   ; check the kernel's head
0x403B00(0x40006C)                  ; hand over to the kernel
```

`halt` is 0x400789: it writes 0x01000000 to PCI 0x8000036C and stays in an
endless loop.

The 16 bytes at 0x40008C are the kernel key. They lie 0x20 behind 0x40006C,
the argument of the hand-over. That agrees with the description of a field
of three 16-byte keys whose third is for the kernel
([xboxdevwiki: Boot Process][bp], "Kernel decryption"). 0x400553, 0x400980,
0x400F14, 0x4029D9 and 0x403B00 have not been examined for this milestone.

## 5. 2BL: loading the kernel, 0x4007D0

The loader reads four fields at the end of the 2BL (in memory 0x80405Fxx):

| 2BL offset | Value | Meaning |
|---|---|---|
| 0x5FDC | 0xA00 | length of the data block |
| 0x5FE0 | 0xD33 | length of the ROM head checked along, from 0xFFF00000 |
| 0x5FE8 | 0x33C5E | length of the packed kernel |
| 0x5FEC | 20 bytes | check value |

Position in the ROM, from the pointer arithmetic at the start of 0x4007D0:
the data block ends 0x6200 bytes before the end of the ROM, that is directly
before the 2BL. The kernel ends directly before the data block. In the bank
that gives 0x39400 for the data block and 0x57A2 for the kernel. 0xFFF00000
is the start of the last 1 MiB window ([xboxdevwiki: Flash ROM][fr]) and so
bank offset 0.

**Check value.** 0x402C44 sets the five initial values 0x67452301,
0xEFCDAB89, 0x98BADCFE, 0x10325476 and 0xC3D2E1F0, that is SHA-1. 0x402C84
takes data in, 0x402DA4 finishes. The loader computes:

```
d1 = sha1(key16 ‖ u32(kernel_size) ‖ kernel_bytes_encrypted
          ‖ u32(data_size) ‖ data_bytes ‖ u32(head_size) ‖ rom_head)
d2 = sha1(key16 ‖ d1)
if d2 != bl[0x5FEC:0x6000]: halt
```

`unpack.py` recomputes this: `digest_ok: true`.

**Decrypting.** 0x40068D hands out memory from a running pointer at
0x403A10. The kernel is copied there, then 0x40079F decrypts it in place:
0x402B51 prepares RC4 with the 16-byte key, 0x402A04 produces the stream.
The state bytes i and j lie behind the 256-byte table as in the MCPX
(+0x100, +0x101).

**Unpacking** to 0x80010000 (0x4008F7: `push 0x80010000`), see section 6.

**Fix-up.** After that 0x400904–0x400909 adds the dword at 0x403A08 to the
dword at 0x80010030. The value at 0x403A08 is 0 in the image, and there is
no other place in the 2BL that contains 0x403A08 as an address. With this
BIOS the fix-up therefore changes nothing.

## 6. The pack format

**Framing** (0x4006A5): the packed kernel is a sequence of chunks. Each
begins with two 16-bit values, packed length and unpacked length, followed
by the packed bytes. The loop ends when the bytes read reach the length from
0x5FE8. The decoder is set up with 0x20000 and 0x8000 (local values in
0x4006A5, call of 0x400F6D). Measured: 20 chunks whose end falls exactly on
0x33C5E, unpacked 0x9B500 bytes together.

**Bit stream**: LZX after the public specification [MS-PATCH][patch],
section 2, with window 2^17 (0x20000), hence 34 position slots. Two places
differ from the text of the specification. Both are evidenced by the fact
that only so does the kernel unpack completely and with the same hash as
XboxBiosTool's (section 7):

1. There is no field "Extra Length" (2.6.6). A length of 257 is final. In
   the kernel 937 such matches occur (`unpack.json`:
   `matches_of_length_257`).
2. Aligned offset bits apply from 3 footer bits on, as 2.6.3 says (formatted
   offset > 15), not only from 4, as the pseudocode in 2.7 says. With the
   pseudocode the first block of this type in the kernel fails.

**E8 translation** (2.2.2): the head of the stream switches it on, the
translation size is 0xB71B00. In the 2BL, 0x4022C4 does the reverse
translation on the output copy of every chunk, and only if the switch at
state+0x2EC4 is set and the chunk counter at state+0x2ECC is below 0x8000.
That corresponds to the limit of 1 GiB in the specification.

## 7. Result and comparison

| File | Size | SHA-256 | XboxBiosTool v0.3.0.0 |
|---|---|---|---|
| `kernel.img` | 0x9B500 | `6bf7aecdbea49ed67a44214366520b56649f6b395b75ff346d7dd3d7115c407f` | equal (`krnl.img`) |
| `kernel.lzx` | 0x33C5E | `2c7483febca76d3fb517ace35bd70102530e9519d32870c98d110d66c2618e73` | equal (`krnl.bin`) |
| `kernel_data.bin` | 0xA00 | `6c0081278c381d16c78b138b5baefeb3cd68e08b7787d680040db1cac1e7d59e` | equal (`krnl_data.bin`) |
| `2bl.bin` | 0x6000 | `34390a1a0a91435c10a4d116b7d9942c65d6880e4ec1c3a09870c4281698a402` | differs in 0x5E80–0x5F7F, see below |

XboxBiosTool writes zeros into the 256 bytes at 2BL 0x5E80–0x5F7F.
`unpack.py` leaves the RC4 output there, that is what the MCPX writes to
0x90000 according to the wiki. No code in the 2BL contains an address from
this range.

In memory stands the version of `unpack.py`. Measured with
`python tools/xemu/bench.py --name c2_break2bl --break 0x9024C`: the run
stops at the entry point of the 2BL before its first instruction is executed
(EIP 0x0009024C according to GDB stub and snapshot). The 0x6000 bytes from
physical 0x90000 then have the SHA-256
`34390a1a0a91435c10a4d116b7d9942c65d6880e4ec1c3a09870c4281698a402`, equal to
`2bl.bin`. Against `bldr.bin` of XboxBiosTool exactly the 256 bytes at
0x5E80–0x5F7F differ. Up to this point only the MCPX code has worked, and
xemu executes that from the dump `mcpx_1.0.bin`.

The image is a valid PE: "MZ" and "PE" in their places, ImageBase
0x80010000, SizeOfImage 0x9B500 equal to the unpacked length. The sections
are in `Overview.md`.

[bios]: https://xboxdevwiki.net/BIOS
[bp]: https://xboxdevwiki.net/Boot_Process
[mx]: https://xboxdevwiki.net/MCPX_ROM
[fr]: https://xboxdevwiki.net/Flash_ROM
[xbt]: https://github.com/tommojphillips/XboxBiosTool/blob/master/README.md
[patch]: https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/cc78752a-b4af-4eee-88cb-01f4d8a4c2bf
