# Writes the example project's test models (tree.glb, rock.glb) beside this script: parts as named
# nodes with locator children, for the procmodel descriptors tree.json and rock.json.
import json, math, os, random, struct, sys

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
    pos, nrm, idx = [], [], []
    for ring, (y, r) in enumerate(((0, r0), (h, r1))):
        for i in range(sides + 1):
            a = 2 * math.pi * i / sides
            pos.append((r * math.cos(a), y, r * math.sin(a)))
            nrm.append((math.cos(a), (r0 - r1) / h, math.sin(a)))
    n = sides + 1
    for i in range(sides):
        idx += [i, n + i, i + 1, i + 1, n + i, n + i + 1]
    return pos, nrm, idx

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
    idx = [i for tri in f for i in (tri[0], tri[2], tri[1])]
    return pos, nrm, idx

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

    def _vec3(self, values):
        data = b"".join(struct.pack("<3f", *p) for p in values)
        acc = {"bufferView": self._view(data, 34962), "componentType": 5126, "count": len(values), "type": "VEC3"}
        acc["min"] = [min(p[i] for p in values) for i in range(3)]
        acc["max"] = [max(p[i] for p in values) for i in range(3)]
        self.g["accessors"].append(acc)
        return len(self.g["accessors"]) - 1

    def mesh(self, name, prims):
        out = []
        for (pos, nrm, idx), mat in prims:
            ia = {"bufferView": self._view(struct.pack("<%dI" % len(idx), *idx), 34963), "componentType": 5125,
                  "count": len(idx), "type": "SCALAR"}
            self.g["accessors"].append(ia)
            indices = len(self.g["accessors"]) - 1
            out.append({"attributes": {"POSITION": self._vec3(pos), "NORMAL": self._vec3(nrm)},
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

out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
tree(out + "/tree.glb")
rock(out + "/rock.glb")
print("wrote", out + "/tree.glb", out + "/rock.glb")
