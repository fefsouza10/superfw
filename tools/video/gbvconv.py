#!/usr/bin/env python3
# Converts a video (anything ffmpeg can read) to the SuperFW .gbv format.
#
#   python3 gbvconv.py episode.mkv                 # -> episode.gbv (<= 31 MB)
#   python3 gbvconv.py movie.mp4 -o movie.gbv --max-mb 31 --res 240x160
#
# Needs Python 3 with numpy and Pillow, plus ffmpeg (in the PATH, or the
# imageio-ffmpeg package): pip install numpy pillow imageio-ffmpeg
#
# Every source frame is kept (up to 30 per second); the image quality is what
# adapts so the file fits in the size limit. See gbvplayer/player.c for the
# format description.

import argparse, json, math, os, re, shutil, struct, subprocess, sys, threading, time
import numpy as np
from PIL import Image

VBLANK_HZ = 16777216 / 280896      # 59.7275
MAX_DUR = 8                         # Max vblanks per frame (player limit)
HDR_SIZE = 64
MAX_VBLANK_BYTES = 5000              # Video bytes the player decodes per vblank

def ffmpeg_exe():
  try:
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()
  except ImportError:
    return "ffmpeg"

def probe(ff, fn):
  # Parses "ffmpeg -i" output: duration, fps, audio presence.
  out = subprocess.run([ff, "-hide_banner", "-i", fn], capture_output=True, text=True,
                       errors="replace").stderr
  m = re.search(r"Duration: (\d+):(\d+):([\d.]+)", out)
  dur = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else 0
  fps = 24000 / 1001
  m = re.search(r"Video:.* ([\d.]+) fps", out) or re.search(r"Video:.* ([\d.]+) tbr", out)
  if m:
    fps = float(m.group(1))
  if abs(fps - 23.98) < 0.01: fps = 24000 / 1001
  if abs(fps - 29.97) < 0.01: fps = 30000 / 1001
  return dur, fps, ("Audio:" in out)

# ------------------------------------------------------------- Terminal ---
# Live progress with an animated Game Boy Advance. Falls back to plain lines
# when the output is not a terminal (ie. redirected to a file).

APP_NAME = "Video-to-GBA Converter by fefsouza10"

GBA_ART = [
  r"     _________________________________________________",
  "    /  [ L ]                                   [ R ]  \\",
  "   /          .-----------------------------.          \\",
  r"  |     _     |{0}|           |",
  r"  |   _| |_   |{1}|     (B)   |",
  r"  |  |_   _|  |{2}|  (A)      |",
  r"  |    |_|    |{3}|           |",
  r"  |           |{4}|   : : :   |",
  r"  |  {L} POWER  '-----------------------------'   : : :   |",
  "   \\              GAME BOY ADVANCE                     /",
  "    \\          (SELECT)  (START)                      /",
  r"     '-----------------------------------------------'",
]
SCREEN_W = 29
RUNNER = [
  [" o ", "/|\\", "/ \\"],
  [" o ", "\\|/", " | "],
  [" o/", "/| ", "/ \\"],
  ["\\o ", " |\\", "/ \\"],
]

