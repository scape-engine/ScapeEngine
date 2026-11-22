# Writes the example project's test models (tree.glb, rock.glb, creature.glb, buildings/*.glb, craft/*.glb) beside this script: parts as named
# nodes, with locator children or (the creature's) skinned to a shared skeleton, for the procmodel descriptors
# tree.json, rock.json and creature.json. Also the layer
# images of their procedural textures (textures/*.png), which bark, stone and hide .texture.json
# describe. The buildings are an outpost's parts and WFC module scenes, and a watchtower's L-system levels
# (worldgen/planet.json's building styles). The craft are a rover and a skiff, each a body with locators for its
# wheels or fins (craft/rover.json and craft/skiff.json; worldgen/craft.json's fleet).
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
        for prim in prims:
            (pos, nrm, uv, idx), mat = prim[:2]
            ia = {"bufferView": self._view(struct.pack("<%dI" % len(idx), *idx), 34963), "componentType": 5125,
                  "count": len(idx), "type": "SCALAR"}
            self.g["accessors"].append(ia)
            indices = len(self.g["accessors"]) - 1
            attributes = {"POSITION": self._vec(pos), "NORMAL": self._vec(nrm), "TEXCOORD_0": self._vec(uv)}
            if len(prim) > 2:
                # skinned: four joints (bytes) and weights per vertex
                joints, weights = prim[2]
                data = b"".join(struct.pack("<4B", *j) for j in joints)
                self.g["accessors"].append({"bufferView": self._view(data, 34962), "componentType": 5121,
                                            "count": len(joints), "type": "VEC4"})
                attributes["JOINTS_0"] = len(self.g["accessors"]) - 1
                attributes["WEIGHTS_0"] = self._vec(weights)
            out.append({"attributes": attributes, "indices": indices, "material": mat})
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

    def _floats(self, values, kind):
        # an accessor of float tuples (or scalars) with no buffer target, for skins and animations
        n = 1 if kind == "SCALAR" else len(values[0])
        flat = values if n == 1 else [x for v in values for x in v]
        while len(self.bin) % 4: self.bin.append(0)
        self.g["bufferViews"].append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": 4 * len(flat)})
        self.bin += struct.pack("<%df" % len(flat), *flat)
        acc = {"bufferView": len(self.g["bufferViews"]) - 1, "componentType": 5126, "count": len(values), "type": kind}
        if kind == "SCALAR":
            acc["min"], acc["max"] = [min(values)], [max(values)]
        self.g["accessors"].append(acc)
        return len(self.g["accessors"]) - 1

    def skin(self, joints, inverse_binds):
        self.g.setdefault("skins", []).append({"joints": joints, "inverseBindMatrices": self._floats(inverse_binds, "MAT4")})
        return len(self.g["skins"]) - 1

    def animation(self, name, times, rotations):
        # rotations: node -> one quaternion per time
        a = {"name": name, "samplers": [], "channels": []}
        t = self._floats(times, "SCALAR")
        for node, quats in rotations.items():
            a["samplers"].append({"input": t, "output": self._floats(quats, "VEC4"), "interpolation": "LINEAR"})
            a["channels"].append({"sampler": len(a["samplers"]) - 1, "target": {"node": node, "path": "rotation"}})
        self.g.setdefault("animations", []).append(a)

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

def tube(points, radii, sides=8):
    # a tube through `points`, one ring per point (u once around, v along it)
    pos, nrm, uv, idx = [], [], [], []
    length = 0
    for k, (c, r) in enumerate(zip(points, radii)):
        d = [points[min(k + 1, len(points) - 1)][i] - points[max(k - 1, 0)][i] for i in range(3)]
        dl = math.sqrt(sum(x * x for x in d))
        d = [x / dl for x in d]
        ref = (0, 0, 1) if abs(d[2]) < 0.9 else (1, 0, 0)
        u = (d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0])
        ul = math.sqrt(sum(x * x for x in u))
        u = [x / ul for x in u]
        # u x w = -d: the rings wind as cylinder()'s
        w = (u[1] * d[2] - u[2] * d[1], u[2] * d[0] - u[0] * d[2], u[0] * d[1] - u[1] * d[0])
        if k:
            length += math.sqrt(sum((c[i] - points[k - 1][i]) ** 2 for i in range(3)))
        for i in range(sides + 1):
            a = 2 * math.pi * i / sides
            n = tuple(math.cos(a) * u[j] + math.sin(a) * w[j] for j in range(3))
            pos.append(tuple(c[j] + r * n[j] for j in range(3)))
            nrm.append(n)
            uv.append((i / sides, length / (2 * math.pi * radii[0])))
    n = sides + 1
    for k in range(len(points) - 1):
        for i in range(sides):
            a = k * n + i
            idx += [a, a + n, a + 1, a + 1, a + n, a + n + 1]
    return pos, nrm, uv, idx

