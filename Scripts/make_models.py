"""Builds the three monsters in Blender and exports each animated part as an FBX file.

    blender --background --factory-startup --python Scripts/make_models.py -- <output folder> [preview.png]

Scripts/make-assets.sh runs this and then imports the results into Content/Models.
Everything is modelled here from code, in metres, facing +X, standing on Z = 0.

Colours travel in the material names: "c_RRGGBB_gN" is an sRGB colour and a glow strength.
The game reads those names and paints each slot itself, so no material assets are exported.
The part names must match HG::ModelNames in the C++ code, and the pivots in PIVOTS must match
the places HGMonster.cpp attaches the parts.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector, noise

# Where each part's origin sits on the assembled monster, in metres.
PIVOTS = {
    "Gary_Body": (0.0, 0.0, 0.0),
    "Gary_Head": (0.0, 0.0, 2.26),
    "Gary_Arm": (0.0, 0.27, 2.02),
    "Librarian_Body": (0.0, 0.0, 0.0),
    "Librarian_Head": (0.0, 0.0, 2.20),
    "Librarian_Arm": (0.0, 0.24, 1.93),
    "Librarian_Shush": (0.0, 0.24, 1.93),
    "Mouth_Bottom": (0.0, 0.0, 0.0),
    "Mouth_Top": (-0.60, 0.0, 1.00),
}


# ------------------------------------------------------------------------------------------
# Materials and primitives

def material(hex_colour, glow=0):
    name = f"c_{hex_colour}_g{glow}"
    found = bpy.data.materials.get(name)
    if found:
        return found
    made = bpy.data.materials.new(name)
    made.use_nodes = True
    made.use_backface_culling = True
    srgb = [int(hex_colour[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    linear = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in srgb]
    shader = next(n for n in made.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    for socket in shader.inputs:
        if socket.identifier == "Base Color":
            socket.default_value = (*linear, 1.0)
        elif socket.identifier == "Roughness":
            socket.default_value = 0.65
        elif socket.identifier == "Emission Color" and glow:
            socket.default_value = (*linear, 1.0)
        elif socket.identifier == "Emission Strength" and glow:
            socket.default_value = min(float(glow), 8.0)
    made.diffuse_color = (*linear, 1.0)
    return made


def to_object(bm, mat):
    mesh = bpy.data.meshes.new("part")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(mat)
    obj = bpy.data.objects.new("part", mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def placed(location, rotation=None, scale=(1.0, 1.0, 1.0)):
    turn = Euler(rotation or (0.0, 0.0, 0.0), "XYZ").to_matrix().to_4x4()
    return Matrix.Translation(Vector(location)) @ turn @ Matrix.Diagonal((*scale, 1.0))


def ellipsoid(centre, radii, mat, rotation=None, segments=20, rings=12):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=1.0)
    bm.transform(placed(centre, rotation, radii))
    return to_object(bm, mat)


def sphere(centre, radius, mat, segments=14, rings=8):
    return ellipsoid(centre, (radius, radius, radius), mat, None, segments, rings)


def hemisphere(centre, radii, mat, upper, inside_out=False, lumps=0.0):
    """Half an ellipsoid, open at the equator. inside_out makes it a bowl seen from the open side."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=28, v_segments=14, radius=1.0)
    doomed = [v for v in bm.verts if (v.co.z < -1e-5 if upper else v.co.z > 1e-5)]
    bmesh.ops.delete(bm, geom=doomed, context="VERTS")
    if lumps:
        for v in bm.verts:
            # Lumpy away from the rim, exact at the rim so the lips still fit.
            weight = min(1.0, abs(v.co.z) * 3.0)
            v.co *= 1.0 + lumps * weight * noise.noise(v.co * 3.1 + Vector((7.0, 3.0, 1.0)))
    if inside_out:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.transform(placed(centre, None, radii))
    return to_object(bm, mat)