def fmt_time(t):
  t = int(max(0, t))
  return "%d:%02d:%02d" % (t // 3600, t // 60 % 60, t % 60) if t >= 3600 else "%d:%02d" % (t // 60, t % 60)

class Ui:
  def __init__(self):
    self.tty = sys.stdout.isatty()
    if self.tty and os.name == "nt":
      os.system("")        # Enables ANSI escape codes on the Windows console
    self.state = {"stage": "Starting", "progress": 0.0, "info": ""}
    self.lines = 0
    self.tick = 0
    self.t0 = time.time()
    self.last_plain = -1
    self.lock = threading.Lock()
    self.stop = threading.Event()
    self.thread = None

  def banner(self):
    title = "  " + APP_NAME
    print()
    print("  " + "=" * len(APP_NAME))
    print(title)
    print("  " + "=" * len(APP_NAME))
    print("  Turns any video into a .gbv file for the SuperFW video player.")
    print()

  def info(self, *rows):
    for k, v in rows:
      print("  %-12s %s" % (k + ":", v))
    print()

  def screen(self, progress):
    rows = []
    # Row 0: scrolling star field
    stars = "  .     *        .    +     .      *   " * 2
    off = self.tick % 39
    rows.append(stars[off:off + SCREEN_W])
    # Rows 1-3: runner heading to the flag; its position is the progress
    x = int(progress * (SCREEN_W - 7))
    spr = RUNNER[(self.tick // 2) % len(RUNNER)] if progress < 1 else [r"\o/", " | ", "/ \\"]
    flag = ["|>", "| ", "| "]
    for i in range(3):
      line = " " * x + spr[i]
      line = line.ljust(SCREEN_W - 3) + flag[i] + " "
      rows.append(line[:SCREEN_W])
    # Row 4: moving ground
    ground = "_-__-___-_-__" * 4
    g = ground[self.tick % 13:][:SCREEN_W]
    rows.append(g)
    return rows

  def render(self):
    with self.lock:
      st = dict(self.state)
    p = min(1.0, max(0.0, st["progress"]))
    led = "*" if (self.tick // 4) % 2 == 0 else "o"
    art = [l.replace("{L}", led) for l in GBA_ART]
    scr = self.screen(p)
    out = []
    for l in art:
      for i in range(5):
        l = l.replace("{%d}" % i, scr[i])
      out.append(l)
    barw = 40
    fill = int(p * barw)
    spin = "|/-\\"[self.tick % 4]
    out.append("")
    out.append("  %s %-44s" % (spin if p < 1 else " ", st["stage"]))
    out.append("  [%s%s] %5.1f%%" % ("#" * fill, "." * (barw - fill), 100 * p))
    out.append("  %-66s" % st["info"])
    return out

  def draw(self):
    lines = self.render()
    buf = ""
    if self.lines:
      buf += "\x1b[%dF" % self.lines
    buf += "\n".join(l + "\x1b[K" for l in lines) + "\n"
    sys.stdout.write(buf)
    sys.stdout.flush()
    self.lines = len(lines)
    self.tick += 1

  def _loop(self):
    while not self.stop.wait(0.12):
      self.draw()

  def start(self):
    # Can be called again after finish() (one progress screen per part).
    self.stop.clear()
    self.lines = 0
    self.state.update(stage="Starting", progress=0.0, info="")
    if self.tty:
      self.draw()
      self.thread = threading.Thread(target=self._loop, daemon=True)
      self.thread.start()

  def update(self, **kw):
    with self.lock:
      self.state.update(kw)
    if not self.tty:
      step = int(self.state["progress"] * 20)
      if step != self.last_plain or "stage" in kw:
        self.last_plain = step
        print("  %-28s %5.1f%%  %s" % (self.state["stage"], 100 * self.state["progress"],
              self.state["info"]), flush=True)

  def finish(self):
    if self.thread:
      self.stop.set()
      self.thread.join()
      self.draw()
    print()

  def fail(self, msg):
    self.finish()
    print("  ERROR: " + msg)
    sys.exit(1)

# ---------------------------------------------------------------- Audio ---

IMA_STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
  307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
  1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
  20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]

class AdpcmEncoder:
  def __init__(self):
    self.pred, self.idx = 0, 0

  def encode(self, samples):
    # Returns a chunk: state header + 4-bit codes (low nibble first).
    out = bytearray(struct.pack("<hBB", self.pred, self.idx, 0))
    pred, idx = self.pred, self.idx
    steps, itab = IMA_STEPS, IMA_INDEX
    codes = []
    for s in samples:
      step = steps[idx]
      diff = s - pred
      nib = 0
      if diff < 0:
        nib = 8
        diff = -diff
      vd = step >> 3
      if diff >= step: nib |= 4; diff -= step; vd += step
      step >>= 1
      if diff >= step: nib |= 2; diff -= step; vd += step
      step >>= 1
      if diff >= step: nib |= 1; vd += step
      pred = pred - vd if nib & 8 else pred + vd
      if pred > 32767: pred = 32767
      elif pred < -32768: pred = -32768
      idx += itab[nib]
      if idx < 0: idx = 0
      elif idx > 88: idx = 88
      codes.append(nib)
    if len(codes) & 1:
      codes.append(0)
    out += bytes(codes[i] | (codes[i + 1] << 4) for i in range(0, len(codes), 2))
    self.pred, self.idx = pred, idx
    return bytes(out)

# ------------------------------------------------------------- Palette ---

def make_palette(frames):
  # 256 colors (5 bits per channel) for a group of frames.
  sample = frames[:: max(1, len(frames) // 8)][:8]
  h, w = sample[0].shape[:2]
  mosaic = np.concatenate(sample, axis=0)
  img = Image.fromarray(mosaic)
  q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
  pal = np.array(q.getpalette()[:768], dtype=np.int32).reshape(-1, 3)
  used = np.unique(np.array(q))
  pal = (pal[used] + 4) >> 3
  pal = np.clip(pal, 0, 31)
  pal = np.unique(pal, axis=0)
  if len(pal) < 256:
    pal = np.concatenate([pal, np.zeros((256 - len(pal), 3), np.int32)])
  return pal[:256]

def make_lut(pal):
  # RGB555 -> nearest palette index.
  r, g, b = np.meshgrid(np.arange(32), np.arange(32), np.arange(32), indexing="ij")
  cols = np.stack([r.ravel(), g.ravel(), b.ravel()], 1).astype(np.int32)   # index = r*1024+g*32+b
  lut = np.empty(len(cols), np.uint8)
  pf = pal.astype(np.int32)
  for i in range(0, len(cols), 4096):
    c = cols[i:i + 4096]
    d = ((c[:, None, :] - pf[None, :, :]) ** 2).sum(2)
    lut[i:i + 4096] = d.argmin(1)
  return lut

# --------------------------------------------------------------- Video ---

class VideoEncoder:
  # Per frame: global motion (s8 dx, s8 dy), 2-bit ops per 8x8 macroblock,
  # then the data. Ops: 0 skip (keep the back page, two frames ago), 1 fill,
  # 2 split into 4x4 blocks, 3 extended (copies from the previous frame,
  # which is the front page, or a half resolution block). See player.c.
  SUB_BYTES = np.array([0, 1, 4, 16])

  def __init__(self, w, h):
    self.w, self.h = w, h
    self.mbw, self.mbh = w // 8, h // 8
    self.pages = [np.zeros((h, w), np.uint8), np.zeros((h, w), np.uint8)]
    self.cur = 0          # Page the next frame is decoded into
    self.prev_src = None
    self.lam = 10.0

  def blocks4(self, a):
    # (H, W, ...) -> (mbh, mbw, 2, 2, 4, 4, ...): 4x4 blocks inside 8x8 macroblocks
    s = a.shape[2:]
    return a.reshape(self.mbh, 2, 4, self.mbw, 2, 4, *s).transpose(0, 3, 1, 4, 2, 5, *range(6, 6 + len(s)))

  def unblocks4(self, b):
    s = b.shape[6:]
    return b.transpose(0, 2, 4, 1, 3, 5, *range(6, 6 + len(s))).reshape(self.h, self.w, *s)

  def global_motion(self, cur, prev):
    # Coarse search on a 1/4 size image, then refined at full size.
    def small(a):
      h, w = a.shape[0] // 4 * 4, a.shape[1] // 4 * 4
      return a[:h, :w].reshape(h // 4, 4, w // 4, 4, 3).mean((1, 3))
    c, p = small(cur), small(prev)
    def sad(a, b, dx, dy):
      h, w = a.shape[:2]
      ya, yb = max(0, -dy), min(h, h - dy)
      xa, xb = max(0, -dx), min(w, w - dx)
      if yb - ya < h // 2 or xb - xa < w // 2:
        return 1e18
      return np.abs(a[ya:yb, xa:xb] - b[ya + dy:yb + dy, xa + dx:xb + dx]).mean()
    best = min(((sad(c, p, dx, dy), dx, dy) for dy in range(-6, 7) for dx in range(-8, 9)))
    bx, by = best[1] * 4, best[2] * 4
    best = min(((sad(cur, prev, bx + dx, by + dy), bx + dx, by + dy)
                for dy in range(-3, 4) for dx in range(-3, 4)))
    if best[0] > sad(cur, prev, 0, 0) * 0.97:
      return 0, 0
    return int(np.clip(best[1], -100, 100)), int(np.clip(best[2], -100, 100))

  def encode(self, rgb5, pal, lut, intra, target):
    # rgb5: (H, W, 3) int 0..31. intra: 2 keyframe (no references), 1 no
    # skip (the back page is stale), 0 anything goes. Picks the lambda that
    # makes the frame close to target bytes. Returns (bytes, lambda).
    H, W, mbh, mbw = self.h, self.w, self.mbh, self.mbw
    palf = pal.astype(np.float32)
    src = rgb5.astype(np.float32)
    T = self.blocks4(src)                                            # (mbh,mbw,2,2,4,4,3)
    def cidx(c):
      c = np.clip(np.rint(c), 0, 31).astype(np.int32)
      return lut[c[..., 0] * 1024 + c[..., 1] * 32 + c[..., 2]]
    def err(idx, tgt):
      return ((palf[idx] - tgt) ** 2).sum(-1)
    INF = np.float32(1e18)

    # Plain 4x4 modes
    q = cidx(T)
    e_raw = err(q, T).sum((-1, -2))
    cf = cidx(T.mean((4, 5)))
    e_fill = err(cf[..., None, None], T).sum((-1, -2))
    luma = T @ np.array([2, 4, 1], np.float32)
    hi = luma > luma.mean((4, 5), keepdims=True)
    nh = hi.sum((4, 5))
    m1 = (T * hi[..., None]).sum((4, 5)) / np.maximum(nh, 1)[..., None]
    m0 = (T * ~hi[..., None]).sum((4, 5)) / np.maximum(16 - nh, 1)[..., None]
    c0, c1 = cidx(m0), cidx(m1)
    q2 = np.where(hi, c1[..., None, None], c0[..., None, None])
    e_2c = err(q2, T).sum((-1, -2))
    e_skip = err(self.blocks4(self.pages[self.cur]), T).sum((-1, -2)) if intra == 0 else \
             np.full(e_fill.shape, INF, np.float32)
    sub_e = np.stack([e_skip, e_fill, e_2c, e_raw], -1)             # (mbh,mbw,2,2,4)

    # Macroblock fill and half resolution (4x4 pixels, each one 2x2)
    mcf = cidx(T.mean((2, 3, 4, 5)))
    mb_fill = err(mcf[:, :, None, None, None, None], T).sum((2, 3, 4, 5))
    M = src.reshape(mbh, 8, mbw, 8, 3).transpose(0, 2, 1, 3, 4)       # (mbh,mbw,8,8,3)
    hq = cidx(M.reshape(mbh, mbw, 4, 2, 4, 2, 3).mean((3, 5)))        # (mbh,mbw,4,4)
    hrec = np.repeat(np.repeat(hq, 2, 2), 2, 3)
    e_half = err(hrec, M).sum((2, 3))

    # Copies from the previous frame (front page)
    gdx = gdy = 0
    have_copy = intra < 2
    if have_copy:
      front = self.pages[self.cur ^ 1]
      P = palf[front]
      if self.prev_src is not None:
        gdx, gdy = self.global_motion(src, self.prev_src)
      cands = {(0, 0), (gdx, gdy)}
      for d in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        cands.add((gdx + d[0], gdy + d[1]))
      for d in ((2, 0), (-2, 0), (0, 2), (0, -2), (1, 0), (-1, 0), (0, 1), (0, -1)):
        cands.add(d)
      cands = sorted(cands)
      R = max(4, max(max(abs(a), abs(b)) for a, b in cands))
      Pp = np.pad(P, ((R, R), (R, R), (0, 0)), mode="edge")
      ys, xs = np.arange(mbh) * 8, np.arange(mbw) * 8
      E = np.empty((len(cands), mbh, mbw, 2, 2), np.float32)
      for i, (dx, dy) in enumerate(cands):
        S = Pp[R + dy:R + dy + H, R + dx:R + dx + W]
        e = self.blocks4(((S - src) ** 2).sum(-1)).sum((-1, -2))
        ok = ((ys + dy >= 0) & (ys + dy + 8 <= H))[:, None] & ((xs + dx >= 0) & (xs + dx + 8 <= W))[None, :]
        E[i] = np.where(ok[:, :, None, None], e, INF)
      gi = cands.index((gdx, gdy))
      e_g = E[gi]
      bi = E.sum((3, 4)).argmin(0)                                    # best vector per MB
      e_mv = np.take_along_axis(E, bi[None, :, :, None, None], 0)[0]
    else:
      e_g = e_mv = np.full(e_fill.shape, INF, np.float32)
      bi = np.zeros((mbh, mbw), np.int64)
      cands = [(0, 0)]

    sb = self.SUB_BYTES.astype(np.float32)
    skip_mb = np.where(intra == 0, e_skip.sum((2, 3)), INF)
    g_mb, mv_mb = e_g.sum((2, 3)), e_mv.sum((2, 3))
    def decide(lam):
      sc = sub_e + lam * sb                                           # (...,4)
      so3 = sc[..., 1:].argmin(-1) + 1                                # best coded sub-op
      c3 = np.take_along_axis(sc, so3[..., None], -1)[..., 0]
      so = np.where(sc[..., 0] < c3, 0, so3)
      sbest = np.minimum(sc[..., 0], c3)
      # Refinement after a copy: sub-op 0 keeps the copied pixels.
      sog = np.where(e_g < c3, 0, so3)
      som = np.where(e_mv < c3, 0, so3)
      opts = np.stack([
        skip_mb,                                                     # 0 skip
        mb_fill + lam * 1,                                           # 1 fill
        sbest.sum((2, 3)) + lam * 1,                                 # 2 split
        g_mb + lam * 1,                                              # 3/0 global copy
        np.minimum(e_g, c3).sum((2, 3)) + lam * 2,                   # 3/1 + refine
        mv_mb + lam * 3,                                             # 3/2 vector copy
        np.minimum(e_mv, c3).sum((2, 3)) + lam * 4,                  # 3/3 + refine
        e_half + lam * 17,                                           # 3/4 half res
      ], -1)
      mo = opts.argmin(-1)
      base_b = np.array([0, 1, 1, 1, 2, 3, 4, 17], np.float32)[mo]
      extra = np.select([mo == 2, mo == 4, mo == 6],
                        [sb[so].sum((2, 3)), sb[sog].sum((2, 3)), sb[som].sum((2, 3))], 0)
      return mo, so, sog, som, float((base_b + extra).sum()) + 2 + (mbw * mbh + 3) // 4

    # Lambda search (bisection in log space), starting around the last one.
    lo, hi_l = math.log(self.lam / 16), math.log(self.lam * 16)
    if decide(math.exp(lo))[4] <= target:
      lo = math.log(0.01)
    if decide(math.exp(hi_l))[4] > target:
      hi_l = math.log(1e7)
    for _ in range(11):
      mid = (lo + hi_l) / 2
      if decide(math.exp(mid))[4] > target:
        lo = mid
      else:
        hi_l = mid
    lam = math.exp(hi_l)
    self.lam = min(max(lam, 0.05), 1e6)
    mo, so, sog, som, _ = decide(lam)
    if os.environ.get("GBV_STATS"):
      st = getattr(VideoEncoder, "stats", None)
      if st is None:
        st = VideoEncoder.stats = {"mo": np.zeros(8), "so": np.zeros(4)}
      st["mo"] += np.bincount(mo.ravel(), minlength=8)
      sel = np.concatenate([so[mo == 2].ravel(), sog[mo == 4].ravel(), som[mo == 6].ravel()])
      st["so"] += np.bincount(sel, minlength=4)

    # Bitstream and the decoded picture (exactly what the player does)
    out = self.pages[self.cur].copy()
    ob = self.blocks4(out)
    front = self.pages[self.cur ^ 1]
    ops = bytearray((mbw * mbh + 3) // 4)
    data = bytearray()
    def subdata(y, x, sop):
      data.append(int(sop[0, 0]) | int(sop[0, 1]) << 2 | int(sop[1, 0]) << 4 | int(sop[1, 1]) << 6)
      for by in range(2):
        for bx in range(2):
          o = sop[by, bx]
          if o == 1:
            c = int(cf[y, x, by, bx]); data.append(c); ob[y, x, by, bx] = c
          elif o == 2:
            m = int((hi[y, x, by, bx].reshape(16) * (1 << np.arange(16))).sum())
            data.extend((int(c0[y, x, by, bx]), int(c1[y, x, by, bx]), m & 255, m >> 8))
            ob[y, x, by, bx] = q2[y, x, by, bx]
          elif o == 3:
            blk = q[y, x, by, bx]
            data.extend(blk.astype(np.uint8).tobytes())
            ob[y, x, by, bx] = blk
    n = 0
    for y in range(mbh):
      for x in range(mbw):
        o = int(mo[y, x])
        op = min(o, 3)
        ops[n >> 2] |= op << ((n & 3) * 2)
        n += 1
        if o == 1:
          c = int(mcf[y, x]); data.append(c); ob[y, x] = c
        elif o == 2:
          subdata(y, x, so[y, x])
        elif o in (3, 4, 5, 6):
          dx, dy = (gdx, gdy) if o < 5 else cands[bi[y, x]]
          data.append(o - 3)
          if o >= 5:
            data.extend(struct.pack("<bb", dx, dy))
          blk = front[y * 8 + dy:y * 8 + dy + 8, x * 8 + dx:x * 8 + dx + 8]
          ob[y, x] = blk.reshape(2, 4, 2, 4).transpose(0, 2, 1, 3)
          if o == 4:
            subdata(y, x, sog[y, x])
          elif o == 6:
            subdata(y, x, som[y, x])
        elif o == 7:
          data.append(4)
          data.extend(hq[y, x].astype(np.uint8).tobytes())
          ob[y, x] = hrec[y, x].reshape(2, 4, 2, 4).transpose(0, 2, 1, 3)
    self.pages[self.cur] = self.unblocks4(ob)
    self.cur ^= 1
    self.prev_src = src
    return struct.pack("<bb", gdx, gdy) + bytes(ops + data), lam

# ---------------------------------------------------------------- Main ---

def clip_args(args):
  # ffmpeg input options that select the part being converted (--parts).
  if getattr(args, "part_len", None) is None:
    return []
  return ["-ss", "%.3f" % args.part_start, "-t", "%.3f" % args.part_len]

def read_frames(ff, fn, w, h, fps_out, clip=()):
  vf = ("scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=%d:%d:(ow-iw)/2:(oh-ih)/2" % (w, h, w, h))
  if fps_out:
    vf += ",fps=%f" % fps_out
  p = subprocess.Popen([ff, "-v", "error", *clip, "-i", fn, "-vf", vf, "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
  fsz = w * h * 3
  while True:
    buf = p.stdout.read(fsz)
    if len(buf) < fsz:
      break
    yield np.frombuffer(buf, np.uint8).reshape(h, w, 3)
  p.wait()

def read_audio(ff, fn, rate, clip=()):
  # ffmpeg only takes whole sample rates. The player rate (16777216 / timer
  # reload) is within 0.05 Hz of it, a few ms of drift over 20 minutes.
  p = subprocess.run([ff, "-v", "error", *clip, "-i", fn, "-vn", "-ac", "1", "-ar", str(int(round(rate))),
                      "-f", "s16le", "-"], capture_output=True)
  if p.returncode or not p.stdout:
    raise RuntimeError("could not decode the audio track: " +
                       p.stderr.decode("utf-8", "replace").strip()[-200:])
  a = np.frombuffer(p.stdout, np.int16)
  # Quiet tracks lose detail in the 8-bit output and are hard to hear on the
  # GBA speaker: bring the loud parts close to full scale (up to 4x gain).
  peak = np.percentile(np.abs(a.astype(np.int32)), 99.9) if len(a) else 0
  if peak > 0:
    gain = min(4.0, 29000 / peak)
    if gain > 1.1:
      a = np.clip(a.astype(np.float32) * gain, -32768, 32767).astype(np.int16)
  return a

def encode(args, budget_scale, ui, attempt, media):
  ff = ffmpeg_exe()
  w, h = map(int, args.res.split("x"))
  duration, fps, has_audio = media
  fps_out = None
  if fps > args.max_fps + 0.01:
    fps_out, fps = args.max_fps, args.max_fps
  spv = args.spv if (has_audio and not args.no_audio) else 0

  total_vb = int(round(duration * VBLANK_HZ))
  audio_bytes = (total_vb * spv) // 2 + 4 * duration * fps
  max_bytes = int(args.max_mb * 1024 * 1024) - 64 * 1024        # room for the player
  nframes_est = max(1, duration * fps)
  # Palettes (512 bytes per keyframe) are paid from the video budget as they come.
  video_budget = (max_bytes - audio_bytes - nframes_est * 12) * 0.985 * budget_scale
  per_vblank = max(50.0, video_budget / max(1, total_vb))
  passtxt = "" if attempt == 0 else " (pass %d)" % (attempt + 1)
  if spv:
    ui.update(stage="Reading audio" + passtxt, progress=0.0, info="")
  samples = read_audio(ff, args.input, spv * VBLANK_HZ, clip_args(args)) if spv else None
  ui.update(stage="Converting" + passtxt, progress=0.0,
            info="video budget %.1f KB/s" % (per_vblank * VBLANK_HZ / 1024))
  aenc = AdpcmEncoder()
  venc = VideoEncoder(w, h)

  out = bytearray(HDR_SIZE)
  index = []
  frames_out = 0
  vb_time = 0
  pending = None        # [flags, dur, palbytes, videobytes, start_vb]

  def flush():
    nonlocal frames_out
    flags, dur, pb, vb, start = pending
    a = b""
    if spv:
      s = samples[start * spv:(start + dur) * spv]
      if len(s) < dur * spv:
        s = np.concatenate([s, np.zeros(dur * spv - len(s), np.int16)])
      a = aenc.encode(s.tolist())
    body = struct.pack("<BBH", flags, dur, len(a)) + pb + a + vb
    size = 4 + len(body)
    pad = (-size) & 3
    out.extend(struct.pack("<I", size + pad) + body + bytes(pad))
    frames_out += 1

  gop, gop_pal, gop_lut, gop_len = [], None, None, 0
  pal_err0 = 0.0        # Palette error of the frame the palette was made from

  def pal_err(lut, pal, f5):
    # Mean squared error of a frame subsample mapped to the palette.
    c = f5[::4, ::4]
    return float(((pal[lut[c[..., 0] * 1024 + c[..., 1] * 32 + c[..., 2]]] - c) ** 2).sum(-1).mean())
  src_iter = read_frames(ff, args.input, w, h, fps_out, clip_args(args))
  prev_small = None
  frame_no = 0
  spent = 0
  last_src = None
  t0 = time.time()
  intra_left = 0
  for frame in src_iter:
    # Timing: source frame i spans [i/fps, (i+1)/fps) -> vblanks
    t_start = int(round(frame_no * VBLANK_HZ / fps))
    t_end = int(round((frame_no + 1) * VBLANK_HZ / fps))
    frame_no += 1
    dur = t_end - t_start
    if dur <= 0:
      continue
    rgb5 = (frame.astype(np.int32) + 4) >> 3
    rgb5 = np.clip(rgb5, 0, 31)
    small = frame[::8, ::8].astype(np.int32)

    # Scene cut or GOP too long -> new palette + keyframe
    newgop = gop_pal is None or gop_len >= args.gop * fps
    # The palette only changes on keyframes. When the picture drifts away from
    # it (fades, lighting changes, new characters) the colors go wrong, so
    # start a new group once it maps much worse than when it was made.
    if not newgop and gop_len >= fps / 4:
      if pal_err(gop_lut, gop_pal, rgb5) > max(2.5 * pal_err0, pal_err0 + 2.0):
        newgop = True
    if prev_small is not None and np.abs(small - prev_small).mean() > args.scene:
      newgop = True
    prev_small = small
    if newgop:
      gop_pal = make_palette([frame])
      gop_lut = make_lut(gop_pal)
      pal_err0 = pal_err(gop_lut, gop_pal, rgb5)
      gop_len = 0
      intra_left = 2

    # Merge frames that do not change (common in anime) into the previous one
    if pending and not newgop and last_src is not None and pending[1] + dur <= MAX_DUR:
      d = ((last_src - rgb5) ** 2).sum(-1)
      if d.mean() < args.still:
        pending[1] += dur
        vb_time = t_end
        gop_len += 1
        if os.environ.get("GBV_DEBUG"):
          dec = gop_pal[venc.pages[venc.cur ^ 1]]
          mse = float(((dec - rgb5) ** 2).mean()) + 1e-6
          sys.stderr.write("M %d %.2f\n" % (frame_no, 10 * math.log10(31 * 31 / mse)))
        continue

    if pending:
      flush()

    intra = intra_left
    # Rate control: every frame gets its share of the budget, plus a part of
    # what is left over (or minus what was overspent) spread over ~3 seconds.
    # The encoder then picks the lambda that fits the frame in that size.
    base = per_vblank * dur
    debt = max(spent - per_vblank * t_start, -per_vblank * 600)
    target = base - debt * dur / 180
    target = min(max(target, base * 0.25), base * (8 if intra == 2 else 4))
    if intra == 2:
      # A keyframe has nothing to copy from: starving it leaves the whole
      # group blocky, since the next frames can only copy its mistakes.
      target = max(target, base * 4)
    target = min(target, MAX_VBLANK_BYTES * dur)    # Keeps the decoder on time
    vbytes, lam = venc.encode(rgb5, gop_pal, gop_lut, intra, target)
    spent += len(vbytes) + (512 if newgop else 0) + 8
    if os.environ.get("GBV_DEBUG"):
      dec = gop_pal[venc.pages[venc.cur ^ 1]]
      mse = float(((dec - rgb5) ** 2).mean()) + 1e-6
      sys.stderr.write("%d %d %d %.2f %d %.2f\n" % (frame_no, len(vbytes), int(target), lam, int(debt),
                                                   10 * math.log10(31 * 31 / mse)))
      dumps = os.environ.get("GBV_DUMP", "")
      if dumps and str(frame_no) in dumps.split(","):
        Image.fromarray((np.concatenate([dec, rgb5], 1) * 8).astype(np.uint8)).save(
          os.environ.get("GBV_DUMPDIR", ".") + "/f%05d.png" % frame_no)

    flags = 0
    pb = b""
    if intra_left == 2:
      flags = 1
      pb = b"".join(struct.pack("<H", int(r) | int(g) << 5 | int(b) << 10) for r, g, b in gop_pal)
      index.append((frames_out, len(out), t_start))
    if intra_left:
      intra_left -= 1
    pending = [flags, dur, pb, vbytes, t_start]
    last_src = rgb5
    vb_time = t_end
    gop_len += 1
    if frame_no % 8 == 0:
      el = time.time() - t0
      prog = t_end / max(1, total_vb)
      eta = el / prog - el if prog > 0.01 else 0
      speed = (t_end / VBLANK_HZ) / max(0.001, el)
      ui.update(progress=prog,
                info="%s / %s  |  %.1f MB  |  %d frames  |  %.1fx  |  ETA %s" % (
                  fmt_time(t_end / VBLANK_HZ), fmt_time(duration), len(out) / 1048576,
                  frames_out, speed, fmt_time(eta) if prog > 0.01 else "--:--"))

  if pending:
    flush()
  if os.environ.get("GBV_STATS"):
    st = VideoEncoder.stats
    sys.stderr.write("MB modes skip/fill/split/gcopy/gcopy+ref/mv/mv+ref/half: %s\n" %
                     " ".join("%.1f%%" % (100 * v / st["mo"].sum()) for v in st["mo"]))
    sys.stderr.write("4x4 sub-ops keep/fill/2col/raw: %s\n" %
                     " ".join("%.1f%%" % (100 * v / st["so"].sum()) for v in st["so"]))
  index_off = len(out)
  out += struct.pack("<I", len(index))
  for f, o, t in index:
    out += struct.pack("<III", f, o, t)
  title = os.path.splitext(os.path.basename(args.input))[0]
  if getattr(args, "part_len", None) is not None:
    title = "%s %d/%d" % (title, args.part_no, args.parts)
  title = title.encode("utf-8")[:39]
  out[:HDR_SIZE] = struct.pack("<4sHHHHIII40s", b"GBV2", w, h, spv, 0, frames_out, index_off,
                               vb_time, title)
  return bytes(out), max_bytes

def main():
  ap = argparse.ArgumentParser(description=APP_NAME + ": converts videos to .gbv files "
                               "for the SuperFW video player")
  ap.add_argument("input", help="Video file (anything ffmpeg can read: mkv, mp4, avi...)")
  ap.add_argument("-o", "--output", help="Output .gbv file (default: next to the input)")
  ap.add_argument("--max-mb", type=float, default=31.5, help="Size limit (default 31.5 MB)")
  ap.add_argument("--res", default="240x160", help="240x160 (default) or 120x80 (scaled up)")
  ap.add_argument("--max-fps", type=float, default=30, help="Frame rate cap (default 30)")
  ap.add_argument("--spv", type=int, default=176, choices=[176, 264, 304],
                  help="Audio samples per frame: 176 (10.5 kHz), 264 (15.8 kHz), 304 (18.2 kHz)")
  ap.add_argument("--no-audio", action="store_true", help="Drop the sound track")
  ap.add_argument("--gop", type=float, default=10, help="Seconds between keyframes (seek points)")
  ap.add_argument("--scene", type=float, default=28, help="Scene cut threshold")
  ap.add_argument("--still", type=float, default=0.3, help="Merge source frames that change less than this")
  ap.add_argument("--parts", type=int, default=1,
                  help="Split the video in this many files, each one up to the size limit "
                       "(2 parts = twice the bytes per minute = a much sharper picture)")
  args = ap.parse_args()
  args.parts = max(1, args.parts)
  if not args.output:
    args.output = os.path.splitext(args.input)[0] + ".gbv"

  ui = Ui()
  ui.banner()
  if not os.path.isfile(args.input):
    print("  ERROR: file not found: %s" % args.input)
    sys.exit(1)
  ff = ffmpeg_exe()
  if ff == "ffmpeg" and not shutil.which("ffmpeg"):
    print("  ERROR: ffmpeg was not found. Install it with: pip install imageio-ffmpeg")
    sys.exit(1)
  media = probe(ff, args.input)
  duration, fps, has_audio = media
  if duration <= 0:
    print("  ERROR: could not read the video (is it a valid video file?)")
    sys.exit(1)
  shown_fps = min(fps, args.max_fps)
  ui.info(("Input", args.input), ("Output", args.output),
          ("Duration", fmt_time(duration)),
          ("Video", "%s @ %.3f fps%s" % (args.res, shown_fps,
                    " (capped from %.3f)" % fps if fps > args.max_fps + 0.01 else "")),
          ("Audio", ("%.1f kHz mono ADPCM" % (args.spv * VBLANK_HZ / 1000))
                    if has_audio and not args.no_audio else "none"),
          ("Size limit", "%.1f MB" % args.max_mb))

  t0 = time.time()
  outputs = []
  for part in range(args.parts):
    output = args.output
    pmedia = media
    if args.parts > 1:
      plen = duration / args.parts
      args.part_start, args.part_len, args.part_no = part * plen, plen, part + 1
      pmedia = (plen, fps, has_audio)
      base, ext = os.path.splitext(args.output)
      output = "%s (%d of %d)%s" % (base, part + 1, args.parts, ext)
      print("  Part %d of %d: %s to %s -> %s" % (part + 1, args.parts, fmt_time(args.part_start),
                                               fmt_time(args.part_start + plen), output))
    ui.start()
    scale = 1.0
    try:
      for attempt in range(4):
        data, maxb = encode(args, scale, ui, attempt, pmedia)
        if len(data) <= maxb:
          break
        scale *= maxb / len(data) * 0.97
        ui.update(stage="Too big (%.1f MB), retrying with less quality" % (len(data) / 1048576),
                  progress=0.0)
      ui.update(stage="Done!", progress=1.0, info="")
    except KeyboardInterrupt:
      ui.fail("cancelled by the user.")
    except RuntimeError as e:
      ui.fail(str(e))
    ui.finish()
    open(output, "wb").write(data)
    outputs.append((output, len(data), maxb, struct.unpack_from("<I", data, 12)[0]))

  for output, size, maxb, nframes in outputs:
    print("  Saved %s" % output)
    print("  %.2f MB (limit %.2f MB), %d frames." % (size / 1048576, maxb / 1048576, nframes))
  print("  Converted in %s. Copy %s to the SD card and open %s from SuperFW. Enjoy the show!" % (
        fmt_time(time.time() - t0), "it" if len(outputs) == 1 else "them",
        "it" if len(outputs) == 1 else "each part"))
  if args.parts == 1 and duration > 12 * 60:
    print("  Tip: fast scenes look blocky in long videos. Splitting it in two files gives each")
    print("  part twice the bytes per minute and a much sharper picture: add --parts 2")
  print()

if __name__ == "__main__":
  main()
