#!/usr/bin/env python3
# not upstream: the ground materials' DDS files from CC0 scans (design-ground-detail 1.1); run by hand, not at build time
# make.py <sources> <out>: <sources>/<material>/ holds the scan's colour, height, OpenGL normal and ambient occlusion maps
# writes <out>/<material>_a.dds (colour, height) and _b.dds (normal x y, +y north, ao), BC7 with full mips, and <out>/CREDITS.txt
# needs numpy, Pillow and etcpak (BC7: bc7enc by Rich Geldreich)

import glob
import os
import struct
import sys

import numpy as np
from PIL import Image

SIZE = 1024
NAMES = ["grass", "forest", "soil", "sand", "rock", "gravel"]  # GroundGeom::Name order

# the picked scans: asset, source, url (CC0)
ASSETS = {
	"grass": ("Grass 004", "ambientCG", "https://ambientcg.com/view?id=Grass004"),
	"forest": ("Forest Leaves 02", "Rob Tuytel, Poly Haven", "https://polyhaven.com/a/forest_leaves_02"),
	"soil": ("Dry Mud Field 001", "Rob Tuytel, Rico Cilliers, Poly Haven", "https://polyhaven.com/a/dry_mud_field_001"),
	"sand": ("Aerial Beach 01", "Rob Tuytel, Poly Haven", "https://polyhaven.com/a/aerial_beach_01"),
	"rock": ("Aerial Rocks 02", "Rob Tuytel, Poly Haven", "https://polyhaven.com/a/aerial_rocks_02"),
	"gravel": ("Gravel Ground 01", "Rob Tuytel, Poly Haven", "https://polyhaven.com/a/gravel_ground_01"),
}

PATTERNS = {
	"col": ["*_diff_*", "*_Color.*", "*diffuse*", "*albedo*"],
	"hgt": ["*_disp_*", "*_Displacement.*", "*height*"],
	"nrm": ["*_nor_gl_*", "*_NormalGL.*"],
	"nrmdx": ["*_nor_dx_*", "*_NormalDX.*"],
	"ao": ["*_ao_*", "*_AmbientOcclusion.*"],
	"arm": ["*_arm_*"],
}


def find(d, key):
	for p in PATTERNS[key]:
		hits = sorted(f for f in glob.glob(os.path.join(d, p)) if os.path.splitext(f)[1].lower() in (".png", ".jpg", ".jpeg", ".tif", ".tiff"))
		if hits:
			return hits[0]
	return None


def load(path, mode):
	im = Image.open(path)
	if mode == "F":
		a = np.asarray(im)
		if a.ndim == 3:
			a = a[..., 0]
		a = a.astype(np.float64)
		a /= 65535.0 if a.max() > 255.0 else 255.0
		im = Image.fromarray(a.astype(np.float32), "F")
	else:
		im = im.convert(mode)
	return im


def resize(im):
	return im if im.size == (SIZE, SIZE) else im.resize((SIZE, SIZE), Image.LANCZOS)


def channel(im):
	return np.asarray(resize(im), dtype=np.float64)


def seam(a):
	# wrap-around step against the steps inside: a tileable scan has the same
	inner = np.mean(np.abs(np.diff(a, axis=1))) + np.mean(np.abs(np.diff(a, axis=0)))
	wrap = np.mean(np.abs(a[:, 0] - a[:, -1])) + np.mean(np.abs(a[0, :] - a[-1, :]))
	return wrap / max(inner, 1e-9)


def mips(rgba):
	out = [rgba]
	a = rgba.astype(np.float64)
	while a.shape[0] > 1:
		a = 0.25 * (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2])
		out.append(np.clip(np.rint(a), 0, 255).astype(np.uint8))
	return out


