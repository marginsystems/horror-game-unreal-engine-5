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
    "Gary_Arm": (0.0, 0.25, 2.02),
    "Librarian_Body": (0.0, 0.0, 0.0),
    "Librarian_Head": (0.0, 0.0, 2.20),
    "Librarian_Arm": (0.0, 0.24, 1.93),
    "Librarian_Shush": (0.0, 0.24, 1.93),
    "Mouth_Bottom": (0.0, 0.0, 0.0),
    "Mouth_Top": (-0.60, 0.0, 1.00),
    "Toy_Sorter": (0.0, 0.0, 0.0),
    "Toy_Circle": (0.0, 0.0, 0.0),
    "Toy_Square": (0.0, 0.0, 0.0),
    "Toy_Triangle": (0.0, 0.0, 0.0),
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
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
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


def fuse(parts, mat, voxel=0.018, lumps=0.004):
    """Melts overlapping parts into one skin, so a body reads as flesh and not as stacked shapes."""
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    remesh = obj.modifiers.new("fuse", "REMESH")
    remesh.mode = "VOXEL"
    remesh.voxel_size = voxel
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    soften = obj.modifiers.new("soften", "SMOOTH")
    soften.factor = 0.7
    soften.iterations = 8
    bpy.ops.object.modifier_apply(modifier=soften.name)
    if lumps:
        for vertex in obj.data.vertices:
            bump = noise.noise(vertex.co * 11.0) + 0.5 * noise.noise(vertex.co * 27.0)
            vertex.co += vertex.normal * lumps * bump
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return obj


# ------------------------------------------------------------------------------------------
# Gary: a starved, grey thing the height of a doorway, with a name tag somebody pinned on it.

def build_gary():
    skin = material("B8B0A2")
    dark = material("070505")
    glint = material("FFF4E0", 40)
    tooth = material("CFC6A4")
    tag = material("E8E4D8", 1)
    red = material("8E1818")

    flesh = []
    for side in (-1.0, 1.0):
        flesh.append(ellipsoid((0.09, side * 0.11, 0.035), (0.15, 0.05, 0.035), skin))
        flesh.append(limb((0.0, side * 0.11, 0.06), (0.03, side * 0.115, 0.62), 0.03, 0.04, skin))
        flesh.append(sphere((0.035, side * 0.115, 0.64), 0.055, skin))
        flesh.append(limb((0.03, side * 0.115, 0.66), (-0.02, side * 0.10, 1.20), 0.042, 0.06, skin))
        flesh.append(sphere((-0.02, side * 0.13, 1.25), 0.075, skin))
        flesh.append(sphere((-0.03, side * 0.25, 2.02), 0.06, skin))
        flesh.append(limb((-0.01, side * 0.04, 2.06), (-0.03, side * 0.25, 2.03), 0.03, 0.03, skin))
    flesh.append(ellipsoid((-0.02, 0.0, 1.28), (0.09, 0.15, 0.09), skin))
    # A hunched spine you can count, a hollow belly and a cage of ribs.
    for x, z in ((-0.06, 1.30), (-0.09, 1.42), (-0.11, 1.54), (-0.12, 1.66), (-0.12, 1.78), (-0.105, 1.90), (-0.08, 2.00), (-0.05, 2.09)):
        flesh.append(sphere((x, 0.0, z), 0.05, skin))
    flesh.append(ellipsoid((-0.04, 0.0, 1.50), (0.07, 0.10, 0.17), skin))
    flesh.append(ellipsoid((-0.02, 0.0, 1.86), (0.12, 0.195, 0.22), skin))
    for k in range(6):
        width = 0.205 * (0.80 + 0.20 * math.sin(math.pi * (k + 0.5) / 6.0))
        flesh.append(ring((-0.02, 0.0, 1.71 + 0.055 * k), width, 0.017, skin, rotation=(0.0, math.radians(12.0), 0.0), scale=(0.64, 1.0, 1.0)))
    flesh.append(limb((-0.04, 0.0, 2.08), (0.0, 0.0, 2.27), 0.035, 0.032, skin))
    body = fuse(flesh, skin)
    # HELLO my name is
    finish("Gary_Body", [body, box((0.097, 0.08, 1.925), (0.012, 0.09, 0.045), tag), box((0.098, 0.08, 1.955), (0.012, 0.09, 0.02), red)])

    skull = [
        ellipsoid((0.01, 0.0, 2.45), (0.12, 0.10, 0.15), skin),
        ellipsoid((0.04, 0.0, 2.385), (0.085, 0.08, 0.09), skin),
        ellipsoid((0.055, 0.0, 2.265), (0.06, 0.062, 0.05), skin),
    ]
    for side in (-1.0, 1.0):
        skull.append(limb((0.0, side * 0.07, 2.37), (0.045, side * 0.052, 2.27), 0.024, 0.02, skin))
    head = [fuse(skull, skin, voxel=0.012), ellipsoid((0.078, 0.0, 2.32), (0.04, 0.042, 0.062), dark)]
    for side in (-1.0, 1.0):
        head.append(sphere((0.10, side * 0.045, 2.47), 0.033, dark, 14, 10))
        head.append(sphere((0.131, side * 0.045, 2.468), 0.007, glint, 8, 6))
    for y in (-0.036, -0.018, 0.0, 0.018, 0.036):
        head.append(spike((0.108 - abs(y) * 0.5, y, 2.345), (0.0, 0.0, -1.0), 0.008, 0.035, tooth))
    finish("Gary_Head", head)

    sx, sy, sz = PIVOTS["Gary_Arm"]
    def at(x, y, z):
        return (sx + x, sy + y, sz + z)
    arm = [fuse([
        limb(at(0.0, 0.0, -0.03), at(0.0, 0.0, -0.52), 0.035, 0.028, skin),
        sphere(at(0.0, 0.0, -0.55), 0.042, skin),
        limb(at(0.0, 0.0, -0.57), at(0.03, 0.0, -1.12), 0.03, 0.022, skin),
        sphere(at(0.035, 0.0, -1.15), 0.03, skin),
        ellipsoid(at(0.04, 0.0, -1.21), (0.022, 0.045, 0.07), skin),
    ], skin, voxel=0.011)]
    # Fingers twice as long as fingers should be.
    arm.append(limb(at(0.045, 0.04, -1.18), at(0.09, 0.10, -1.36), 0.011, 0.004, skin, 8))
    for i, y in enumerate((-0.033, -0.011, 0.011, 0.033)):
        arm.append(limb(at(0.04, y, -1.26), at(0.07 + 0.012 * i, y * 1.9, -1.57 + abs(y) * 1.2), 0.011, 0.004, skin, 8))
    finish("Gary_Arm", arm)