def joined(meshes):
    pos, nrm, uv, idx = [], [], [], []
    for p, n, t, i in meshes:
        idx += [x + len(pos) for x in i]
        pos += p; nrm += n; uv += t
    return pos, nrm, uv, idx

def blend(a, b, t):
    # four joint slots: joints a and b mixed by t (clamped to 0..1)
    t = max(0.0, min(1.0, t))
    return (a, b, 0, 0), (1 - t, t, 0.0, 0.0)

def creature(path):
    # A four-legged grazer facing +x, every part skinned to one shared skeleton: bodies, heads (with their
    # necks), legs (all four) and tails. Its idle animation lowers the head to graze and swishes the tail
    rng = random.Random(3)
    g = Glb()
    hide = g.material("hide", (0.55, 0.45, 0.32))
    # joints: name, parent, model-space position (translations only, so the inverse binds are too)
    joints = [("SKELETON", None, (0, 0, 0)), ("HIPS", 0, (-0.2, 0.9, 0)), ("SPINE", 1, (0.35, 0.95, 0)),
              ("NECK", 2, (0.7, 1.05, 0)), ("HEAD", 3, (1.0, 1.32, 0)),
              ("TAIL1", 1, (-0.75, 1.02, 0)), ("TAIL2", 5, (-1.0, 0.9, 0))]
    legs = [("FL", 2, (0.45, 0.85, 0.2)), ("FR", 2, (0.45, 0.85, -0.2)),
            ("BL", 1, (-0.5, 0.85, 0.2)), ("BR", 1, (-0.5, 0.85, -0.2))]
    for name, parent, (x, y, z) in legs:
        joints.append(("HIP_" + name, parent, (x, y, z)))
        joints.append(("KNEE_" + name, len(joints) - 1, (x, 0.45, z)))
    at = {name: k for k, (name, _, _) in enumerate(joints)}
    nodes = []
    for name, parent, p in joints:
        local = p if parent is None else tuple(p[i] - joints[parent][2][i] for i in range(3))
        nodes.append(g.node(name, None, local, None, None if parent is None else nodes[parent]))
    skin = g.skin(nodes, [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1) for _, _, p in joints])

    def part(name, mesh, weigh):
        js, ws = zip(*[weigh(p) for p in mesh[0]])
        n = g.node(name, g.mesh(name, [(mesh, hide, (js, ws))]))
        g.g["nodes"][n]["skin"] = skin

    # bodies: the hips behind, the spine in front
    for name, size in (("BODY_A", (0.8, 0.4, 0.38)), ("BODY_B", (1.0, 0.33, 0.33))):
        body = moved(stretched(blob(1, (0, 0, 0), 1, 0.06, rng), size), (0, 0.98, 0))
        part(name, body, lambda p: blend(at["HIPS"], at["SPINE"], (p[0] + 0.3) / 0.6))
    # heads with their necks: the neck bends at NECK, the head follows HEAD
    neck = [joints[at["NECK"]][2], (0.85, 1.18, 0), joints[at["HEAD"]][2]]
    for name, size, r in (("HEAD_A", (0.3, 0.18, 0.17), 0.12), ("HEAD_B", (0.24, 0.24, 0.22), 0.15)):
        head = moved(stretched(blob(1, (0, 0, 0), 1, 0.08, rng), size), (1.1, 1.3, 0))
        mesh = joined([tube(neck, [r * 1.3, r * 1.1, r]), head])
        part(name, mesh, lambda p: blend(at["NECK"], at["HEAD"], (p[0] - 0.8) / 0.2))
    # legs: hip to foot, bending at the knee
    for name, r0, r1 in (("LEGS_A", 0.1, 0.05), ("LEGS_B", 0.14, 0.09)):
        meshes = []
        for leg, _, (x, y, z) in legs:
            points = [(x, y + 0.05, z), (x, 0.65, z), (x, 0.45, z), (x, 0.22, z), (x, 0.0, z)]
            meshes.append(tube(points, [r0, r0 * 0.9, (r0 + r1) / 2, r1, r1]))

        def weigh(p):
            leg = ("F" if p[0] > 0 else "B") + ("L" if p[2] > 0 else "R")
            return blend(at["HIP_" + leg], at["KNEE_" + leg], (0.55 - p[1]) / 0.2)
        part(name, joined(meshes), weigh)
    # tails: TAIL1 at the root, TAIL2 towards the tip
    tail = [joints[at["TAIL1"]][2], joints[at["TAIL2"]][2], (-1.15, 0.6, 0)]
    tuft = moved(stretched(blob(1, (0, 0, 0), 1, 0.2, rng), (0.12, 0.2, 0.12)), (-1.1, 0.72, 0))
    for name, mesh in (("TAIL_A", tube(tail, [0.06, 0.04, 0.02], 6)),
                       ("TAIL_B", joined([tube(tail[:2], [0.07, 0.05], 6), tuft]))):
        part(name, mesh, lambda p: blend(at["TAIL1"], at["TAIL2"], (-0.8 - p[0]) / 0.2))
    # idle: grazing (the neck down, the head nodding), the tail swishing, the spine breathing, a front leg
    # stepping forward to graze and back, a hind leg shuffling and the weight shifting between the sides.
    # +z swings a foot forward (+x); front knees fold back (-z), hind knees forward (+z)
    times = [0.0, 0.8, 1.6, 2.4, 3.2, 4.0]
    about_x = lambda deg: axis_quat((1, 0, 0), deg)
    about_z = lambda deg: axis_quat((0, 0, 1), deg)
    about_y = lambda deg: axis_quat((0, 1, 0), deg)
    g.animation("idle", times, {
        nodes[at["NECK"]]: [about_z(a) for a in (0, -35, -55, -55, -20, 0)],
        nodes[at["HEAD"]]: [about_z(a) for a in (0, -10, 10, -5, 5, 0)],
        nodes[at["SPINE"]]: [about_z(a) for a in (0, 2, 0, 2, 0, 0)],
        nodes[at["HIPS"]]: [about_x(a) for a in (0, 3, 3, -3, -3, 0)],
        nodes[at["HIP_FL"]]: [about_z(a) for a in (0, 35, 18, 30, 0, 0)],
        nodes[at["KNEE_FL"]]: [about_z(a) for a in (0, -75, 0, -80, 0, 0)],
        nodes[at["HIP_FR"]]: [about_z(a) for a in (0, -5, -5, 0, 0, 0)],
        nodes[at["HIP_BR"]]: [about_z(a) for a in (0, 0, 0, -25, 8, 0)],
        nodes[at["KNEE_BR"]]: [about_z(a) for a in (0, 0, 0, 60, 0, 0)],
        nodes[at["TAIL1"]]: [about_y(a) for a in (0, 30, -30, 30, -30, 0)],
        nodes[at["TAIL2"]]: [about_y(a) for a in (0, 25, -25, 25, -25, 0)],
    })
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