def box(centre, size, mat, rotation=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bm.transform(placed(centre, rotation, size))
    return to_object(bm, mat)


def aim(direction):
    """Rotation that points +Z along direction."""
    return Vector((0.0, 0.0, 1.0)).rotation_difference(Vector(direction).normalized()).to_matrix().to_4x4()


def limb(start, end, radius_start, radius_end, mat, segments=12, round_ends=True):
    """A tapered tube from start to end with rounded ends: arms, legs, fingers, stalks."""
    start = Vector(start)
    end = Vector(end)
    along = end - start
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=not round_ends, cap_tris=False, segments=segments,
                          radius1=radius_start, radius2=radius_end, depth=along.length)
    bm.transform(Matrix.Translation((start + end) * 0.5) @ aim(along))
    if round_ends:
        for at, radius in ((start, radius_start), (end, radius_end)):
            cap = bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=6, radius=radius)
            bmesh.ops.transform(bm, matrix=Matrix.Translation(at) @ aim(along), verts=cap["verts"])
    return to_object(bm, mat)


def spike(base, direction, radius, length, mat):
    """A cone standing on base: teeth and claws."""
    direction = Vector(direction).normalized()
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=8, radius1=radius, radius2=radius * 0.12, depth=length)
    bm.transform(Matrix.Translation(Vector(base) + direction * (length * 0.5)) @ aim(direction))
    return to_object(bm, mat)


def skirt(radius_bottom, radius_top, height, mat, pleats=16):
    """An open cone with a zigzag hem."""
    bm = bmesh.new()
    count = pleats * 2
    rows = 6
    grid = []
    for row in range(rows + 1):
        t = row / rows
        ring = []
        for i in range(count):
            angle = math.tau * i / count
            fold = 1.0 - 0.10 * (1.0 - t) * (i % 2)
            # Flares out near the floor like a bell.
            radius = (radius_top + (radius_bottom - radius_top) * (1.0 - t) ** 1.35) * fold
            ring.append(bm.verts.new((radius * math.cos(angle), radius * math.sin(angle), height * t)))
        grid.append(ring)
    for row in range(rows):
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((grid[row][i], grid[row][j], grid[row + 1][j], grid[row + 1][i]))
    return to_object(bm, mat)


def ring(centre, major, minor, mat, rotation=None, scale=(1.0, 1.0, 1.0), arc=(0.0, math.tau), major_segments=36, minor_segments=8):
    """A torus around Z, or part of one."""
    bm = bmesh.new()
    whole = abs(arc[1] - arc[0]) >= math.tau - 1e-6
    steps = major_segments if whole else max(3, int(major_segments * abs(arc[1] - arc[0]) / math.tau))
    grid = []
    for i in range(steps if whole else steps + 1):
        u = arc[0] + (arc[1] - arc[0]) * i / steps
        loop = []
        for j in range(minor_segments):
            v = math.tau * j / minor_segments
            r = major + minor * math.cos(v)
            loop.append(bm.verts.new((r * math.cos(u), r * math.sin(u), minor * math.sin(v))))
        grid.append(loop)
    spans = steps if whole else steps
    for i in range(spans):
        a = grid[i]
        b = grid[(i + 1) % len(grid)]
        for j in range(minor_segments):
            k = (j + 1) % minor_segments
            bm.faces.new((a[j], b[j], b[k], a[k]))
    if not whole:
        bm.faces.new(reversed(grid[0]))
        bm.faces.new(grid[-1])
    bm.transform(placed(centre, rotation, scale))
    return to_object(bm, mat)


def disc(centre, radius, thickness, mat, rotation=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=20, radius1=radius, radius2=radius, depth=thickness)
    bm.transform(placed(centre, rotation))
    return to_object(bm, mat)


def finish(name, parts):
    """Joins parts into one mesh called SM_<name> with its origin at the part's pivot."""
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = f"SM_{name}"
    obj.data.name = f"SM_{name}"
    obj.data.transform(Matrix.Translation(-Vector(PIVOTS[name])))
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    obj.data.set_sharp_from_angle(angle=math.radians(50.0))
    obj.data.update()
    return obj


# ------------------------------------------------------------------------------------------
# Gary: a shop mannequin, far too tall, with a name tag and a smile somebody drew on.

