# Writes the example project's test models (tree.glb, rock.glb, creature.glb) beside this script: parts as named
# nodes with locator children, for the procmodel descriptors tree.json, rock.json and creature.json. Also the layer
# images of their procedural textures (textures/*.png), which bark, stone and hide .texture.json
# describe.
import json, math, os, random, struct, sys, zlib

def quat_mul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz)

def axis_quat(axis, deg):
    s, c = math.sin(math.radians(deg) / 2), math.cos(math.radians(deg) / 2)
    return tuple(a * s for a in axis) + (c,)

def outward(yaw, tilt):
    # local +Y leaning `tilt` degrees from up, towards `yaw` around the y axis
    return quat_mul(axis_quat((0, 1, 0), yaw), axis_quat((0, 0, 1), -tilt))

def cylinder(sides, h, r0, r1):
    # u once around, v at the same scale up the side
    pos, nrm, uv, idx = [], [], [], []
    for ring, (y, r) in enumerate(((0, r0), (h, r1))):
        for i in range(sides + 1):
            a = 2 * math.pi * i / sides
            pos.append((r * math.cos(a), y, r * math.sin(a)))
            nrm.append((math.cos(a), (r0 - r1) / h, math.sin(a)))
            uv.append((i / sides, y / (2 * math.pi * r0)))
    n = sides + 1
    for i in range(sides):
        idx += [i, n + i, i + 1, i + 1, n + i, n + i + 1]
    return pos, nrm, uv, idx