def box(lo, hi):
    # an axis-aligned box with flat faces, uv in metres
    pos, nrm, uv, idx = [], [], [], []
    for axis in range(3):
        for side in (0, 1):
            n = [0, 0, 0]
            n[axis] = 1 if side else -1
            u, v = (axis + 1) % 3, (axis + 2) % 3
            base = len(pos)
            for a, b in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = [0, 0, 0]
                p[axis] = hi[axis] if side else lo[axis]
                p[u] = hi[u] if a else lo[u]
                p[v] = hi[v] if b else lo[v]
                pos.append(tuple(p))
                nrm.append(tuple(n))
                uv.append((p[u], p[v]))
            idx += [base, base + 1, base + 2, base, base + 2, base + 3] if side else [base, base + 2, base + 1, base, base + 3, base + 2]
    return pos, nrm, uv, idx

def building_parts(folder):
    # the outpost's parts, one model each, on a 4 m grid: a wall stands on the cell's back edge, its outer face on
    # the edge (z = 2), so walls of neighbouring cells meet without overlapping
    plaster, trim, glass = (0.78, 0.74, 0.66), (0.36, 0.3, 0.26), (0.35, 0.5, 0.58)
    def save(name, boxes):
        g = Glb()
        mats = {}
        prims = []
        for lo, hi, colour in boxes:
            if colour not in mats: mats[colour] = g.material("m%d" % len(mats), colour)
            prims.append((box(lo, hi), mats[colour]))
        g.node(name.upper(), g.mesh(name.upper(), prims))
        g.save(os.path.join(folder, name + ".glb"))
    save("floor", [((-2, 0, -2), (2, 0.2, 2), trim)])
    save("roof", [((-2.2, 3.8, -2.2), (2.2, 4.0, 2.2), trim)])
    save("wall", [((-2, 0.2, 1.8), (2, 3.8, 2), plaster)])
    save("door", [((-2, 0.2, 1.8), (-0.6, 3.8, 2), plaster), ((0.6, 0.2, 1.8), (2, 3.8, 2), plaster),
                  ((-0.6, 2.6, 1.8), (0.6, 3.8, 2), plaster), ((-0.7, 0.2, 1.75), (-0.6, 2.6, 2.05), trim),
                  ((0.6, 0.2, 1.75), (0.7, 2.6, 2.05), trim)])
    save("window", [((-2, 0.2, 1.8), (2, 1.4, 2), plaster), ((-2, 2.6, 1.8), (2, 3.8, 2), plaster),
                    ((-2, 1.4, 1.8), (-0.8, 2.6, 2), plaster), ((0.8, 1.4, 1.8), (2, 2.6, 2), plaster),
                    ((-0.8, 1.4, 1.88), (0.8, 2.6, 1.92), glass)])

