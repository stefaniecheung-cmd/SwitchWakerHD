# -*- coding: utf-8 -*-
"""Patch an NRO's homebrew assets (ASET): icon, nacp name/author/displayVersion.

The ASET section sits at file offset {NroHeader.size} (0x18). It contains the
256x256 JPEG icon and the 0x4000-byte NACP used by hbmenu. devkitPro's default
icon (a question mark) and an empty displayVersion make hbmenu show "?" and
"v0.1.0"; this tool replaces them in place.

Usage:
  python patch_nro_meta.py --nro wwhd.nro [--icon icon.jpg]
      [--name "Wind Waker HD"] [--author "centollOS/SwitchWakerHD"]
      [--version 1.1] [--out patched.nro]   # default: in place
"""
import argparse
import struct
import sys


def patch_nacp(nacp, name=None, author=None, version=None):
    """NACP: 16 entries x 0x300 (name @+0x000, author @+0x200); displayVersion @0x2020."""
    nacp = bytearray(nacp)
    assert len(nacp) == 0x4000
    if name:
        for k in range(16):
            off = k * 0x300
            nacp[off:off + 0x100] = name.encode("utf-8").ljust(0x100, b"\0")
    if author:
        for k in range(16):
            off = k * 0x300 + 0x200
            nacp[off:off + 0x100] = author.encode("utf-8").ljust(0x100, b"\0")
    if version:
        # NACP displayVersion is at 0x3060 (after the 16x0x300 title entries;
        # what NSGame Manager / hbmenu show). 0x2020 is unused space: clear it
        # in case an older run of this tool wrote there by mistake.
        nacp[0x2020:0x2030] = b"\0" * 0x10
        nacp[0x3060:0x3070] = version.encode("ascii").ljust(0x10, b"\0")
    return bytes(nacp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nro", required=True)
    ap.add_argument("--icon", help="256x256 JPEG (the NRO icon hbmenu shows)")
    ap.add_argument("--name")
    ap.add_argument("--author")
    ap.add_argument("--version")
    ap.add_argument("--out", help="output path (default: in place)")
    a = ap.parse_args()

    d = bytearray(open(a.nro, "rb").read())
    magic = d[0x10:0x14]
    assert magic == b"NRO0", "not an NRO: %r" % magic
    aset_start = struct.unpack_from("<I", d, 0x18)[0]  # NroHeader.size == ASET offset
    assert d[aset_start:aset_start + 4] == b"ASET", "no ASET at 0x%x" % aset_start

    ioff, isz, noff, nsz, roff, rsz = struct.unpack_from("<QQQQQQ", d, aset_start + 8)
    nacp = bytes(d[aset_start + noff:aset_start + noff + nsz])
    icon = open(a.icon, "rb").read() if a.icon else bytes(d[aset_start + ioff:aset_start + ioff + isz])
    if a.icon:
        assert icon[:3] == b"\xff\xd8\xff", "icon must be a JPEG"
        print("icon: %d bytes (was %d)" % (len(icon), isz))

    nacp = patch_nacp(nacp, a.name, a.author, a.version)
    if a.name:
        print("name:    %r" % a.name)
    if a.author:
        print("author:  %r" % a.author)
    if a.version:
        print("version: %r" % a.version)

    # rebuild the ASET: header (0x38) + icon + nacp; romfs stays empty
    new_aset = bytearray(b"ASET\0\0\0\0")
    new_aset += struct.pack("<QQ", 0x38, len(icon))                # icon
    new_aset += struct.pack("<QQ", 0x38 + len(icon), len(nacp))    # nacp
    new_aset += struct.pack("<QQ", 0, 0)                           # romfs: empty
    new_aset += icon + nacp

    out = bytes(d[:aset_start]) + bytes(new_aset)  # NroHeader.size still points at ASET
    dst = a.out or a.nro
    open(dst, "wb").write(out)
    print("written: %s (%d bytes, was %d)" % (dst, len(out), len(d)))


if __name__ == "__main__":
    sys.exit(main())