def blob(radius, centre, squash, jitter, rng, subdiv=2):
    # an icosphere with seeded radial noise
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    v = [tuple(c / math.sqrt(sum(x * x for x in p)) for c in p) for p in v]
    for _ in range(subdiv):
        mid, nf = {}, []
        def m(a, b):
            k = (min(a, b), max(a, b))
            if k not in mid:
                p = [(v[a][i] + v[b][i]) / 2 for i in range(3)]
                l = math.sqrt(sum(x * x for x in p))
                v.append(tuple(x / l for x in p))
                mid[k] = len(v) - 1
            return mid[k]
        for a, b, c in f:
            ab, bc, ca = m(a, b), m(b, c), m(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    scale = [radius * (1 + rng.uniform(-jitter, jitter)) for _ in v]
    pos = [(centre[0] + p[0] * s, centre[1] + p[1] * s * squash, centre[2] + p[2] * s) for p, s in zip(v, scale)]
    nrm = list(v)
    # u mirrored around (no seam), v from pole to pole
    uv = [(2 * abs(math.atan2(p[2], p[0])) / math.pi, 2 * math.acos(max(-1, min(1, p[1]))) / math.pi) for p in v]
    idx = [i for tri in f for i in (tri[0], tri[2], tri[1])]
    return pos, nrm, uv, idx

class Glb:
    def __init__(self):
        self.bin = bytearray()
        self.g = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [],
                  "materials": [], "accessors": [], "bufferViews": [], "buffers": []}

    def material(self, name, rgb, roughness=0.9):
        self.g["materials"].append({"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": list(rgb) + [1.0], "metallicFactor": 0.0, "roughnessFactor": roughness}})
        return len(self.g["materials"]) - 1

    def _view(self, data, target):
        while len(self.bin) % 4: self.bin.append(0)
        self.g["bufferViews"].append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data), "target": target})
        self.bin += data
        return len(self.g["bufferViews"]) - 1

    def _vec(self, values):
        n = len(values[0])
        data = b"".join(struct.pack("<%df" % n, *p) for p in values)
        acc = {"bufferView": self._view(data, 34962), "componentType": 5126, "count": len(values), "type": "VEC%d" % n}
        acc["min"] = [min(p[i] for p in values) for i in range(n)]
        acc["max"] = [max(p[i] for p in values) for i in range(n)]
        self.g["accessors"].append(acc)
        return len(self.g["accessors"]) - 1

    def mesh(self, name, prims):
        out = []
        for (pos, nrm, uv, idx), mat in prims:
            ia = {"bufferView": self._view(struct.pack("<%dI" % len(idx), *idx), 34963), "componentType": 5125,
                  "count": len(idx), "type": "SCALAR"}
            self.g["accessors"].append(ia)
            indices = len(self.g["accessors"]) - 1
            out.append({"attributes": {"POSITION": self._vec(pos), "NORMAL": self._vec(nrm), "TEXCOORD_0": self._vec(uv)},
                        "indices": indices, "material": mat})
        self.g["meshes"].append({"name": name, "primitives": out})
        return len(self.g["meshes"]) - 1

    def node(self, name, mesh=None, t=(0, 0, 0), r=None, parent=None):
        n = {"name": name, "translation": list(t)}
        if r: n["rotation"] = list(r)
        if mesh is not None: n["mesh"] = mesh
        self.g["nodes"].append(n)
        i = len(self.g["nodes"]) - 1
        if parent is None: self.g["scenes"][0]["nodes"].append(i)
        else: self.g["nodes"][parent].setdefault("children", []).append(i)
        return i

    def save(self, path):
        while len(self.bin) % 4: self.bin.append(0)
        self.g["buffers"].append({"byteLength": len(self.bin)})
        js = json.dumps(self.g, separators=(",", ":")).encode()
        while len(js) % 4: js += b" "
        with open(path, "wb") as f:
            f.write(struct.pack("<3I", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin)))
            f.write(struct.pack("<2I", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<2I", len(self.bin), 0x004E4942) + self.bin)

def tree(path):
    rng = random.Random(1)
    g = Glb()
    bark = g.material("bark", (0.33, 0.22, 0.13))
    leaves = [g.material("leaves_green", (0.18, 0.45, 0.16)), g.material("leaves_yellow", (0.55, 0.6, 0.18))]
    # trunks: locators up the trunk, leaning out
    for name, x, h, r0, count in (("TRUNK_A", 0, 4.0, 0.25, 5), ("TRUNK_B", 3, 5.5, 0.3, 5)):
        t = g.node(name, g.mesh(name, [(cylinder(8, h, r0, 0.1), bark)]), (x, 0, 0))
        for i in range(count):
            g.node("T%s_L%d" % (name[-1], i + 1), None,
                   (0, h * (0.45 + 0.5 * i / (count - 1)), 0), outward(i * 137.5, 55 - 5 * i), t)
    # branches: a stem with a leaf clump at its tip, and locators for the next branches
    for name, x, length, leaf in (("BRANCH_A", 6, 2.0, 0), ("BRANCH_B", 8, 2.6, 1)):
        stem = cylinder(6, length, 0.09, 0.03)
        clump = blob(0.75, (0, length, 0), 0.7, 0.18, rng)
        b = g.node(name, g.mesh(name, [(stem, bark), (clump, leaves[leaf])]), (x, 0, 0))
        for i in range(3):
            g.node("B%s_L%d" % (name[-1], i + 1), None, (0, length * (0.4 + 0.25 * i), 0),
                   outward(i * 120 + 30, 45), b)
    g.save(path)

def rock(path):
    rng = random.Random(2)
    g = Glb()
    stone = [g.material("stone_grey", (0.42, 0.41, 0.4)), g.material("stone_brown", (0.45, 0.37, 0.3))]
    for k, (name, x, radius, squash) in enumerate((("ROCK_A", 0, 1.0, 0.6), ("ROCK_B", 3, 0.8, 0.9),
                                                   ("ROCK_C", 6, 1.1, 0.45))):
        r = g.node(name, g.mesh(name, [(blob(radius, (0, radius * squash * 0.5, 0), squash, 0.25, rng), stone[k % 2])]),
                   (x, 0, 0))
        # spots around the foot where smaller stones may lie
        for i in range(4):
            a = math.radians(i * 90 + 20 * k)
            g.node("R%s_L%d" % (name[-1], i + 1), None, (math.cos(a) * radius * 1.3, 0, math.sin(a) * radius * 1.3),
                   axis_quat((0, 1, 0), i * 77), r)
    g.save(path)

def stretched(mesh, s):
    # a mesh scaled per axis (normals rescaled the inverse way)
    pos, nrm, uv, idx = mesh
    pos = [tuple(p[k] * s[k] for k in range(3)) for p in pos]
    nrm = [tuple(n[k] / s[k] for k in range(3)) for n in nrm]
    nrm = [tuple(c / math.sqrt(sum(x * x for x in n)) for c in n) for n in nrm]
    return pos, nrm, uv, idx

def moved(mesh, t):
    pos, nrm, uv, idx = mesh
    return [tuple(p[k] + t[k] for k in range(3)) for p in pos], nrm, uv, idx

def creature(path):
    # a four-legged grazer facing +x: a body with locators for its neck, legs and tail; heads, legs and
    # tails hang off them (legs grow down from their locators)
    rng = random.Random(3)
    g = Glb()
    hide = g.material("hide", (0.55, 0.45, 0.32))
    down = axis_quat((1, 0, 0), 180)
    for name, x, (bx, by, bz), leg in (("BODY_A", 0, (0.75, 0.45, 0.4), 0.7), ("BODY_B", 3, (1.0, 0.38, 0.35), 0.85)):
        y = leg + by * 0.5
        b = g.node(name, g.mesh(name, [(moved(stretched(blob(1, (0, 0, 0), 1, 0.06, rng), (bx, by, bz)), (0, y, 0)), hide)]),
                   (x, 0, 0))
        p = name[-1]
        g.node(p + "_NECK", None, (bx * 0.85, y + by * 0.45, 0), axis_quat((0, 0, 1), -20), b)
        g.node(p + "_TAIL", None, (-bx * 0.95, y + by * 0.2, 0), axis_quat((0, 0, 1), 120), b)
        for i, (lx, lz) in enumerate(((0.55, 0.6), (0.55, -0.6), (-0.6, 0.6), (-0.6, -0.6))):
            g.node("%s_LEG%d" % (p, i + 1), None, (bx * lx, y - by * 0.5, bz * lz), down, b)
    # heads: along +x from the neck
    for name, x, mesh in (("HEAD_A", 6, moved(stretched(blob(1, (0, 0, 0), 1, 0.08, rng), (0.4, 0.26, 0.24)), (0.3, 0.15, 0))),
                          ("HEAD_B", 8, moved(stretched(blob(1, (0, 0, 0), 1, 0.12, rng), (0.32, 0.32, 0.3)), (0.22, 0.2, 0)))):
        g.node(name, g.mesh(name, [(mesh, hide)]), (x, 0, 0))
    # legs: from the hip down (local +y); the thick ones are shorter, so the body sits lower
    for name, x, length, r0, r1 in (("LEG_A", 10, 0.8, 0.1, 0.06), ("LEG_B", 11, 0.95, 0.14, 0.1)):
        g.node(name, g.mesh(name, [(cylinder(8, length, r0, r1), hide)]), (x, 0, 0))
    for name, x, mesh in (("TAIL_A", 12, cylinder(6, 0.6, 0.06, 0.02)),
                          ("TAIL_B", 13, moved(stretched(blob(1, (0, 0, 0), 1, 0.2, rng), (0.18, 0.3, 0.18)), (0, 0.25, 0)))):
        g.node(name, g.mesh(name, [(mesh, hide)]), (x, 0, 0))
    g.save(path)

def save_png(path, size, texel):
    # texel(x, y) -> RGBA floats 0..1; 8-bit RGBA, unfiltered rows
    rows = b"".join(b"\0" + bytes(max(0, min(255, round(c * 255))) for x in range(size) for c in texel(x, y))
                    for y in range(size))
    chunk = lambda kind, data: struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", size, size, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

def noise(seed, cells_x, cells_y):
    # tiling value noise, smoothly interpolated: f(u, v) for u, v in [0, 1)
    rng = random.Random(seed)
    grid = [[rng.random() for _ in range(cells_x)] for _ in range(cells_y)]
    def f(u, v):
        x, y = u * cells_x, v * cells_y
        x0, y0 = int(x), int(y)
        tx, ty = x - x0, y - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        g = lambda i, j: grid[j % cells_y][i % cells_x]
        top = g(x0, y0) + (g(x0 + 1, y0) - g(x0, y0)) * tx
        bottom = g(x0, y0 + 1) + (g(x0 + 1, y0 + 1) - g(x0, y0 + 1)) * tx
        return top + (bottom - top) * ty
    return f

def fbm(seed, cells_x, cells_y, octaves=4):
    layers = [noise(seed + o, cells_x << o, cells_y << o) for o in range(octaves)]
    return lambda u, v: sum(l(u, v) * 0.5 ** o for o, l in enumerate(layers)) / (2 - 0.5 ** (octaves - 1))

def textures(folder, size=128):
    # the procedural textures' layers: coloured, so a recolour has a hue to move (grey stays grey)
    os.makedirs(folder, exist_ok=True)
    s = float(size)
    grain, plates, moss = fbm(10, 12, 2), fbm(20, 4, 3), fbm(30, 6, 6)
    def bark_a(x, y):
        # long grain with dark furrows
        n = grain(x / s, y / s)
        furrow = 0.45 + 0.55 * min(1, abs(n - 0.5) * 6)
        return (0.46 * furrow, 0.31 * furrow, 0.19 * furrow, 1)
    def bark_b(x, y):
        # plates split by cracks
        n = plates(x / s, y / s)
        crack = min(1, abs(math.sin(n * 18)) * 3)
        k = 0.35 + 0.65 * crack
        return (0.5 * k, 0.36 * k, 0.24 * k, 1)
    def bark_moss(x, y):
        n = moss(x / s, y / s)
        a = max(0, min(1, (n - 0.52) * 8))
        return (0.33, 0.52, 0.2, a)
    speckle, strata, vein = fbm(40, 8, 8), fbm(50, 3, 6), fbm(60, 5, 5)
    def stone_a(x, y):
        k = 0.55 + 0.55 * speckle(x / s, y / s)
        return (0.47 * k, 0.43 * k, 0.38 * k, 1)
    def stone_b(x, y):
        n = strata(x / s, y / s)
        k = 0.6 + 0.35 * math.sin((y / s + n * 0.6) * math.pi * 8)
        return (0.5 * k, 0.42 * k, 0.34 * k, 1)
    def stone_vein(x, y):
        a = max(0, 1 - abs(vein(x / s, y / s) - 0.5) * 40)
        return (0.75, 0.66, 0.5, a)
    # with each: a normal map from its height (brightness, or alpha where it has holes) and masks
    # (g roughness, b metallic): veins are a shiny mineral
    fur, mottle, spots = fbm(70, 32, 32, 3), fbm(80, 5, 5), fbm(90, 7, 7)
    def hide_a(x, y):
        # short fur
        k = 0.7 + 0.4 * fur(x / s, y / s)
        return (0.55 * k, 0.43 * k, 0.3 * k, 1)
    def hide_b(x, y):
        # a pale belly-like mottle
        k = 0.75 + 0.3 * mottle(x / s, y / s) + 0.15 * fur(x / s, y / s)
        return (0.6 * k, 0.5 * k, 0.36 * k, 1)
    def hide_spots(x, y):
        a = max(0, min(1, (spots(x / s, y / s) - 0.56) * 10))
        return (0.3, 0.2, 0.12, a)
    for name, texel, roughness, metallic in (("bark_a", bark_a, 0.9, 0), ("bark_b", bark_b, 0.85, 0),
                                             ("bark_moss", bark_moss, 0.7, 0), ("stone_a", stone_a, 0.8, 0),
                                             ("stone_b", stone_b, 0.75, 0), ("stone_vein", stone_vein, 0.35, 0.8),
                                             ("hide_a", hide_a, 0.8, 0), ("hide_b", hide_b, 0.8, 0),
                                             ("hide_spots", hide_spots, 0.75, 0)):
        save_png(os.path.join(folder, name + ".png"), size, texel)
        rgba = [[texel(x, y) for x in range(size)] for y in range(size)]
        height = [[t[3] if t[3] < 1 or name.endswith(("moss", "vein", "spots")) else (t[0] + t[1] + t[2]) / 1.5 for t in row]
                  for row in rgba]
        def normal(x, y):
            h = lambda i, j: height[j % size][i % size]
            nx, ny = (h(x - 1, y) - h(x + 1, y)) * 4, (h(x, y + 1) - h(x, y - 1)) * 4  # +y up the image
            l = math.sqrt(nx * nx + ny * ny + 1)
            return (nx / l * 0.5 + 0.5, ny / l * 0.5 + 0.5, 1 / l * 0.5 + 0.5, 1)
        save_png(os.path.join(folder, name + ".normal.png"), size, normal)
        save_png(os.path.join(folder, name + ".masks.png"), size,
                 lambda x, y: (0, roughness + (0.5 - height[y][x]) * 0.15, metallic, 1))

out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
tree(out + "/tree.glb")
rock(out + "/rock.glb")
creature(out + "/creature.glb")
textures(out + "/textures")
print("wrote", out + "/tree.glb", out + "/rock.glb", out + "/creature.glb", out + "/textures")
