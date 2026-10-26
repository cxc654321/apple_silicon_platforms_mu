#!/usr/bin/env python3
"""
L1 固件结构校验：解析 UEFI FD 里的 Firmware Volume / FFS 文件。

不依赖 UEFITool，只用标准库。用来在拿到 .fd 之后确认：
  - 有多少个 FV、总大小是否合理
  - 每个 FV 的 header 校验和是否正确
  - FFS 文件是否完整（size 不越界、8 字节对齐）
  - 关键驱动 GUID 是否真的被打进去了

用法：
    python3 tools/verify_fw.py Build/MacBookAirMid2020Pkg-AARCH64/DEBUG_CLANGPDB/FV/XXX.fd
    python3 tools/verify_fw.py XXX.fd --list        # 列出所有 FFS 文件
    python3 tools/verify_fw.py XXX.fd --expect <guid> # 断言某个 GUID 在固件里
退出码 0 = 结构正常，1 = 有问题
"""
import struct
import sys

FV_SIG = b"_FVH"
FFS_HEADER_SIZE = 24
FFS_ALIGN = 8

FFS_TYPE_NAMES = {
    0x01: "RAW", 0x02: "FREEFORM", 0x03: "SECURITY_CORE", 0x04: "PEI_CORE",
    0x05: "DXE_CORE", 0x06: "PEIM", 0x07: "DRIVER", 0x08: "COMBINED_PEIM_DRIVER",
    0x09: "APPLICATION", 0x0A: "MM", 0x0B: "FIRMWARE_VOLUME_IMAGE",
    0x0C: "COMBINED_MM_DXE", 0x0D: "MM_CORE", 0x0E: "MM_STANDALONE",
    0x0F: "MM_CORE_STANDALONE", 0x10: "PAD",
}


def guid_str(b):
    d1, d2, d3 = struct.unpack_from("<IHH", b, 0)
    d4 = b[8:10].hex().upper()
    d5 = b[10:16].hex().upper()
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{d4}-{d5}"


def fv_checksum_ok(data, off, header_len):
    s = 0
    for i in range(0, header_len, 2):
        if off + i + 1 >= len(data):
            return False
        s += struct.unpack_from("<H", data, off + i)[0]
    return (s & 0xFFFF) == 0


def parse_ffs(data, start, end, verbose, problems):
    files = []
    pos = start
    while pos + FFS_HEADER_SIZE <= end:
        hdr = data[pos:pos + FFS_HEADER_SIZE]
        if hdr == b"\xff" * FFS_HEADER_SIZE or hdr == b"\x00" * FFS_HEADER_SIZE:
            break
        guid = guid_str(hdr[0:16])
        ftype = hdr[18]
        size = hdr[20] | (hdr[21] << 8) | (hdr[22] << 16)
        if size < FFS_HEADER_SIZE or pos + size > end:
            problems.append(f"FFS @0x{pos:X} 大小非法 ({size})")
            break
        files.append((guid, ftype, size))
        if verbose:
            print(f"    0x{pos:08X}  {guid}  {FFS_TYPE_NAMES.get(ftype, hex(ftype)):<22} {size:>8} B")
        nxt = pos + size
        pos = (nxt + FFS_ALIGN - 1) & ~(FFS_ALIGN - 1)
    return files


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    verbose = "--list" in sys.argv
    expect = []
    for i, a in enumerate(sys.argv):
        if a == "--expect" and i + 1 < len(sys.argv):
            expect.append(sys.argv[i + 1].upper())

    if not args:
        print(__doc__)
        return 2

    path = args[0]
    with open(path, "rb") as fh:
        data = fh.read()

    print(f"文件: {path}")
    print(f"大小: {len(data)} 字节 ({len(data)/1024/1024:.2f} MB)")

    problems, fvs, all_guids = [], [], set()
    off = 0
    while off < len(data) - 56:
        if data[off + 40:off + 44] != FV_SIG:
            off += 16
            continue
        fv_len = struct.unpack_from("<Q", data, off + 32)[0]
        attr = struct.unpack_from("<I", data, off + 44)[0]
        hdr_len = struct.unpack_from("<H", data, off + 48)[0]
        fs_guid = guid_str(data[off + 16:off + 32])

        if fv_len < 56 or off + fv_len > len(data):
            problems.append(f"FV @0x{off:X} 长度非法 ({fv_len})")
            off += 16
            continue

        csum_ok = fv_checksum_ok(data, off, hdr_len)
        if not csum_ok:
            problems.append(f"FV @0x{off:X} header 校验和错误")

        print(f"\nFV @0x{off:08X}  len={fv_len:>9} B  hdr={hdr_len:<4} "
              f"attr=0x{attr:08X}  校验和={'OK' if csum_ok else 'BAD'}")
        print(f"   文件系统 GUID: {fs_guid}")

        files = parse_ffs(data, off + hdr_len, off + fv_len, verbose, problems)
        for ftype in files:
            all_guids.add(ftype[0])
        kinds = {}
        for _, t, _ in files:
            kinds[FFS_TYPE_NAMES.get(t, hex(t))] = kinds.get(FFS_TYPE_NAMES.get(t, hex(t)), 0) + 1
        print(f"   FFS 文件 {len(files)} 个: " + ", ".join(f"{k}×{v}" for k, v in sorted(kinds.items())))
        fvs.append((off, fv_len))
        off += fv_len

    print(f"\n汇总: {len(fvs)} 个 FV，合计 {sum(l for _, l in fvs)} B")

    ok = True
    for g in expect:
        if g in all_guids:
            print(f"  [OK] 找到期望的 GUID {g}")
        else:
            print(f"  [FAIL] 固件里没有 GUID {g}")
            ok = False

    if problems:
        print("\n[FAIL] 结构问题:")
        for p in problems:
            print("  -", p)
        return 1

    print("\n[PASS] 固件结构完整")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