# ------------------------------------------------------------------------------------------
# The Librarian: a black dress, hands that reach her knees, and an open book where the head should be.

def build_librarian():
    dress = material("1B201E")
    cardigan = material("33291F")
    paper = material("D9D2BA")
    skin = material("C4BCAA")
    cover = material("4A1414")
    ink = material("1E1A16")
    lens = material("B4FF96", 30)
    frame = material("0E0E0E")
    ribbon = material("8A1218", 1)

    body = [
        skirt(0.50, 0.12, 1.52, dress),
        fuse([
            ellipsoid((0.0, 0.0, 1.70), (0.12, 0.18, 0.30), cardigan),
            ellipsoid((-0.03, 0.0, 1.90), (0.11, 0.25, 0.10), cardigan),
            sphere((-0.02, -0.24, 1.93), 0.065, cardigan),
            sphere((-0.02, 0.24, 1.93), 0.065, cardigan),
        ], cardigan),
        ring((0.0, 0.0, 1.50), 0.125, 0.02, ink),
        limb((0.0, 0.0, 1.96), (0.0, 0.0, 2.22), 0.032, 0.028, skin),
    ]
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
        head.append(ring((0.09, side * 0.075, 2.58), 0.06, 0.007, frame, rotation=(0.0, math.radians(90.0), 0.0), major_segments=24, minor_segments=6))
        head.append(disc((0.088, side * 0.075, 2.58), 0.056, 0.006, lens, rotation=(0.0, math.radians(90.0), 0.0)))
        head.append(limb((0.09, side * 0.135, 2.58), (-0.01, side * 0.19, 2.58), 0.005, 0.005, frame, 6))
    head.append(limb((0.09, -0.016, 2.585), (0.09, 0.016, 2.585), 0.006, 0.006, frame, 6))
    # The bookmark hangs out of the bottom like a tongue.
    head.append(box((0.03, 0.0, 2.19), (0.006, 0.036, 0.20), ribbon, (0.0, math.radians(-12.0), 0.0)))
    finish("Librarian_Head", head)

    sx, sy, sz = PIVOTS["Librarian_Arm"]
    def at(x, y, z):
        return (sx + x, sy + y, sz + z)
    arm = [
        limb(at(0.0, 0.0, -0.03), at(0.0, 0.0, -0.50), 0.034, 0.026, cardigan),
        limb(at(0.0, 0.0, -0.50), at(0.05, 0.0, -1.02), 0.026, 0.018, skin),
        ellipsoid(at(0.055, 0.0, -1.09), (0.018, 0.035, 0.065), skin),
    ]
    for y in (-0.027, -0.009, 0.009, 0.027):
        arm.append(limb(at(0.055, y, -1.13), at(0.085, y * 1.7, -1.47 + abs(y) * 1.5), 0.008, 0.003, skin, 8))
    finish("Librarian_Arm", arm)

    # The other arm never comes down: one finger on its way to where her lips would be.
    # Unreal flips Y on import, so "towards her middle" is +Y here for the arm the game hangs at +Y.
    inward = 1.0
    shush = [
        limb(at(0.0, 0.0, -0.02), at(0.26, 0.02 * inward, -0.24), 0.034, 0.026, cardigan),
        limb(at(0.26, 0.02 * inward, -0.24), at(0.19, 0.19 * inward, 0.26), 0.026, 0.018, skin),
        ellipsoid(at(0.185, 0.205 * inward, 0.30), (0.03, 0.03, 0.04), skin),
        limb(at(0.18, 0.215 * inward, 0.33), at(0.15, 0.24 * inward, 0.58), 0.009, 0.003, skin, 8),
    ]
    finish("Librarian_Shush", shush)