def build_gary():
    shell = material("2A2A30")
    joint = material("6E6E78")
    eye = material("FFFFFF", 60)
    tag = material("F2F2EA", 1)
    red = material("C81E1E")
    smile = material("E8E0D0", 3)

    body = []
    for side in (-1.0, 1.0):
        body.append(ellipsoid((0.07, side * 0.13, 0.045), (0.15, 0.065, 0.045), shell))
        body.append(sphere((0.0, side * 0.13, 0.10), 0.045, joint))
        body.append(limb((0.0, side * 0.13, 0.13), (0.01, side * 0.13, 0.61), 0.042, 0.055, shell))
        body.append(sphere((0.01, side * 0.13, 0.66), 0.065, joint))
        body.append(limb((0.01, side * 0.13, 0.71), (0.0, side * 0.115, 1.22), 0.06, 0.08, shell))
        body.append(sphere((0.0, side * 0.27, 2.02), 0.078, joint))
    body.append(ellipsoid((0.0, 0.0, 1.30), (0.14, 0.20, 0.13), shell))
    body.append(sphere((0.0, 0.0, 1.45), 0.085, joint))
    body.append(ellipsoid((0.0, 0.0, 1.60), (0.115, 0.165, 0.17), shell))
    body.append(ellipsoid((0.0, 0.0, 1.88), (0.155, 0.25, 0.26), shell))
    body.append(limb((0.0, 0.0, 2.08), (0.0, 0.0, 2.27), 0.05, 0.042, joint))
    # HELLO my name is
    body.append(box((0.146, 0.10, 1.925), (0.012, 0.10, 0.05), tag))
    body.append(box((0.147, 0.10, 1.96), (0.012, 0.10, 0.022), red))
    finish("Gary_Body", body)

    head = [
        ellipsoid((0.01, 0.0, 2.44), (0.13, 0.115, 0.165), shell),
        ellipsoid((0.025, 0.0, 2.37), (0.105, 0.095, 0.12), shell),
    ]
    for side in (-1.0, 1.0):
        head.append(sphere((0.128, side * 0.045, 2.47), 0.017, eye))
    head.append(ring((0.128, 0.0, 2.405), 0.055, 0.007, smile, rotation=(0.0, math.radians(90.0), 0.0),
                     arc=(math.radians(-62.0), math.radians(62.0))))
    finish("Gary_Head", head)

    # One arm, hanging from the shoulder. The game mirrors nothing: both sides use it as it is.
    sx, sy, sz = PIVOTS["Gary_Arm"]
    def at(x, y, z):
        return (sx + x, sy + y, sz + z)
    arm = [
        limb(at(0.0, 0.0, -0.06), at(0.0, 0.0, -0.50), 0.05, 0.042, shell),
        sphere(at(0.0, 0.0, -0.55), 0.052, joint),
        limb(at(0.0, 0.0, -0.60), at(0.03, 0.0, -1.05), 0.042, 0.032, shell),
        sphere(at(0.035, 0.0, -1.09), 0.038, joint),
        ellipsoid(at(0.04, 0.0, -1.17), (0.028, 0.05, 0.07), shell),
        limb(at(0.045, 0.045, -1.14), at(0.08, 0.085, -1.25), 0.012, 0.008, shell, 8),
    ]
    for i, y in enumerate((-0.036, -0.012, 0.012, 0.036)):
        arm.append(limb(at(0.04, y, -1.22), at(0.06 + 0.012 * i, y * 1.6, -1.43 + abs(y) * 0.8), 0.011, 0.007, shell, 8))
    finish("Gary_Arm", arm)


# ------------------------------------------------------------------------------------------
# The Librarian: a long dress, a cardigan, and an open book where the head should be.

