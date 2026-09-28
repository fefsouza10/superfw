#!/usr/bin/env python3
# Builds the soft-patch demo: a tiny homebrew GBA ROM plus IPS, UPS and BPS
# patches for it. Every patch is checked with the reference appliers below.
#
# Usage: mkdemo.py OUTDIR   (needs arm-none-eabi-gcc and Pillow)

import os, struct, subprocess, sys, zlib
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROM_SIZE = 256 * 1024
BIG_SIZE = ROM_SIZE + 64 * 1024
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

def make_font(path):
  font = ImageFont.truetype(FONT, 13)
  rows = []
  for c in range(32, 127):
    im = Image.new("L", (8, 16), 0)
    ImageDraw.Draw(im).text((0, 0), chr(c), font=font, fill=255)
    glyph = []
    for y in range(16):
      b = 0
      for x in range(8):
        if im.getpixel((x, y)) > 110:
          b |= 0x80 >> x
      glyph.append(b)
    rows.append("  {" + ",".join("0x%02x" % v for v in glyph) + "},")
  with open(path, "w") as f:
    f.write("#include <stdint.h>\nconst uint8_t font8x16[95][16] = {\n%s\n};\n" % "\n".join(rows))

def build_rom(tmp):
  make_font(os.path.join(tmp, "font.c"))
  elf = os.path.join(tmp, "demo.elf")
  subprocess.check_call([
    "arm-none-eabi-gcc", "-mcpu=arm7tdmi", "-mthumb", "-mthumb-interwork", "-O2",
    "-ffreestanding", "-nostdlib", "-T", os.path.join(HERE, "demo.ld"),
    os.path.join(HERE, "crt0.S"), os.path.join(HERE, "demo.c"),
    os.path.join(tmp, "font.c"), "-o", elf, "-lgcc"])
  binf = os.path.join(tmp, "demo.bin")
  subprocess.check_call(["arm-none-eabi-objcopy", "-O", "binary", elf, binf])
  rom = bytearray(open(binf, "rb").read())

  # Header: logo from the SuperFW ROM, title, game code, checksum.
  fw = open(os.path.join(HERE, "..", "..", "superfw.gba"), "rb").read()
  rom[0x04:0xA0] = fw[0x04:0xA0]
  rom[0xA0:0xAC] = b"PATCH DEMO\0\0"
  rom[0xAC:0xB0] = b"ZPDE"
  rom[0xB0:0xB2] = b"01"
  rom[0xB2] = 0x96
  rom[0xB3:0xBD] = bytes(10)
  rom[0xBD] = (-(0x19 + sum(rom[0xA0:0xBD]))) & 0xFF

  # Pad with deterministic noise (gives the BPS patch something to copy).
  seed = 12345
  while len(rom) < ROM_SIZE:
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
    rom.append((seed >> 16) & 0xFF)
  return bytes(rom)

def with_cfg(rom, bg, fg, lines):
  off = rom.index(b"DEMOCFG!")
  data = bytearray(rom)
  cfg = struct.pack("<HH", bg, fg) + b"".join(l.encode().ljust(16, b"\0") for l in lines)
  data[off + 8: off + 8 + len(cfg)] = cfg
  return data

def rgb(r, g, b):
  return r | (g << 5) | (b << 10)

def grow(data, text):
  blk = bytearray(BIG_SIZE - len(data))
  msg = b"BIGROM!!" + text.encode() + b"\0"
  blk[:len(msg)] = msg
  return bytes(data) + bytes(blk)