def bc7(level):
	import etcpak
	h, w = level.shape[:2]
	if w < 4 or h < 4:
		level = np.tile(level, (4 // h if h < 4 else 1, 4 // w if w < 4 else 1, 1))
		h, w = level.shape[:2]
	return etcpak.compress_bc7(np.ascontiguousarray(level).tobytes(), w, h)


def dds(path, levels):
	h, w = levels[0].shape[:2]
	data = [bc7(l) for l in levels]
	pf = struct.pack("<II4sIIIII", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
	hdr = struct.pack("<4sIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, h, w, len(data[0]), 0, len(levels))
	hdr += b"\0" * 44 + pf + struct.pack("<IIIII", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
	hdr += struct.pack("<IIIII", 98, 3, 0, 1, 0)  # BC7_UNORM, 2D, one layer
	with open(path, "wb") as f:
		f.write(hdr)
		for d in data:
			f.write(d)


def material(src, name, out):
	d = os.path.join(src, name)
	col, hgt, nrm = find(d, "col"), find(d, "hgt"), find(d, "nrm")
	flip = False
	if not nrm and find(d, "nrmdx"):
		nrm, flip = find(d, "nrmdx"), True
	if not (col and hgt and nrm):
		sys.exit(f"{name}: needs a colour, a height and a normal map in {d}")
	rgb = np.stack([channel(c) for c in load(col, "RGB").split()], axis=-1)
	h = channel(load(hgt, "F"))
	lo, hi = np.percentile(h, [0.5, 99.5])
	h = np.clip((h - lo) / max(hi - lo, 1e-9), 0.0, 1.0)
	c = np.stack([channel(c) for c in load(nrm, "RGB").split()], axis=-1) / 255.0
	n = c * 2.0 - 1.0
	if np.hypot (*n[..., :2].reshape(-1, 2).mean(axis=0)) > 0.1:
		# a tileable height field has a mean slope of 0: a map far from it was saved with sRGB gamma
		lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4) * 2.0 - 1.0
		if np.hypot (*lin[..., :2].reshape(-1, 2).mean(axis=0)) > 0.1:
			sys.exit(f"{name}: the normal map's mean slope isn't 0, as stored or as sRGB")
		print(f"{name}: the normal map was stored with sRGB gamma, linearised")
		n = lin
	if flip:
		n[..., 1] = -n[..., 1]
	n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
	ao_path, arm = find(d, "ao"), find(d, "arm")
	if ao_path:
		ao = channel(load(ao_path, "L")) / 255.0
	elif arm:
		ao = np.asarray(resize(load(arm, "RGB").split()[0]), dtype=np.float64) / 255.0
	else:
		ao = np.ones((SIZE, SIZE))
		print(f"{name}: no ambient occlusion map, 1 used")
	for label, a in (("colour", rgb.mean(axis=-1)), ("height", h), ("normal y", n[..., 1])):
		s = seam(a)
		print(f"{name}: {label} seam ratio {s:.2f}")
		if s > 2.0:
			sys.exit(f"{name}: the {label} map doesn't tile (wrap step {s:.1f}x the steps inside)")
	a8 = np.dstack([rgb, h * 255.0])
	b8 = np.dstack([(n[..., 0] * 0.5 + 0.5) * 255.0, (n[..., 1] * 0.5 + 0.5) * 255.0, ao * 255.0, np.full((SIZE, SIZE), 255.0)])
	la, lb = mips(np.clip(np.rint(a8), 0, 255).astype(np.uint8)), mips(np.clip(np.rint(b8), 0, 255).astype(np.uint8))
	dds(os.path.join(out, f"{name}_a.dds"), la)
	dds(os.path.join(out, f"{name}_b.dds"), lb)
	m = la[-1][0, 0]
	print(f"{name}: mean colour {m[0]} {m[1]} {m[2]}, height {m[3]}")


def main():
	if len(sys.argv) != 3:
		sys.exit("make.py <sources> <out>")
	src, out = sys.argv[1], sys.argv[2]
	os.makedirs(out, exist_ok=True)
	for name in NAMES:
		material(src, name, out)
	with open(os.path.join(out, "CREDITS.txt"), "w") as f:
		f.write("Ground materials (Textures/GroundDetail), made by Utils/GroundMat/make.py from these scans, all CC0 (public domain):\n")
		for name in NAMES:
			a = ASSETS.get(name)
			if a:
				f.write(f"{name}: {a[0]}, {a[1]}, {a[2]}\n")


if __name__ == "__main__":
	main()
