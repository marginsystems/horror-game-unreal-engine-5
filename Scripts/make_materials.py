"""Creates Content/Materials/M_HG, the one material the game paints everything with.

It has three parameters: Color (base colour), Glow (emissive strength, 0 for none)
and Roughness. Run it through Scripts/make-assets.sh; it replaces the asset if it exists.
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

lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

lib.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(PATH)
unreal.log(f"HGASSET {PATH} saved={saved}")
