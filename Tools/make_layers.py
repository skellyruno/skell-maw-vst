"""Splits Assets/source/art_original.jpg into the plugin art:
   Assets/art.jpg          the illustration with its six butterflies removed (background patched with real texture)
   Assets/butterflies.png  the butterflies as transparent sprites, 3x2 cells of 200 px
   Assets/tongue.png       the serpent's tongue as a transparent sprite (also removed from art.jpg)
Needs numpy, scipy, opencv-python, pillow.  Run from the repo root:  python3 Tools/make_layers.py
The printed table is what Common/MawEditor.cpp uses for sprite home positions."""
import numpy as np, cv2, scipy.ndimage as ndi
from PIL import Image

SRC = 'Assets/source/art_original.jpg'
CELL = 200
HOMES = [(682, 132), (1234, 267), (219, 284), (3, 412), (147, 607), (1264, 886)]   # top-left of each bright blob
EDGE_BODY_X = 37        # the yellow butterfly is cut by the left edge: its body axis, used to mirror the visible wing

src = cv2.imread(SRC)
rgb = src[:, :, ::-1].astype(np.float32)
lum, mx = rgb.mean(2), rgb.max(2)
bright = (lum > 75) | (mx > 130)
mask = ndi.binary_opening(ndi.binary_closing(bright, iterations=3), iterations=2)
lab, _ = ndi.label(mask)
boxes = ndi.find_objects(lab)
ids = [next(i for i, sl in enumerate(boxes, 1) if (sl[1].start, sl[0].start) == h) for h in HOMES]

full = np.zeros(mask.shape, bool)
atlas = np.zeros((CELL * 2, CELL * 3, 4), np.uint8)
table = []
for k, i in enumerate(ids):
    m = ndi.binary_fill_holes(ndi.binary_dilation(lab == i, iterations=4))
    # antennae are thin and were lost by the opening: take any bright pixel near the body too
    near = ndi.binary_dilation(m, iterations=48 if k == 0 else 34)   # butterfly 0 has long antennae & ((lum > 70) | (mx > 140))
    full |= m | near
    ys, xs = np.nonzero(m)
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    if xs.min() == 0:
        cx = EDGE_BODY_X
    x0, y0 = int(round(cx - CELL / 2)), int(round(cy - CELL / 2))
    cell = np.zeros((CELL, CELL, 4), np.uint8)
    for u in range(CELL):
        ax = x0 + u
        if xs.min() == 0 and ax < EDGE_BODY_X:            # mirror the visible right wing over the clipped left one
            ax = 2 * EDGE_BODY_X - ax
        if not 0 <= ax < 2000:
            continue
        lo, hi = max(0, y0), min(2000, y0 + CELL)
        cell[lo - y0:hi - y0, u, :3] = src[lo:hi, ax, ::-1]
        cell[lo - y0:hi - y0, u, 3] = m[lo:hi, ax] * 255
    cell[:, :, 3] = np.clip(cv2.GaussianBlur(cell[:, :, 3].astype(np.float32), (0, 0), 1.4), 0, 255).astype(np.uint8)
    r, c = divmod(k, 3)
    atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = cell
    table.append((k, cx / 2000, cy / 2000))

# patch the holes with texture borrowed from nearby dark background
hole = cv2.dilate(full.astype(np.uint8), np.ones((9, 9), np.uint8)).astype(bool)
lab_h, nh = ndi.label(hole)
base = src.copy()
for j in range(1, nh + 1):
    mj = lab_h == j
    best = None
    ys, xs = np.nonzero(mj)
    for dy in range(-640, 641, 40):
        for dx in range(-640, 641, 40):
            ty, tx = ys + dy, xs + dx
            if ty.min() < 0 or tx.min() < 0 or ty.max() >= 2000 or tx.max() >= 2000 or hole[ty, tx].any():
                continue
            v = lum[ty, tx]
            score = v.mean() + np.percentile(v, 99)        # want an all-dark donor: no twigs, no wings
            if best is None or score < best[0]:
                best = (score, dy, dx)
    if best is None:
        continue
    _, dy, dx = best
    ys, xs = np.nonzero(mj)
    soft = cv2.GaussianBlur(mj.astype(np.float32), (0, 0), 3.0)[ys, xs][:, None]
    base[ys, xs] = (src[ys + dy, xs + dx] * soft + src[ys, xs] * (1 - soft)).astype(np.uint8)

# the serpent's tongue: a red strip on dark background, fading out where it joins the jaw
TONGUE_CUT_X, TONGUE_FADE = 1235, 60          # sprite alpha falls to 0 between CUT_X - FADE and CUT_X
TONGUE_PIVOT = (1205, 1445)
R, G, B = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
red = (R > 100) & (R > 1.6 * G) & (R > 1.6 * B)
box = np.zeros_like(red); box[1190:1465, 880:TONGUE_CUT_X] = True
red = ndi.binary_closing(red & box, iterations=2)
tl, tn = ndi.label(red)
tm = tl == (1 + int(np.argmax(ndi.sum(red, tl, range(1, tn + 1)))))
tm = ndi.binary_fill_holes(ndi.binary_dilation(tm, iterations=3))
cols = np.arange(2000)[None, :]
fade = np.clip((TONGUE_CUT_X - cols) / TONGUE_FADE, 0, 1)
alpha = cv2.GaussianBlur(tm.astype(np.float32), (0, 0), 1.2) * fade
ys, xs = np.nonzero(alpha > 0.02)
tx0, tx1, ty0, ty1 = xs.min() - 4, xs.max() + 5, ys.min() - 4, ys.max() + 5
sprite = np.zeros((ty1 - ty0, tx1 - tx0, 4), np.uint8)
sprite[:, :, :3] = src[ty0:ty1, tx0:tx1, ::-1]
sprite[:, :, 3] = (np.clip(alpha[ty0:ty1, tx0:tx1], 0, 1) * 255).astype(np.uint8)
Image.fromarray(sprite, 'RGBA').save('Assets/tongue.png', optimize=True)
# remove it from the base where the sprite is fully opaque (the faded root stays, so the join never shows a gap)
hole_t = cv2.dilate(((alpha > 0.98) | (red & (cols < TONGUE_CUT_X - TONGUE_FADE))).astype(np.uint8), np.ones((17, 17), np.uint8)).astype(bool)   # 'red' also catches the thin tip
hole_t &= cols < TONGUE_CUT_X - TONGUE_FADE + 8
yy, xx = np.nonzero(hole_t)
soft = cv2.GaussianBlur(hole_t.astype(np.float32), (0, 0), 3.0)[yy, xx][:, None]
base[yy, xx] = (src[yy - 230, xx] * soft + src[yy, xx] * (1 - soft)).astype(np.uint8)   # same trunk/background texture from just above

cv2.imwrite('Assets/art.jpg', base, [cv2.IMWRITE_JPEG_QUALITY, 90])
Image.fromarray(atlas, 'RGBA').save('Assets/butterflies.png', optimize=True)
for k, x, y in table:
    print(f'sprite {k}: home x={x:.4f} y={y:.4f}')
print(f'tongue: origin x={tx0} y={ty0} size {tx1 - tx0}x{ty1 - ty0}, pivot {TONGUE_PIVOT}')
