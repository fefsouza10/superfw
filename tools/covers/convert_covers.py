#!/usr/bin/env python3
"""
Builds high quality covers for SuperFW (8bpp BMPs with their own palette).

The firmware reads /IMGS/<c0>/<c1>/<CODE>.bmp, where CODE is the 4-character
GBA game code (ie. BPEE). It accepts the 16bpp EZ-Flash Omega pack (dithered
to a fixed palette on the console) and, much nicer, the 8bpp BMPs this script
makes: resized with a good filter to fit 76x50 and reduced to an optimal
palette of up to 216 colors (GBA 15-bit colors), with Floyd-Steinberg dither.

Sources (pick one):
  --imgs DIR        an existing IMGS folder (EZ-Flash Omega 16bpp BMPs, or any
                    image PIL can read, named <CODE>.bmp in any subfolder)
  --images DIR      a folder of images named <CODE>.png/.jpg/.bmp
  --libretro DAT    download title screens (or --boxart) from the libretro
                    thumbnails repository, for every game code in a No-Intro
                    DAT ("Nintendo - Game Boy Advance.dat" from
                    libretro-database/metadat/no-intro)

Output goes to OUT/IMGS/... (default OUT=.): copy that IMGS folder to the SD.
Requires Pillow (pip install pillow).
"""
import argparse, os, re, struct, sys, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor
from PIL import Image

COVER_W, COVER_H, NCOLORS = 76, 50, 216
LIBRETRO = "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/"
CODE_RE = re.compile(r"^[0-9A-Za-z]{4}$")


def read_bmp16_gba(path):
    """Reads a 16bpp BMP storing GBA-native pixels (red in the low bits), as
    the EZ-Flash Omega pack does (PIL would swap red and blue)."""
    data = open(path, "rb").read()
    if data[:2] != b"BM" or struct.unpack_from("<H", data, 28)[0] != 16:
        return None
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    topdown = h < 0
    h = abs(h)
    stride = (w * 2 + 3) & ~3
    im = Image.new("RGB", (w, h))
    px = im.load()
    for fr in range(h):
        y = fr if topdown else h - 1 - fr
        for x in range(w):
            v = struct.unpack_from("<H", data, off + fr * stride + x * 2)[0]
            r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
            px[x, y] = ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))
    return im


def load_image(path):
    if path.lower().endswith(".bmp"):
        im = read_bmp16_gba(path)
        if im is not None:
            return im
    return Image.open(path).convert("RGB")


def to_cover(im):
    """Fits the image in COVER_W x COVER_H and reduces it to NCOLORS colors
    representable on the GBA (5 bits per channel)."""
    im = im.convert("RGB")
    scale = min(COVER_W / im.width, COVER_H / im.height)
    size = (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
    im = im.resize(size, Image.LANCZOS)
    # Optimal palette, then snapped to 15-bit colors and re-applied with dither.
    q = im.quantize(colors=NCOLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:NCOLORS * 3]
    pal = [((c >> 3) << 3) | (c >> 5) for c in pal]
    palimg = Image.new("P", (1, 1))
    palimg.putpalette(pal + [0] * (768 - len(pal)))
    q = im.quantize(palette=palimg, dither=Image.Dither.FLOYDSTEINBERG)
    used = max(q.tobytes()) + 1
    return q, pal[:used * 3]


def write_bmp8(path, q, pal):
    w, h = q.size
    ncol = len(pal) // 3
    stride = (w + 3) & ~3
    pix = q.load()
    rows = b"".join(bytes(pix[x, y] for x in range(w)) + b"\0" * (stride - w)
                    for y in reversed(range(h)))
    quads = b"".join(bytes((pal[i * 3 + 2], pal[i * 3 + 1], pal[i * 3], 0)) for i in range(ncol))
    off = 14 + 40 + len(quads)
    hdr = b"BM" + struct.pack("<IHHI", off + len(rows), 0, 0, off)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(rows), 2835, 2835, ncol, ncol)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(hdr + info + quads + rows)


def out_path(out, code):
    code = code.upper()
    return os.path.join(out, "IMGS", code[0], code[1], code + ".bmp")


def convert(src, out, code):
    try:
        q, pal = to_cover(load_image(src))
        write_bmp8(out_path(out, code), q, pal)
        return True
    except Exception as e:
        print("  %s: %s" % (src, e), file=sys.stderr)
        return False


def from_folder(folder, out):
    jobs = []
    for root, _, files in os.walk(folder):
        for fn in files:
            code, ext = os.path.splitext(fn)
            if CODE_RE.match(code) and ext.lower() in (".bmp", ".png", ".jpg", ".jpeg"):
                jobs.append((os.path.join(root, fn), code))
    return jobs


def parse_dat(datfile):
    """Returns {game code: title} from a No-Intro DAT (first title wins)."""
    games, name = {}, None
    for line in open(datfile, encoding="utf-8", errors="replace"):
        m = re.match(r'\s*name "(.*)"$', line.rstrip())
        if m:
            name = m.group(1)
        m = re.match(r'\s*serial "([0-9A-Za-z]{4})"$', line.rstrip())
        if m and name and m.group(1).upper() not in games:
            games[m.group(1).upper()] = name
    return games


def libretro_name(title):
    # libretro-thumbnails replaces these characters with '_'
    return re.sub(r'[&*/:`<>?\\|"]', "_", title)


def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return True
    try:
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
        with open(dest, "wb") as f:
            f.write(data)
        return True
    except Exception:
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--imgs", help="existing IMGS folder")
    src.add_argument("--images", help="folder of <CODE>.png/.jpg/.bmp images")
    src.add_argument("--libretro", metavar="DAT", help="No-Intro DAT with game codes")
    ap.add_argument("--boxart", action="store_true", help="libretro: use box art instead of title screens")
    ap.add_argument("--cache", default="libretro-cache", help="libretro: download folder")
    ap.add_argument("--out", default=".", help="output folder (IMGS is created inside)")
    ap.add_argument("-j", type=int, default=8, help="parallel jobs")
    args = ap.parse_args()

    if args.libretro:
        games = parse_dat(args.libretro)
        kind = "Named_Boxarts" if args.boxart else "Named_Titles"
        os.makedirs(args.cache, exist_ok=True)
        print("%d game codes in the DAT, downloading %s..." % (len(games), kind))

        def dl(item):
            code, title = item
            dest = os.path.join(args.cache, code + ".png")
            url = LIBRETRO + kind + "/" + urllib.parse.quote(libretro_name(title)) + ".png"
            return (dest, code) if fetch(url, dest) else None
        with ThreadPoolExecutor(args.j) as ex:
            jobs = [j for j in ex.map(dl, sorted(games.items())) if j]
    else:
        jobs = from_folder(args.imgs or args.images, args.out)

    print("Converting %d images..." % len(jobs))
    with ThreadPoolExecutor(args.j) as ex:
        ok = sum(ex.map(lambda j: convert(j[0], args.out, j[1]), jobs))
    print("Done: %d covers in %s" % (ok, os.path.join(args.out, "IMGS")))


if __name__ == "__main__":
    main()
