#!/usr/bin/env python3
# Converts a video (anything ffmpeg can read) to the SuperFW .gbv format.
#
#   python3 gbvconv.py episodio.mkv                # -> episodio.gbv (<= 31 MB)
#   python3 gbvconv.py filme.mp4 -o filme.gbv --max-mb 31 --res 240x160
#
# Needs Python 3 with numpy and Pillow, plus ffmpeg (in the PATH, or the
# imageio-ffmpeg package): pip install numpy pillow imageio-ffmpeg
#
# Every source frame is kept (up to 30 per second); the image quality is what
# adapts so the file fits in the size limit. See gbvplayer/player.c for the
# format description.

import argparse, json, math, os, re, struct, subprocess, sys, time
import numpy as np
from PIL import Image

VBLANK_HZ = 16777216 / 280896      # 59.7275
MAX_DUR = 8                         # Max vblanks per frame (player limit)
HDR_SIZE = 64

def ffmpeg_exe():
  try:
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()
  except ImportError:
    return "ffmpeg"

def probe(ff, fn):
  # Parses "ffmpeg -i" output: duration, fps, audio presence.
  out = subprocess.run([ff, "-hide_banner", "-i", fn], capture_output=True, text=True).stderr
  m = re.search(r"Duration: (\d+):(\d+):([\d.]+)", out)
  dur = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else 0
  fps = 24000 / 1001
  m = re.search(r"Video:.* ([\d.]+) fps", out) or re.search(r"Video:.* ([\d.]+) tbr", out)
  if m:
    fps = float(m.group(1))
  if abs(fps - 23.98) < 0.01: fps = 24000 / 1001
  if abs(fps - 29.97) < 0.01: fps = 30000 / 1001
  return dur, fps, ("Audio:" in out)

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
  def __init__(self, w, h):
    self.w, self.h = w, h
    self.mbw, self.mbh = w // 8, h // 8
    self.pages = [np.zeros((h, w), np.uint8), np.zeros((h, w), np.uint8)]
    self.cur = 0          # Page the next frame is decoded into
    self.lam = 20.0

  def blocks4(self, a):
    # (H, W, ...) -> (mbh, mbw, 2, 2, 4, 4, ...): 4x4 blocks inside 8x8 macroblocks
    h, w = self.h, self.w
    s = a.shape[2:]
    return a.reshape(self.mbh, 2, 4, self.mbw, 2, 4, *s).transpose(0, 3, 1, 4, 2, 5, *range(6, 6 + len(s)))

  def unblocks4(self, b):
    s = b.shape[6:]
    return b.transpose(0, 2, 4, 1, 3, 5, *range(6, 6 + len(s))).reshape(self.h, self.w, *s)

  def encode(self, rgb5, pal, lut, intra, lam):
    # rgb5: (H, W, 3) int 0..31. Returns (video bytes, decoded indices).
    palf = pal.astype(np.float32)
    T = self.blocks4(rgb5.astype(np.float32))                        # (mbh,mbw,2,2,4,4,3)
    ref = self.blocks4(self.pages[self.cur])                         # indices
    def cidx(c):   # float colors (...,3) -> palette index
      c = np.clip(np.rint(c), 0, 31).astype(np.int32)
      return lut[c[..., 0] * 1024 + c[..., 1] * 32 + c[..., 2]]
    def err(idx, tgt):
      return ((palf[idx] - tgt) ** 2).sum(-1)

    q = cidx(T)                                                      # raw indices
    e_raw = err(q, T).sum((-1, -2))
    e_skip = err(ref, T).sum((-1, -2))
    mean = T.mean((4, 5))                                            # (mbh,mbw,2,2,3)
    cf = cidx(mean)
    e_fill = err(cf[..., None, None], T).sum((-1, -2))
    # Two colors: split by luma around the block mean
    luma = T @ np.array([2, 4, 1], np.float32)
    hi = luma > luma.mean((4, 5), keepdims=True)
    nh = hi.sum((4, 5))
    m1 = (T * hi[..., None]).sum((4, 5)) / np.maximum(nh, 1)[..., None]
    m0 = (T * ~hi[..., None]).sum((4, 5)) / np.maximum(16 - nh, 1)[..., None]
    c0, c1 = cidx(m0), cidx(m1)
    q2 = np.where(hi, c1[..., None, None], c0[..., None, None])
    e_2c = err(q2, T).sum((-1, -2))

    INF = np.float32(1e18)
    costs = np.stack([np.where(intra, INF, e_skip), e_fill + lam * 10, e_2c + lam * 34, e_raw + lam * 130], -1)
    sub_op = costs.argmin(-1)                                        # (mbh,mbw,2,2)
    sub_cost = costs.min(-1).sum((-1, -2)) + lam * 8                 # split cost (sub-op byte)
    # Macroblock options
    mb_skip = np.where(intra, INF, e_skip.sum((-1, -2)))
    mmean = T.mean((2, 3, 4, 5))
    mcf = cidx(mmean)
    mb_fill = err(mcf[:, :, None, None, None, None], T).sum((-1, -2, -3, -4)) + lam * 8
    mb_op = np.stack([mb_skip, mb_fill, sub_cost], -1).argmin(-1) # 0 skip, 1 fill, 2 split

    # Build the decoded image and the bitstream
    out = self.pages[self.cur].copy()
    ob = self.blocks4(out)
    ops = bytearray((self.mbw * self.mbh + 3) // 4)
    data = bytearray()
    n = 0
    for y in range(self.mbh):
      for x in range(self.mbw):
        op = int(mb_op[y, x])
        ops[n >> 2] |= op << ((n & 3) * 2)
        n += 1
        if op == 1:
          c = int(mcf[y, x])
          data.append(c)
          ob[y, x] = c
        elif op == 2:
          so = sub_op[y, x]
          data.append(int(so[0, 0]) | int(so[0, 1]) << 2 | int(so[1, 0]) << 4 | int(so[1, 1]) << 6)
          for by in range(2):
            for bx in range(2):
              o = so[by, bx]
              if o == 1:
                c = int(cf[y, x, by, bx]); data.append(c); ob[y, x, by, bx] = c
              elif o == 2:
                h = hi[y, x, by, bx]
                m = 0
                for r in range(4):
                  for cc in range(4):
                    if h[r, cc]: m |= 1 << (r * 4 + cc)
                data += bytes([int(c0[y, x, by, bx]), int(c1[y, x, by, bx]), m & 255, m >> 8])
                ob[y, x, by, bx] = q2[y, x, by, bx]
              elif o == 3:
                blk = q[y, x, by, bx]
                data += blk.astype(np.uint8).tobytes()
                ob[y, x, by, bx] = blk
    dec = self.unblocks4(ob)
    self.pages[self.cur] = dec
    self.cur ^= 1
    return bytes(ops + data), dec

# ---------------------------------------------------------------- Main ---

def read_frames(ff, fn, w, h, fps_out):
  vf = ("scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=%d:%d:(ow-iw)/2:(oh-ih)/2" % (w, h, w, h))
  if fps_out:
    vf += ",fps=%f" % fps_out
  p = subprocess.Popen([ff, "-v", "error", "-i", fn, "-vf", vf, "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
  fsz = w * h * 3
  while True:
    buf = p.stdout.read(fsz)
    if len(buf) < fsz:
      break
    yield np.frombuffer(buf, np.uint8).reshape(h, w, 3)
  p.wait()

def read_audio(ff, fn, rate):
  p = subprocess.run([ff, "-v", "error", "-i", fn, "-vn", "-ac", "1", "-ar", "%f" % rate,
                      "-f", "s16le", "-"], capture_output=True)
  return np.frombuffer(p.stdout, np.int16)

def encode(args, budget_scale):
  ff = ffmpeg_exe()
  w, h = map(int, args.res.split("x"))
  duration, fps, has_audio = probe(ff, args.input)
  fps_out = None
  if fps > args.max_fps + 0.01:
    fps_out, fps = args.max_fps, args.max_fps
  spv = args.spv if (has_audio and not args.no_audio) else 0

  total_vb = int(round(duration * VBLANK_HZ))
  audio_bytes = (total_vb * spv) // 2 + 4 * duration * fps
  max_bytes = int(args.max_mb * 1024 * 1024) - 64 * 1024        # room for the player
  nframes_est = max(1, duration * fps)
  video_budget = (max_bytes - audio_bytes - nframes_est * 12 - 600 * 520) * budget_scale
  per_vblank = max(50.0, video_budget / max(1, total_vb))
  if args.verbose:
    print("duration %.1fs, %.3f fps, audio %s, video budget %.0f B/s" %
          (duration, fps, "yes" if spv else "no", per_vblank * VBLANK_HZ))

  samples = read_audio(ff, args.input, spv * VBLANK_HZ) if spv else None
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
  src_iter = read_frames(ff, args.input, w, h, fps_out)
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
    if prev_small is not None and np.abs(small - prev_small).mean() > args.scene:
      newgop = True
    prev_small = small
    if newgop:
      gop_pal = make_palette([frame])
      gop_lut = make_lut(gop_pal)
      gop_len = 0
      intra_left = 2

    # Merge frames that do not change (common in anime) into the previous one
    if pending and not newgop and last_src is not None and pending[1] + dur <= MAX_DUR:
      d = ((last_src - rgb5) ** 2).sum(-1)
      if d.mean() < args.still:
        pending[1] += dur
        vb_time = t_end
        gop_len += 1
        continue

    if pending:
      flush()

    intra = intra_left > 0
    vbytes, dec = venc.encode(rgb5, gop_pal, gop_lut, intra, venc.lam)
    # Rate control: lambda follows the bytes spent vs the budget earned so far
    # (merged frames earn budget too). About 4 s of video to react.
    spent += len(vbytes) + (512 if newgop else 0) + 8
    debt = spent - per_vblank * t_end
    venc.lam = float(np.clip(venc.lam * math.exp(np.clip(debt / (per_vblank * 240), -1, 1) * 0.15), 0.02, 1e6))

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
    if args.verbose and frame_no % 500 == 0:
      el = time.time() - t0
      print("  %5.1f%%  %d frames, %.1f MB, %.1f s" % (100 * t_start / max(1, total_vb), frames_out,
            len(out) / 1048576, el), flush=True)

  if pending:
    flush()
  index_off = len(out)
  out += struct.pack("<I", len(index))
  for f, o, t in index:
    out += struct.pack("<III", f, o, t)
  title = os.path.splitext(os.path.basename(args.input))[0].encode("utf-8")[:39]
  out[:HDR_SIZE] = struct.pack("<4sHHHHIII40s", b"GBV1", w, h, spv, 0, frames_out, index_off,
                               vb_time, title)
  return bytes(out), max_bytes

def main():
  ap = argparse.ArgumentParser(description="Converts videos for the SuperFW video player")
  ap.add_argument("input")
  ap.add_argument("-o", "--output")
  ap.add_argument("--max-mb", type=float, default=31.5, help="Size limit (default 31.5 MB)")
  ap.add_argument("--res", default="240x160", help="240x160 (default) or 120x80 (scaled up)")
  ap.add_argument("--max-fps", type=float, default=30, help="Frame rate cap (default 30)")
  ap.add_argument("--spv", type=int, default=176, choices=[176, 264, 304],
                  help="Audio samples per frame: 176 (10.5 kHz), 264 (15.8 kHz), 304 (18.2 kHz)")
  ap.add_argument("--no-audio", action="store_true")
  ap.add_argument("--gop", type=float, default=10, help="Seconds between keyframes (seek points)")
  ap.add_argument("--scene", type=float, default=28, help="Scene cut threshold")
  ap.add_argument("--still", type=float, default=0.3, help="Merge source frames that change less than this")
  ap.add_argument("-v", "--verbose", action="store_true")
  args = ap.parse_args()
  if not args.output:
    args.output = os.path.splitext(args.input)[0] + ".gbv"

  scale = 1.0
  for attempt in range(4):
    data, maxb = encode(args, scale)
    print("%s: %.2f MB (limite %.2f MB)" % (args.output, len(data) / 1048576, maxb / 1048576))
    if len(data) <= maxb:
      break
    scale *= maxb / len(data) * 0.97
    print("Too big, encoding again with a smaller budget...")
  open(args.output, "wb").write(data)

if __name__ == "__main__":
  main()