def build_librarian():
    dress = material("45554A")
    cardigan = material("8A6A2A")
    paper = material("EDE8D3")
    skin = material("BDB8A8")
    cover = material("8E2F2F")
    ink = material("2A2620")
    lens = material("B4FF96", 30)
    frame = material("1A1A1A")
    ribbon = material("C8202A", 1)

    body = [
        skirt(0.52, 0.13, 1.52, dress),
        ellipsoid((0.0, 0.0, 1.70), (0.14, 0.20, 0.30), cardigan),
        ellipsoid((0.0, 0.0, 1.90), (0.12, 0.26, 0.10), cardigan),
        ring((0.0, 0.0, 1.50), 0.135, 0.022, ink),
        ring((0.0, 0.0, 1.99), 0.085, 0.03, paper, scale=(1.0, 1.0, 0.6)),
        limb((0.0, 0.0, 1.98), (0.0, 0.0, 2.22), 0.04, 0.035, skin),
    ]
    for z, x in ((1.58, 0.130), (1.70, 0.142), (1.82, 0.130)):
        body.append(sphere((x, 0.0, z), 0.018, paper, 10, 6))
    for side in (-1.0, 1.0):
        body.append(sphere((0.0, side * 0.24, 1.93), 0.075, cardigan))
    finish("Librarian_Body", body)

    # The book is open towards you: spine at the back, pages forward, glasses resting on the pages.
    hinge = Vector((-0.06, 0.0, 2.47))
    lean = math.radians(20.0)
    head = [box((-0.075, 0.0, 2.47), (0.03, 0.07, 0.52), cover)]
    for side in (-1.0, 1.0):
        along = Vector((math.sin(lean), side * math.cos(lean), 0.0))
        facing = Vector((math.cos(lean), -side * math.sin(lean), 0.0))
        turn = (0.0, 0.0, -side * lean)
        head.append(box(hinge + along * 0.165, (0.02, 0.33, 0.50), cover, turn))
        head.append(box(hinge + along * 0.155 + facing * 0.033, (0.045, 0.30, 0.46), paper, turn))
        for line in range(8):
            width = 0.22 if line % 4 != 3 else 0.12
            head.append(box(hinge + along * (0.05 + width * 0.5) + facing * 0.057 + Vector((0.0, 0.0, 0.19 - 0.05 * line)),
                            (0.004, width, 0.012), ink, turn))
        # Glasses: a rim, a lit lens, an arm back to the cover, and a stern eyebrow.
        head.append(ring((0.09, side * 0.075, 2.58), 0.06, 0.007, frame, rotation=(0.0, math.radians(90.0), 0.0), major_segments=24, minor_segments=6))
        head.append(disc((0.088, side * 0.075, 2.58), 0.056, 0.006, lens, rotation=(0.0, math.radians(90.0), 0.0)))
        head.append(limb((0.09, side * 0.135, 2.58), (-0.01, side * 0.19, 2.58), 0.005, 0.005, frame, 6))
        head.append(box((0.095, side * 0.075, 2.67), (0.008, 0.075, 0.013), frame, (side * math.radians(16.0), 0.0, 0.0)))
    head.append(limb((0.09, -0.016, 2.585), (0.09, 0.016, 2.585), 0.006, 0.006, frame, 6))
    # The bookmark hangs out of the bottom like a tongue.
    head.append(box((0.03, 0.0, 2.20), (0.006, 0.036, 0.17), ribbon, (0.0, math.radians(-12.0), 0.0)))
    finish("Librarian_Head", head)

    sx, sy, sz = PIVOTS["Librarian_Arm"]
    def at(x, y, z):
        return (sx + x, sy + y, sz + z)
    arm = [
        limb(at(0.0, 0.0, -0.03), at(0.0, 0.0, -0.48), 0.04, 0.032, cardigan),
        limb(at(0.0, 0.0, -0.48), at(0.05, 0.0, -0.92), 0.032, 0.024, cardigan),
        ellipsoid(at(0.055, 0.0, -0.99), (0.02, 0.035, 0.06), skin),
    ]
    for y in (-0.027, -0.009, 0.009, 0.027):
        arm.append(limb(at(0.055, y, -1.03), at(0.075, y * 1.5, -1.23 + abs(y) * 0.9), 0.009, 0.006, skin, 8))
    finish("Librarian_Arm", arm)

    # The other arm never comes down: one finger on its way to where her lips would be.
    # Unreal flips Y on import, so "towards her middle" is +Y here for the arm the game hangs at +Y.
    inward = 1.0
    shush = [
        limb(at(0.0, 0.0, -0.02), at(0.26, 0.02 * inward, -0.24), 0.04, 0.032, cardigan),
        limb(at(0.26, 0.02 * inward, -0.24), at(0.19, 0.19 * inward, 0.26), 0.032, 0.024, cardigan),
        ellipsoid(at(0.185, 0.205 * inward, 0.30), (0.035, 0.035, 0.045), skin),
        limb(at(0.18, 0.215 * inward, 0.33), at(0.15, 0.24 * inward, 0.52), 0.011, 0.007, skin, 8),
    ]
    finish("Librarian_Shush", shush)


# ------------------------------------------------------------------------------------------
# The Mouth: two jaws, too many teeth, two small legs and two eyes that disagree.

