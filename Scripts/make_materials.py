"""Creates Content/Materials/M_HG, the one material the game paints everything with.

It has four parameters: Color (base colour), Glow (emissive strength, 0 for none),
Roughness, and Local. The colour is broken up by a noise pattern so nothing is a flat,
clean surface: walls look stained and skin looks blotched. The pattern is fixed in the world
for the building (Local = 0) and fixed to the object for things that move (Local = 1), so it
does not crawl over a monster as it walks.
The building itself (Textured = 1) is covered in scanned surfaces from Content/Textures:
damaged plaster and brick on the walls, worn concrete on floors and ceilings (Floor = 1),
each with its normal and roughness maps, tinted by Color so every place keeps its own mood.
Run it through Scripts/make-assets.sh; it replaces the asset if it exists.
"""
import unreal

FOLDER = "/Game/Materials"
NAME = "M_HG"
PATH = f"{FOLDER}/{NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    unreal.EditorAssetLibrary.delete_asset(PATH)

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
material = tools.create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("used_with_instanced_static_meshes", True)

color = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -520, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))

glow = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -520, 260)
glow.set_editor_property("parameter_name", "Glow")
glow.set_editor_property("default_value", 0.0)

roughness = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -520, 400)
roughness.set_editor_property("parameter_name", "Roughness")
roughness.set_editor_property("default_value", 0.8)

emissive = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -260, 200)
lib.connect_material_expressions(color, "", emissive, "A")
lib.connect_material_expressions(glow, "", emissive, "B")

# Grime: where the noise is sampled, then two sizes of it multiplied into the colour.
world = lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1500, -420)
to_local = lib.create_material_expression(material, unreal.MaterialExpressionTransformPosition, -1300, -300)
to_local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
to_local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
lib.connect_material_expressions(world, "", to_local, "")
local = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1300, -160)
local.set_editor_property("parameter_name", "Local")
local.set_editor_property("default_value", 0.0)
where = lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -1080, -400)
lib.connect_material_expressions(world, "", where, "A")
lib.connect_material_expressions(to_local, "", where, "B")
lib.connect_material_expressions(local, "", where, "Alpha")


def grime(scale, low, x, y):
    node = lib.create_material_expression(material, unreal.MaterialExpressionNoise, x, y)
    node.set_editor_property("scale", scale)
    node.set_editor_property("quality", 1)
    node.set_editor_property("levels", 4)
    node.set_editor_property("turbulence", True)
    node.set_editor_property("output_min", low)
    node.set_editor_property("output_max", 1.0)
    lib.connect_material_expressions(where, "", node, "Position")
    return node


blotches = grime(0.012, 0.5, -860, -480)
speckle = grime(0.11, 0.72, -860, -300)
dirt = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -640, -400)
lib.connect_material_expressions(blotches, "", dirt, "A")
lib.connect_material_expressions(speckle, "", dirt, "B")
stained = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -420, -200)
lib.connect_material_expressions(color, "", stained, "A")
lib.connect_material_expressions(dirt, "", stained, "B")

# Scanned surfaces for the building. Walls use the cube's own UVs once per face; floors repeat twice.
textured = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1300, 700)
textured.set_editor_property("parameter_name", "Textured")
textured.set_editor_property("default_value", 0.0)
is_floor = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1300, 840)
is_floor.set_editor_property("parameter_name", "Floor")
is_floor.set_editor_property("default_value", 0.0)
wall_uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1700, 900)
floor_uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1700, 1300)
floor_uv.set_editor_property("u_tiling", 2.0)
floor_uv.set_editor_property("v_tiling", 2.0)


def sample(name, kind, uv, x, y):
    texture = unreal.load_asset(f"/Game/Textures/{name}")
    if not texture:
        raise RuntimeError(f"HGASSET FAILED /Game/Textures/{name} is missing; run Scripts/make-assets.sh textures first")
    node = lib.create_material_expression(material, unreal.MaterialExpressionTextureSample, x, y)
    node.set_editor_property("texture", texture)
    node.set_editor_property("sampler_type", kind)
    lib.connect_material_expressions(uv, "", node, "UVs")
    return node


def blend(a, a_out, b, b_out, alpha, x, y):
    node = lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    lib.connect_material_expressions(a, a_out, node, "A")
    lib.connect_material_expressions(b, b_out, node, "B")
    lib.connect_material_expressions(alpha, "", node, "Alpha")
    return node


COLOUR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
DATA = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
surface_colour = blend(sample("T_wall_diff", COLOUR, wall_uv, -1400, 900), "RGB", sample("T_floor_diff", COLOUR, floor_uv, -1400, 1300), "RGB", is_floor, -1000, 1000)
surface_normal = blend(sample("T_wall_nor", NORMAL, wall_uv, -1400, 1040), "RGB", sample("T_floor_nor", NORMAL, floor_uv, -1400, 1440), "RGB", is_floor, -1000, 1160)
surface_rough = blend(sample("T_wall_rough", DATA, wall_uv, -1400, 1180), "R", sample("T_floor_rough", DATA, floor_uv, -1400, 1580), "R", is_floor, -1000, 1320)

# The scans average about a third grey, so times three keeps each theme colour about as bright as before.
gain = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -1000, 880)
gain.set_editor_property("r", 3.0)
tint = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -800, 900)
lib.connect_material_expressions(color, "", tint, "A")
lib.connect_material_expressions(gain, "", tint, "B")
tinted = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -640, 960)
lib.connect_material_expressions(surface_colour, "", tinted, "A")
lib.connect_material_expressions(tint, "", tinted, "B")

flat = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -1000, 1500)
flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
final_colour = blend(stained, "", tinted, "", textured, -260, 700)
final_normal = blend(flat, "", surface_normal, "", textured, -260, 1160)
final_rough = blend(roughness, "", surface_rough, "", textured, -260, 1320)

lib.connect_material_property(final_colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
lib.connect_material_property(final_normal, "", unreal.MaterialProperty.MP_NORMAL)
lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.connect_material_property(final_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

lib.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(PATH)
unreal.log(f"HGASSET {PATH} saved={saved}")
if not saved:
    raise RuntimeError(f"HGASSET FAILED {PATH} could not be saved")