# ---- IPS ----
def ips_make(src, dst):
  out = bytearray(b"PATCH")
  i = 0
  while i < len(dst):
    if i < len(src) and src[i] == dst[i]:
      i += 1
      continue
    j = i
    while j < len(dst) and j - i < 0xFFFF and not (j < len(src) and src[j] == dst[j]):
      j += 1
    # Split the run: long repeats of one byte become RLE records.
    k = i
    while k < j:
      r = k
      while r < j and dst[r] == dst[k]:
        r += 1
      if r - k < 16:
        r = k
        while r < j and not (r + 16 <= j and dst[r:r+16].count(dst[r]) == 16):
          r += 1
        r = max(r, k + 1)
        if k == 0x454F46:
          raise Exception("EOF offset")
        out += struct.pack(">I", k)[1:] + struct.pack(">H", r - k) + dst[k:r]
      else:
        out += struct.pack(">I", k)[1:] + b"\0\0" + struct.pack(">HB", r - k, dst[k])
      k = r
    i = j
  return bytes(out + b"EOF")

def ips_apply(src, p):
  assert p[:5] == b"PATCH"
  out = bytearray(src)
  i = 5
  while True:
    off = int.from_bytes(p[i:i+3], "big"); i += 3
    if off == 0x454F46:
      return bytes(out)
    sz = int.from_bytes(p[i:i+2], "big"); i += 2
    if sz:
      data = p[i:i+sz]; i += sz
    else:
      sz = int.from_bytes(p[i:i+2], "big"); data = bytes([p[i+2]]) * sz; i += 3
    if off + sz > len(out):
      out += bytes(off + sz - len(out))
    out[off:off+sz] = data

# ---- VLQ (UPS/BPS) ----
def vlq(n):
  out = bytearray()
  while True:
    x = n & 0x7F
    n >>= 7
    if n == 0:
      out.append(0x80 | x)
      return bytes(out)
    out.append(x)
    n -= 1

def rvlq(p, i):
  data, shift = 0, 1
  while True:
    x = p[i]; i += 1
    data += (x & 0x7F) * shift
    if x & 0x80:
      return data, i
    shift <<= 7
    data += shift

def crcs(src, dst, body):
  body += struct.pack("<II", zlib.crc32(src), zlib.crc32(dst))
  return body + struct.pack("<I", zlib.crc32(body))

# ---- UPS ----
def ups_make(src, dst):
  out = bytearray(b"UPS1") + vlq(len(src)) + vlq(len(dst))
  last = 0
  i = 0
  n = max(len(src), len(dst))
  byte = lambda b, k: b[k] if k < len(b) else 0
  while i < n:
    if byte(src, i) == byte(dst, i):
      i += 1
      continue
    out += vlq(i - last)
    while i < n and byte(src, i) != byte(dst, i):
      out.append(byte(src, i) ^ byte(dst, i))
      i += 1
    out.append(0)
    i += 1
    last = i
  return crcs(src, dst, out)

def ups_apply(src, p):
  assert p[:4] == b"UPS1"
  ssz, i = rvlq(p, 4)
  tsz, i = rvlq(p, i)
  assert ssz == len(src)
  out = bytearray(src[:tsz].ljust(tsz, b"\0"))
  off = 0
  while i < len(p) - 12:
    skip, i = rvlq(p, i)
    off += skip
    while True:
      x = p[i]; i += 1
      if x == 0:
        off += 1
        break
      if off < tsz:
        out[off] ^= x
      off += 1
  return bytes(out)

# ---- BPS ----
def bps_apply(src, p):
  assert p[:4] == b"BPS1"
  ssz, i = rvlq(p, 4)
  tsz, i = rvlq(p, i)
  msz, i = rvlq(p, i)
  assert ssz == len(src)
  i += msz
  out = bytearray()
  srel = trel = 0
  while i < len(p) - 12:
    d, i = rvlq(p, i)
    cmd, ln = d & 3, (d >> 2) + 1
    if cmd == 0:
      out += src[len(out):len(out)+ln]
    elif cmd == 1:
      out += p[i:i+ln]; i += ln
    else:
      o, i = rvlq(p, i)
      o = -(o >> 1) if o & 1 else (o >> 1)
      if cmd == 2:
        srel += o
        out += src[srel:srel+ln]; srel += ln
      else:
        trel += o
        for _ in range(ln):
          out.append(out[trel]); trel += 1
  assert len(out) == tsz
  return bytes(out)

