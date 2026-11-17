"""Creates Content/Materials/M_HG, the one material the game paints everything with.

It has four parameters: Color (base colour), Glow (emissive strength, 0 for none),
Roughness, and Local. The colour is broken up by a noise pattern so nothing is a flat,
clean surface: walls look stained and skin looks blotched. The pattern is fixed in the world
for the building (Local = 0) and fixed to the object for things that move (Local = 1), so it
does not crawl over a monster as it walks.
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

lib.connect_material_property(stained, "", unreal.MaterialProperty.MP_BASE_COLOR)
lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

lib.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(PATH)
unreal.log(f"HGASSET {PATH} saved={saved}")
if not saved:
    raise RuntimeError(f"HGASSET FAILED {PATH} could not be saved")