def building_modules(folder):
    # module scenes: locators only, named <PART>_<n>, turned to each side of the cell
    sides = [(0, 0), (90, 0), (180, 0), (270, 0)]
    def save(name, parts):
        g = Glb()
        for i, (part, yaw) in enumerate(parts):
            g.node("%s_%d" % (part, i + 1), None, (0, 0, 0), axis_quat((0, 1, 0), yaw) if yaw else None)
        g.save(os.path.join(folder, name + ".glb"))
    save("room_door", [("FLOOR", 0), ("ROOF", 0), ("DOOR", 0), ("WALL", 90), ("WINDOW", 180), ("WALL", 270)])
    save("room_hall", [("FLOOR", 0), ("ROOF", 0), ("DOOR", 0), ("WINDOW", 90), ("DOOR", 180), ("WINDOW", 270)])
    save("loft", [("ROOF", 0), ("WINDOW", 0), ("WALL", 90), ("WINDOW", 180), ("WALL", 270)])

def watchtower(folder):
    # the tower's levels: each a scene with its mesh and the locators the L-system snaps levels on
    stone, wood = (0.5, 0.48, 0.45), (0.42, 0.3, 0.2)
    def save(name, boxes, into, out):
        g = Glb()
        mats = {}
        prims = []
        for lo, hi, colour in boxes:
            if colour not in mats: mats[colour] = g.material("m%d" % len(mats), colour)
            prims.append((box(lo, hi), mats[colour]))
        g.node(name.upper(), g.mesh(name.upper(), prims))
        if into: g.node("LEVEL_IN_1", None, (0, 0, 0))
        if out: g.node("LEVEL_OUT_1", None, (0, out, 0))
        g.save(os.path.join(folder, name + ".glb"))
    save("tower_base", [((-1.8, 0, -1.8), (1.8, 3, 1.8), stone)], False, 3)
    save("tower_mid", [((-1.3, 0, -1.3), (1.3, 3, 1.3), stone), ((-1.4, 2.7, -1.4), (1.4, 3, 1.4), wood)], True, 3)
    posts = [((x - 0.1, 0.3, z - 0.1), (x + 0.1, 2.6, z + 0.1), wood) for x in (-1.7, 1.7) for z in (-1.7, 1.7)]
    save("tower_top", [((-1.9, 0, -1.9), (1.9, 0.3, 1.9), wood), ((-2.1, 2.6, -2.1), (2.1, 2.8, 2.1), wood)] + posts
         + [((-1.9, 0.3, z - 0.05), (1.9, 1.2, z + 0.05), wood) for z in (-1.85, 1.85)], True, 0)