# ------------------------------------------------------------------------------------------
# The Mouth: a sack of pale skin that is nearly all jaw. Its six legs are built by the game,
# which plants them on the walls; the knobs on the lower jaw are where they attach.

LEG_SOCKETS = ((0.45, 0.74, 0.80), (0.0, 0.86, 0.80), (-0.45, 0.74, 0.80))


def build_mouth():
    skin = material("A88E84")
    gum = material("6E1A22")
    lining = material("2A0306")
    throat = material("050000")
    tooth = material("D8CFA8", 1)
    tongue = material("5E1A26")
    bead = material("030303")
    ember = material("FF2A10", 8)

    rx, ry, rz = 0.95, 0.90, 0.85
    centre = (0.0, 0.0, 1.0)

    def teeth(parts, upper, count, radius, thickness, longest, seed):
        for i in range(count):
            angle = math.radians(-125.0 + 250.0 * i / (count - 1))
            wobble = 0.5 + 0.5 * noise.noise(Vector((i * 1.7, seed, 0.0)))
            length = longest * (0.45 + 0.55 * wobble)
            base = (rx * radius * math.cos(angle), ry * radius * math.sin(angle), 0.985 if upper else 1.015)
            lean = 0.22 * (wobble - 0.5)
            direction = (lean * math.cos(angle), lean * math.sin(angle) + 0.1 * math.sin(i * 2.3), -1.0 if upper else 1.0)
            parts.append(spike(base, direction, thickness, length, tooth))

    bottom = [
        hemisphere(centre, (rx, ry, rz), skin, upper=False, lumps=0.07),
        hemisphere(centre, (rx * 0.95, ry * 0.95, rz * 0.45), lining, upper=False, inside_out=True),
        ring((0.0, 0.0, 0.985), 1.0, 0.05, gum, scale=(rx * 0.97, ry * 0.97, 1.0)),
        ellipsoid((-0.45, 0.0, 0.95), (0.3, 0.4, 0.22), throat),
    ]
    teeth(bottom, False, 23, 0.90, 0.036, 0.30, 5.0)
    teeth(bottom, False, 15, 0.76, 0.028, 0.18, 9.0)
    # A long thin tongue that tastes the air ahead of it.
    path = [(-0.1, 0.74), (0.2, 0.80), (0.5, 0.92), (0.78, 1.06), (1.0, 1.12), (1.2, 1.06), (1.36, 0.94), (1.46, 0.80)]
    for i in range(len(path) - 1):
        (ax, az), (bx, bz) = path[i], path[i + 1]
        bottom.append(limb((ax, 0.0, az), (bx, 0.0, bz), 0.085 - 0.009 * i, 0.076 - 0.009 * i, tongue, 10))
    for side in (-1.0, 1.0):
        for x, y, z in LEG_SOCKETS:
            bottom.append(sphere((x, side * y, z), 0.12, skin, 14, 10))
    for x, y, z, r in ((0.55, 0.62, 0.52, 0.07), (0.30, -0.78, 0.60, 0.06), (0.72, -0.35, 0.40, 0.05), (-0.3, 0.70, 0.45, 0.09), (-0.6, -0.4, 0.5, 0.08)):
        bottom.append(sphere((x, y, z), r, skin, 10, 6))
    finish("Mouth_Bottom", bottom)

    top = [
        hemisphere(centre, (rx, ry, rz), skin, upper=True, lumps=0.07),
        hemisphere(centre, (rx * 0.95, ry * 0.95, rz * 0.45), lining, upper=True, inside_out=True),
        ring((0.0, 0.0, 1.015), 1.0, 0.05, gum, scale=(rx * 0.97, ry * 0.97, 1.0)),
        limb((-0.50, 0.0, 1.30), (-0.45, 0.0, 1.13), 0.04, 0.03, tongue, 8),
        sphere((-0.45, 0.0, 1.10), 0.055, tongue, 10, 6),
    ]
    teeth(top, True, 23, 0.90, 0.036, 0.32, 2.0)
    teeth(top, True, 15, 0.76, 0.028, 0.18, 7.0)
    # No face. Just a scatter of eyes like a spider's, two of them lit.
    eyes = ((18.0, 34.0, 0.075, True), (-16.0, 37.0, 0.07, True), (40.0, 30.0, 0.05, False), (-42.0, 28.0, 0.055, False),
            (4.0, 52.0, 0.045, False), (-28.0, 54.0, 0.04, False), (30.0, 56.0, 0.04, False), (58.0, 42.0, 0.035, False))
    for azimuth, elevation, radius, lit in eyes:
        a = math.radians(azimuth)
        e = math.radians(elevation)
        on_skin = Vector((rx * math.cos(e) * math.cos(a), ry * math.cos(e) * math.sin(a), 1.0 + rz * math.sin(e)))
        top.append(sphere(on_skin, radius, bead, 12, 8))
        if lit:
            out = Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
            top.append(sphere(on_skin + out * radius * 0.82, radius * 0.3, ember, 8, 6))
    for x, y, z, r in ((0.35, 0.55, 1.62, 0.07), (-0.45, -0.50, 1.60, 0.09), (-0.2, 0.3, 1.82, 0.06)):
        top.append(sphere((x, y, z), r, skin, 10, 6))
    finish("Mouth_Top", top)


