#!/usr/bin/env python3
"""Separates the forge's timber frame beams so they stop z-fighting.

    python3 tools/fix_forge_zfight.py [res/models/SM_Forge_Building.glb]

build_forge.py puts the frame's sills, top plates, posts and braces 5-6 cm
proud of the plaster, and they all end up with their outer faces in (almost)
the same plane where they cross. This moves posts 4 mm and braces 12 mm
further out, away from the wall. Run it once on a model straight from the
pack, a second run would move them again.
"""

import json
import struct
import sys
from pathlib import Path

import numpy as np

path = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parent.parent / "res/models/SM_Forge_Building.glb")
POST, BRACE = 0.004, 0.012

data = path.read_bytes()
jlen = struct.unpack("<I", data[12:16])[0]
gltf = json.loads(data[20:20 + jlen])
blen = struct.unpack("<I", data[20 + jlen:24 + jlen])[0]
binary = bytearray(data[28 + jlen:28 + jlen + blen])
prim = gltf["meshes"][0]["primitives"][0]


def accessor(i):
    a = gltf["accessors"][i]
    bv = gltf["bufferViews"][a["bufferView"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    dt = {5126: "<f4", 5123: "<u2", 5125: "<u4"}[a["componentType"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    return a, np.frombuffer(bytes(binary), dtype=dt, count=a["count"] * n,
                            offset=off).reshape(-1, n)


acc, pos = accessor(prim["attributes"]["POSITION"])
_, idx = accessor(prim["indices"])
idx = idx.reshape(-1, 3).astype(int)

# Beams are boxes: weld corners by position, each connected group is one box
weld = {}
w = np.array([weld.setdefault(tuple(np.round(v, 4)), len(weld)) for v in pos])
parent = list(range(len(weld)))


def find(x):
    while parent[x] != x:
        parent[x] = parent[parent[x]]
        x = parent[x]
    return x


for t in idx:
    parent[find(w[t[1]])] = find(w[t[0]])
    parent[find(w[t[2]])] = find(w[t[0]])
group = np.array([find(w[i]) for i in range(len(pos))])

shift = np.zeros_like(pos, dtype=np.float64)
posts = braces = 0
for g in np.unique(group):
    v = np.where(group == g)[0]
    lo, hi = pos[v].min(0), pos[v].max(0)
    size = hi - lo
    # Front/back wall beams are thin in z (depth 5-6 cm), side wall in x,
    # and they sit proud of the wall (y > 1.2, the plinth is below)
    for axis, other in ((2, 0), (0, 2)):
        if not (0.05 <= size[axis] <= 0.062) or lo[1] < 1.15 or hi[1] > 3.05:
            continue
        along, up = size[other], size[1]
        centre = (lo[axis] + hi[axis]) / 2
        if abs(centre) < 3.0:  # not an outer wall
            continue
        sign = np.sign(centre)
        if along < 0.2 and up > 0.5:        # vertical post
            amount, posts = POST, posts + 1
        elif along > 0.25 and up > 0.25:    # diagonal brace
            amount, braces = BRACE, braces + 1
        else:                                # plate / small trim
            continue
        shift[v, axis] = sign * amount
        break

print(f"moved {posts} posts, {braces} braces")
new = (pos + shift).astype("<f4")
acc["min"], acc["max"] = new.min(0).tolist(), new.max(0).tolist()
while len(binary) % 4:
    binary.append(0)
gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binary),
                            "byteLength": new.nbytes, "target": 34962})
binary.extend(new.tobytes())
acc["bufferView"] = len(gltf["bufferViews"]) - 1
acc["byteOffset"] = 0

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