def build_mouth():
    flesh = material("91262F")
    lip = material("B8454F")
    lining = material("3A0508")
    throat = material("080000")
    tooth = material("E6E0C0", 1)
    gold = material("FFC830", 3)
    tongue = material("D0506A")
    white = material("F4F4F0", 2)
    pupil = material("050505")
    foot = material("5E161C")

    rx, ry, rz = 0.95, 0.90, 0.85
    centre = (0.0, 0.0, 1.0)

    def teeth(parts, upper, missing, golden):
        count = 15
        for i in range(count):
            if i in missing:
                continue
            angle = math.radians(-120.0 + 240.0 * i / (count - 1))
            length = 0.15 + 0.07 * (0.5 + 0.5 * noise.noise(Vector((i * 1.7, 2.0 if upper else 5.0, 0.0))))
            base = (rx * 0.89 * math.cos(angle), ry * 0.89 * math.sin(angle), 0.985 if upper else 1.015)
            lean = 0.14 * (1.0 if i % 3 else -1.0)
            direction = (lean * math.cos(angle), lean * math.sin(angle), -1.0 if upper else 1.0)
            parts.append(spike(base, direction, 0.062, length, gold if i == golden else tooth))

    bottom = [
        hemisphere(centre, (rx, ry, rz), flesh, upper=False, lumps=0.05),
        hemisphere(centre, (rx * 0.95, ry * 0.95, rz * 0.45), lining, upper=False, inside_out=True),
        ring((0.0, 0.0, 0.985), 1.0, 0.055, lip, scale=(rx * 0.97, ry * 0.97, 1.0)),
        ellipsoid((-0.45, 0.0, 0.95), (0.3, 0.4, 0.22), throat),
    ]
    # The three front teeth are gone, which is where the tongue hangs out.
    teeth(bottom, upper=False, missing={6, 7, 8}, golden=4)
    path = [(0.05, 0.74), (0.30, 0.82), (0.55, 0.93), (0.78, 1.04), (0.96, 1.09), (1.07, 0.99), (1.10, 0.84)]
    for i, (x, z) in enumerate(path):
        nx, nz = path[min(i + 1, len(path) - 1)]
        px, pz = path[max(i - 1, 0)]
        pitch = -math.atan2(nz - pz, nx - px)
        width = 0.30 - 0.02 * i
        bottom.append(ellipsoid((x, 0.0, z), (0.17, width, 0.06), tongue, rotation=(0.0, pitch, 0.0), segments=14, rings=8))
    for side in (-1.0, 1.0):
        bottom.append(limb((-0.05, side * 0.42, 0.32), (-0.05, side * 0.45, 0.10), 0.17, 0.15, foot))
        bottom.append(ellipsoid((0.08, side * 0.46, 0.07), (0.26, 0.18, 0.07), foot))
        for dy in (-0.11, 0.0, 0.11):
            bottom.append(spike((0.28, side * 0.46 + dy, 0.045), (1.0, 0.0, 0.1), 0.045, 0.13, tooth))
    for x, y, z, r in ((0.55, 0.62, 0.52, 0.06), (0.30, -0.78, 0.66, 0.05), (0.72, -0.35, 0.40, 0.045), (-0.2, 0.80, 0.60, 0.07)):
        bottom.append(sphere((x, y, z), r, lip, 10, 6))
    finish("Mouth_Bottom", bottom)

    top = [
        hemisphere(centre, (rx, ry, rz), flesh, upper=True, lumps=0.05),
        hemisphere(centre, (rx * 0.95, ry * 0.95, rz * 0.45), lining, upper=True, inside_out=True),
        ring((0.0, 0.0, 1.015), 1.0, 0.055, lip, scale=(rx * 0.97, ry * 0.97, 1.0)),
        limb((-0.50, 0.0, 1.30), (-0.45, 0.0, 1.13), 0.04, 0.03, tongue, 8),
        sphere((-0.45, 0.0, 1.10), 0.055, tongue, 10, 6),
    ]
    teeth(top, upper=True, missing={10}, golden=-1)
    for side, look in ((-1.0, (0.10, 0.045, 0.03)), (1.0, (0.105, 0.03, -0.035))):
        ball = Vector((0.07, side * 0.44, 2.20))
        top.append(limb((-0.05, side * 0.30, 1.76), (0.05, side * 0.42, 2.12), 0.05, 0.035, flesh, 10))
        top.append(sphere(ball, 0.13, white, 16, 10))
        top.append(sphere(ball + Vector(look), 0.06, pupil, 12, 8))
    for x, y, z, r in ((0.35, 0.55, 1.62, 0.06), (-0.45, -0.50, 1.60, 0.07), (0.60, -0.30, 1.52, 0.05)):
        top.append(sphere((x, y, z), r, lip, 10, 6))
    finish("Mouth_Top", top)


