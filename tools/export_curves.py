# Exports Blender Bezier curves as raw control-point data for component::Bezier.
#
# Usage (from the project root):
#   blender -b <file.blend> --python tools/export_curves.py -- [out=res/curves] [txt] [local] [names...]
#     out=<dir>   output directory (default: res/curves/)
#     txt         write whitespace-separated text instead of raw binary
#     local       keep object-local coords (default: world space, i.e. matrix_world applied)
#     names...    only export these curve objects (default: every Bezier curve object)
# Or paste into Blender's Text Editor and hit Run Script (exports everything).
#
# One file per spline: <object>.bin, or <object>_<i>.bin when the object has several splines.
# Layout matches component::Bezier<glm::vec3>::control (non-overlapping cubic segments):
#   P0, P0.handle_right, P1.handle_left, P1, P1.handle_right, ... , Pn
#   -> 3 * segments + 1 points. Cyclic splines repeat P0 at the end (closing segment).
# .bin = tightly packed little-endian float32 x y z per point, no header
#        (point count = file size / 12).
# .txt = one "x y z" line per point.
# Coords are converted from Blender (+Z up) to OpenGL (+Y up): (x, y, z) -> (x, z, -y),
# same convention as build_village.py / scene.json.
import bpy, sys, os, struct, re
from mathutils import Matrix

ARGS = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
PROJECT = (
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
    if "__file__" in globals()
    else bpy.path.abspath("//")
)

out_dir = PROJECT + "res/curves/"
as_text = "raw" not in ARGS
world = "local" not in ARGS
names = [a for a in ARGS if a not in ("txt", "local") and not a.startswith("out=")]
for a in ARGS:
    if a.startswith("out="):
        out_dir = os.path.join(PROJECT, a[4:])
os.makedirs(out_dir, exist_ok=True)


def to_gl(v):
    return (v.x, v.z, -v.y)


def spline_points(spline, m):
    bp = spline.bezier_points
    pts = [m @ bp[0].co]
    segs = list(zip(bp[:-1], bp[1:]))
    if spline.use_cyclic_u and len(bp) > 1:
        segs.append((bp[-1], bp[0]))
    for a, b in segs:
        pts += [m @ a.handle_right, m @ b.handle_left, m @ b.co]
    return [to_gl(p) for p in pts]


def safe(name):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name)


def write(path, pts):
    if as_text:
        with open(path, "w") as f:
            f.write("{")
            f.writelines(
                f"{{{x:.6f},{y:.6f},{z:.6f}}}{',' if i != len(pts) - 1 else ''}\n"
                for i, (x, y, z) in enumerate(pts)
            )

            f.write("}")
    else:
        with open(path, "wb") as f:
            f.write(struct.pack(f"<{len(pts) * 3}f", *(c for p in pts for c in p)))


count = 0
for o in bpy.data.objects:
    if o.type != "CURVE" or (names and o.name not in names):
        continue
    splines = [
        s for s in o.data.splines if s.type == "BEZIER" and len(s.bezier_points) > 1
    ]
    if not splines:
        print(f"skip {o.name}: no Bezier splines with 2+ points")
        continue
    m = o.matrix_world if world else Matrix.Identity(4)
    for i, s in enumerate(splines):
        pts = spline_points(s, m)
        suffix = f"_{i}" if len(splines) > 1 else ""
        path = os.path.join(
            out_dir, safe(o.name) + suffix + (".txt" if as_text else ".bin")
        )
        write(path, pts)
        print(
            f"{o.name}[{i}]: {(len(pts) - 1) // 3} segments, {len(pts)} points"
            f"{' (cyclic)' if s.use_cyclic_u else ''} -> {path}"
        )
        count += 1

missing = set(names) - {o.name for o in bpy.data.objects}
if missing:
    print("not found:", ", ".join(sorted(missing)))
print(f"exported {count} spline(s)")