# ------------------------------------------------------------------------------------------
# The shape sorter from the ward: a wooden box with three holes, and the three shapes.

def prism(centre, radius, height, mat, sides):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=sides, radius1=radius, radius2=radius, depth=height)
    bm.transform(placed(centre))
    return to_object(bm, mat)


def build_toys():
    wood = material("C99A5B")
    hole = material("1E120A")
    red = material("D83A2E")
    blue = material("2F6FD0")
    yellow = material("F2C230")

    # Holes are 9 cm apart along X, in the order circle, square, triangle. HGActors.cpp seats the shapes there.
    top = 0.22
    sorter = [
        box((0.0, 0.0, top * 0.5), (0.34, 0.16, top), wood),
        prism((-0.09, 0.0, top + 0.002), 0.050, 0.006, red, 24),
        prism((-0.09, 0.0, top + 0.004), 0.041, 0.006, hole, 24),
        box((0.0, 0.0, top + 0.002), (0.094, 0.094, 0.006), blue),
        box((0.0, 0.0, top + 0.004), (0.078, 0.078, 0.006), hole),
        prism((0.09, 0.0, top + 0.002), 0.058, 0.006, yellow, 3),
        prism((0.09, 0.0, top + 0.004), 0.047, 0.006, hole, 3),
    ]
    finish("Toy_Sorter", sorter)
    finish("Toy_Circle", [prism((0.0, 0.0, 0.045), 0.038, 0.09, red, 24)])
    finish("Toy_Square", [box((0.0, 0.0, 0.0375), (0.072, 0.072, 0.075), blue)])
    finish("Toy_Triangle", [prism((0.0, 0.0, 0.045), 0.044, 0.09, yellow, 3)])


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
    stand("Mouth_Top", 2.6, rotation=(0.0, math.radians(-38.0), 0.0))

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
    build_toys()
    export(folder)
    if len(arguments) > 1:
        preview(arguments[1])


main()