class BpsWriter:
  def __init__(self, src, tsz):
    self.out = bytearray(b"BPS1") + vlq(len(src)) + vlq(tsz) + vlq(0)
    self.srel = self.trel = 0
  def cmd(self, c, ln):
    self.out += vlq(((ln - 1) << 2) | c)
  def rel(self, delta):
    self.out += vlq((abs(delta) << 1) | (1 if delta < 0 else 0))
  def source_read(self, ln): self.cmd(0, ln)
  def target_read(self, data):
    self.cmd(1, len(data)); self.out += data
  def source_copy(self, off, ln):
    self.cmd(2, ln); self.rel(off - self.srel); self.srel = off + ln
  def target_copy(self, off, ln):
    self.cmd(3, ln); self.rel(off - self.trel); self.trel = off + ln

def bps_make(src, cfg_rom, tail_text):
  # Target: the config change, two 32KB blocks swapped, and a grown tail made
  # of a magic string, a copy of early ROM data and a run built by TargetCopy.
  cfg_off = src.index(b"DEMOCFG!")
  w = BpsWriter(src, BIG_SIZE)
  w.source_read(cfg_off)
  w.target_read(bytes(cfg_rom[cfg_off:cfg_off + 60]))
  w.source_read(0x20000 - cfg_off - 60)
  w.source_copy(0x30000, 0x8000)          # Forward copy (still in SDRAM)
  w.source_read(0x8000)
  w.source_copy(0x20000, 0x8000)          # Backward copy (read from the SD)
  w.source_read(ROM_SIZE - 0x38000)
  msg = b"BIGROM!!" + tail_text.encode() + b"\0"
  w.target_read(msg + b"\xAA")
  w.target_copy(ROM_SIZE + len(msg), 0x100)   # Overlapped run (RLE-like)
  w.source_copy(0x1000, 0x4000)           # Early ROM data again
  rest = BIG_SIZE - (ROM_SIZE + len(msg) + 1 + 0x100 + 0x4000)
  w.target_read(b"\0")
  w.target_copy(ROM_SIZE + len(msg) + 1 + 0x100 + 0x4000, rest - 1)
  target = bps_apply(src, crcs(src, b"", bytearray(w.out)))
  return crcs(src, target, w.out), target

def main():
  outdir = sys.argv[1]
  os.makedirs(outdir, exist_ok=True)
  tmp = os.path.join(outdir, ".build")
  os.makedirs(tmp, exist_ok=True)
  rom = build_rom(tmp)

  variants = {
    "ips": (rgb(2, 18, 4), ["PATCH IPS", "APLICADO!", ""], "IPS: ROM MAIOR"),
    "ups": (rgb(26, 10, 0), ["PATCH UPS", "APLICADO!", ""], "UPS: ROM MAIOR"),
    "bps": (rgb(14, 2, 18), ["PATCH BPS", "APLICADO!", ""], "BPS: ROM MAIOR"),
  }
  for name, (bg, lines, tail) in variants.items():
    cfg_rom = with_cfg(rom, bg, rgb(31, 31, 31), lines)
    if name == "bps":
      patch, target = bps_make(rom, cfg_rom, tail)
      assert bps_apply(rom, patch) == target
    else:
      target = grow(cfg_rom, tail)
      patch = ips_make(rom, target) if name == "ips" else ups_make(rom, target)
      check = ips_apply(rom, patch) if name == "ips" else ups_apply(rom, patch)
      assert check == target, name
    base = os.path.join(outdir, "demo-" + name)
    open(base + ".gba", "wb").write(rom)
    open(base + "." + name, "wb").write(patch)
    open(base + "-patched.gba.ref", "wb").write(target)
    print("%s: patch %d bytes, ROM %d -> %d bytes" % (name, len(patch), len(rom), len(target)))

if __name__ == "__main__":
  main()
