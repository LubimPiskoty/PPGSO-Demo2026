#!/usr/bin/env python3
"""Cuts the front wall's timber sill out of the door opening.

    python3 tools/cut_forge_sill.py [res/models/SM_Forge_Building.glb]

The ironheart_forge pack builds the front wall's sill beam (y 1.2-1.38) as one
box over the whole wall, so it runs across the doorway. This clips every
triangle of that box to |x| > 1.1 and closes the new ends. build_forge.py is
fixed the same way, so this is only needed for already exported models.
"""

import json
import struct
import sys
from pathlib import Path

import numpy as np

path = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parent.parent / "res/models/SM_Forge_Building.glb")
DOOR = 1.1  # half width of the opening
Y0, Y1, Z0, Z1 = 1.19, 1.39, 3.41, 3.49  # the sill box

data = path.read_bytes()
jlen = struct.unpack("<I", data[12:16])[0]
gltf = json.loads(data[20:20 + jlen])
blen = struct.unpack("<I", data[20 + jlen:24 + jlen])[0]
binary = bytearray(data[28 + jlen:28 + jlen + blen])

prim = gltf["meshes"][0]["primitives"][0]
COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read(i):
    a = gltf["accessors"][i]
    bv = gltf["bufferViews"][a["bufferView"]]
    dt = {5126: "<f4", 5123: "<u2", 5125: "<u4"}[a["componentType"]]
    n = COMPONENTS[a["type"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    arr = np.frombuffer(binary, dtype=dt, count=a["count"] * n, offset=off)
    return arr.reshape(-1, n).astype(np.float64 if dt == "<f4" else np.int64)


attrs = {k: read(i) for k, i in prim["attributes"].items()}
if any(v.shape[0] != attrs["POSITION"].shape[0] for v in attrs.values()):
    sys.exit("attribute counts differ")
idx = read(prim["indices"]).reshape(-1, 3)
cols = list(attrs)
# One wide vertex row: every attribute side by side, so clipping interpolates
# all of them the same way
widths = [attrs[c].shape[1] for c in cols]
verts = np.hstack([attrs[c] for c in cols])
P = slice(0, 3)  # POSITION must be first
assert cols[0] == "POSITION"


def in_sill(tri):
    p = verts[tri][:, P]
    return (np.all((p[:, 1] >= Y0) & (p[:, 1] <= Y1)) and
            np.all((p[:, 2] >= Z0) & (p[:, 2] <= Z1)))


new_verts = [v for v in verts]
out = []
caps = []  # triangles on the box's end faces


def add(v):
    new_verts.append(v)
    return len(new_verts) - 1


def clip(poly, keep_left):
    """Sutherland-Hodgman against x = -DOOR (left) or x = +DOOR (right)."""
    def inside(v):
        return v[0] <= -DOOR if keep_left else v[0] >= DOOR
    res = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        if inside(a):
            res.append(a)
        if inside(a) != inside(b):
            t = ((-DOOR if keep_left else DOOR) - a[0]) / (b[0] - a[0])
            res.append(a + (b - a) * t)
    return res


cut = 0
for tri in idx:
    if not in_sill(tri):
        out.append(tuple(tri))
        continue
    xs = verts[tri][:, 0]
    if np.ptp(xs) < 1e-6:  # end cap (all at one x)
        if abs(xs[0]) > DOOR:
            caps.append(tri)
        out.append(tuple(tri))
        continue
    if xs.min() > -DOOR and xs.max() < DOOR:  # entirely in the opening
        cut += 1
        continue
    cut += 1
    for left in (True, False):
        poly = clip([verts[i] for i in tri], left)
        if len(poly) < 3:
            continue
        ids = [add(v) for v in poly]
        for k in range(1, len(ids) - 1):
            out.append((ids[0], ids[k], ids[k + 1]))

# Close the cut ends with copies of the existing end faces, moved to +-DOOR.
# The cap at x=+3.9 (normal +x) closes the left piece at x=-DOOR, and the cap
# at x=-3.9 (normal -x) closes the right piece at x=+DOOR.
for tri in caps:
    x = verts[tri[0], 0]
    ids = []
    for i in tri:
        v = verts[i].copy()
        v[0] = -DOOR if x > 0 else DOOR
        ids.append(add(v))
    out.append(tuple(ids))

print(f"clipped {cut} triangles, added {len(caps)} cap triangles")
new_verts = np.array(new_verts)
new_idx = np.array(out, dtype="<u4").reshape(-1)


def push(arr, target):
    while len(binary) % 4:
        binary.append(0)
    gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binary),
                                "byteLength": arr.nbytes, "target": target})
    binary.extend(arr.tobytes())
    return len(gltf["bufferViews"]) - 1


col = 0
for name, w in zip(cols, widths):
    a = gltf["accessors"][prim["attributes"][name]]
    arr = np.ascontiguousarray(new_verts[:, col:col + w].astype("<f4"))
    col += w
    a["bufferView"] = push(arr, 34962)
    a["byteOffset"] = 0
    a["count"] = len(arr)
    if name == "POSITION":
        a["min"], a["max"] = arr.min(0).tolist(), arr.max(0).tolist()
ia = gltf["accessors"][prim["indices"]]
ia["bufferView"] = push(new_idx, 34963)
ia["byteOffset"] = 0
ia["count"] = len(new_idx)
ia["componentType"] = 5125

gltf["buffers"][0]["byteLength"] = len(binary)
js = json.dumps(gltf, separators=(",", ":")).encode()
js += b" " * (-len(js) % 4)
while len(binary) % 4:
    binary.append(0)
total = 12 + 8 + len(js) + 8 + len(binary)
path.write_bytes(b"glTF" + struct.pack("<II", 2, total) +
                 struct.pack("<I", len(js)) + b"JSON" + js +
                 struct.pack("<I", len(binary)) + b"BIN\0" + bytes(binary))
print("wrote", path)