def craft(folder):
    # parts sit apart along x; locators turn a wheel's or fin's +y outward
    paint, dark = (0.62, 0.2, 0.16), (0.12, 0.12, 0.13)
    def model(name, bodies, extras, spots):
        g = Glb()
        mats = {"paint": g.material("paint", paint), "dark": g.material("dark", dark)}
        x = 0
        for body, boxes in bodies:
            b = g.node(body, g.mesh(body, [(box(lo, hi), mats[m]) for lo, hi, m in boxes]), (x, 0, 0))
            for i, (t, r) in enumerate(spots(body)):
                g.node("%s_L%d" % (body, i + 1), None, t, r, b)
            x += 6
        for part, prims in extras:
            g.node(part, g.mesh(part, [(p, mats[m]) for p, m in prims]), (x, 0, 0))
            x += 3
        g.save(os.path.join(folder, name + ".glb"))
    # a wheel at each corner (x ahead, z to the side), its axle (+y) turned outward
    wheels = lambda half, side: [((x, 0.45, s * side), axis_quat((1, 0, 0), s * 90)) for x in (-half, half) for s in (-1, 1)]
    rover_spots = lambda body: wheels(1.0, 1.0) if body.endswith("A") else wheels(1.3, 1.2)
    model("rover", [("ROVER_BODY_A", [((-1.6, 0.5, -0.9), (1.6, 1.3, 0.9), "paint"), ((-0.6, 1.3, -0.7), (0.9, 1.9, 0.7), "dark")]),
                    ("ROVER_BODY_B", [((-1.9, 0.5, -1.1), (1.9, 1.1, 1.1), "paint"), ((-1.4, 1.1, -0.9), (0.2, 2.0, 0.9), "paint")])],
          [("WHEEL_A", [(cylinder(12, 0.35, 0.45, 0.45), "dark")]), ("WHEEL_B", [(cylinder(6, 0.5, 0.5, 0.5), "dark")])], rover_spots)
    fin_spots = lambda body: [((-1.5, 0.9, s * 0.8), axis_quat((1, 0, 0), s * 60)) for s in (-1, 1)] + [((-1.8, 1.2, 0), None)]
    model("skiff", [("SKIFF_HULL_A", [((-2.0, 0.4, -0.7), (2.4, 1.2, 0.7), "paint"), ((0.2, 1.2, -0.5), (1.6, 1.7, 0.5), "dark")]),
                    ("SKIFF_HULL_B", [((-2.4, 0.5, -0.5), (2.8, 1.0, 0.5), "paint"), ((-0.6, 1.0, -0.4), (1.0, 1.4, 0.4), "dark")])],
          [("FIN_A", [(box((-0.4, 0, -0.05), (0.4, 1.0, 0.05)), "paint")]), ("FIN_B", [(box((-0.6, 0, -0.05), (0.2, 0.7, 0.05)), "dark")])],
          fin_spots)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
tree(out + "/tree.glb")
rock(out + "/rock.glb")
creature(out + "/creature.glb")
textures(out + "/textures")
os.makedirs(out + "/buildings", exist_ok=True)
building_parts(out + "/buildings")
building_modules(out + "/buildings")
watchtower(out + "/buildings")
os.makedirs(out + "/craft", exist_ok=True)
craft(out + "/craft")
print("wrote", out + "/tree.glb", out + "/rock.glb", out + "/creature.glb", out + "/textures", out + "/buildings", out + "/craft")