# ------------------------------------------------------------------------------------------
# Export, and an optional picture of all three standing in a row.

def export(folder):
    for name in PIVOTS:
        obj = bpy.data.objects[f"SM_{name}"]
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.fbx(filepath=os.path.join(folder, f"SM_{name}.fbx"), use_selection=True,
                                 object_types={"MESH"}, mesh_smooth_type="FACE", use_mesh_modifiers=True,
                                 bake_space_transform=False, add_leaf_bones=False)
        triangles = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        print(f"HGMODEL {name}: {triangles} triangles, {len(obj.data.materials)} colours")


def preview(path):
    """Stands the monsters up next to each other and renders them. Only for looking at."""
    def stand(name, offset, mirror=False, rotation=(0.0, 0.0, 0.0)):
        source = bpy.data.objects[f"SM_{name}"]
        copy = bpy.data.objects.new(f"preview_{name}_{mirror}", source.data)
        bpy.context.collection.objects.link(copy)
        pivot = Vector(PIVOTS[name])
        if mirror:
            pivot.y = -pivot.y
        copy.location = pivot + Vector((0.0, offset, 0.0))
        copy.rotation_euler = rotation
        return copy

    for name in PIVOTS:
        bpy.data.objects[f"SM_{name}"].hide_render = True
    stand("Gary_Body", -2.3)
    stand("Gary_Head", -2.3, rotation=(math.radians(14.0), 0.0, 0.0))
    stand("Gary_Arm", -2.3, rotation=(0.0, math.radians(-150.0), 0.0))
    stand("Gary_Arm", -2.3, mirror=True, rotation=(math.radians(20.0), math.radians(25.0), 0.0))
    stand("Librarian_Body", 0.0)
    stand("Librarian_Head", 0.0)
    stand("Librarian_Shush", 0.0, mirror=True)
    stand("Librarian_Arm", 0.0)
    stand("Mouth_Bottom", 2.6)
    stand("Mouth_Top", 2.6, rotation=(0.0, math.radians(-32.0), 0.0))

    scene = bpy.context.scene
    floor = bpy.data.meshes.new("floor")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=30.0)
    bm.to_mesh(floor)
    bm.free()
    floor.materials.append(material("3A3F3D"))
    bpy.context.collection.objects.link(bpy.data.objects.new("floor", floor))

    def lamp(kind, location, energy, rotation=(0.0, 0.0, 0.0), size=1.0, colour=(1.0, 1.0, 1.0)):
        data = bpy.data.lights.new(kind, kind)
        data.energy = energy
        data.color = colour
        if kind == "AREA":
            data.size = size
        obj = bpy.data.objects.new(kind, data)
        obj.location = location
        obj.rotation_euler = rotation
        bpy.context.collection.objects.link(obj)

    lamp("AREA", (5.0, -3.0, 4.5), 1800.0, (math.radians(-35.0), math.radians(50.0), 0.0), 4.0, (1.0, 0.95, 0.85))
    lamp("AREA", (3.0, 5.0, 2.5), 700.0, (math.radians(60.0), math.radians(40.0), 0.0), 3.0, (0.7, 0.85, 1.0))
    lamp("AREA", (-4.0, 0.0, 3.0), 500.0, (0.0, math.radians(-60.0), 0.0), 4.0, (1.0, 0.5, 0.4))

    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.data.lens = 40.0
    camera.location = (8.2, 0.9, 1.75)
    camera.rotation_euler = (math.radians(87.0), 0.0, math.radians(90.0))
    bpy.context.collection.objects.link(camera)
    scene.camera = camera

    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.012, 0.014, 0.016, 1.0)
    scene.world = world
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 900
    scene.render.filepath = path
    scene.render.image_settings.file_format = "PNG"
    for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES"):
        try:
            scene.render.engine = engine
            break
        except TypeError:
            continue
    if scene.render.engine == "CYCLES":
        scene.cycles.samples = 48
    bpy.ops.render.render(write_still=True)
    print(f"HGMODEL preview {path} ({scene.render.engine})")


def main():
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not arguments:
        sys.exit("usage: blender --background --python make_models.py -- <output folder> [preview.png]")
    folder = arguments[0]
    os.makedirs(folder, exist_ok=True)

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    build_gary()
    build_librarian()
    build_mouth()
    export(folder)
    if len(arguments) > 1:
        preview(arguments[1])


main()
