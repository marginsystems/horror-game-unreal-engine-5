"""Imports third-party people, creatures and props into the private assets plugin.

HG_SOURCE lists the folders that hold the unpacked packs, separated by colons. Each job below
names one file to find under them and the plugin folder it is imported into. Every asset that
ends up in the plugin is logged, with sizes for meshes, so the game code can be pointed at it.
"""
import glob
import os

import unreal

SOURCES = [p for p in os.environ.get("HG_SOURCE", "").split(":") if p]
if not SOURCES:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set (one or more folders, separated by colons)")

def find(pattern):
    for source in SOURCES:
        hits = glob.glob(os.path.join(source, "**", pattern), recursive=True)
        if hits:
            return hits[0]
    raise RuntimeError(f"HGASSET FAILED {pattern} not found under {SOURCES}")

jobs = [
    # Scene one, the ward. The mother: a clothed woman on the same skeleton as the animation library,
    # and long hair for her. The bed and the toys are period pieces from Fab.
    (find("Female_Peasant.gltf"), "/HorrorPrivate/Characters/Mom"),
    (find("Superhero_Female_FullBody.gltf"), "/HorrorPrivate/Characters/Woman"),
    (find("T_Superhero_Female_Light_BaseColor.png"), "/HorrorPrivate/Characters/Woman/Skins"),
    # A realistic woman from Fab, to compare against the stylised one.
    (find("army_girl.fbx"), "/HorrorPrivate/Characters/MomReal"),
    (find("Hair_Long.gltf"), "/HorrorPrivate/Characters/MomHair"),
    (find("old_hospital_bed_pbrgr.glb"), "/HorrorPrivate/Ward/Bed"),
    (find("ChildrensToys.fbx"), "/HorrorPrivate/Ward/Toys"),
    (find(os.path.join("2k", "T_Toys_BaseColor.tga")), "/HorrorPrivate/Ward/Toys/Textures"),
    (find(os.path.join("2k", "T_Toys_Normal.tga")), "/HorrorPrivate/Ward/Toys/Textures"),
    (find(os.path.join("2k", "T_Toys_ORM.tga")), "/HorrorPrivate/Ward/Toys/Textures"),
    # The Hollow Walker (Fab, CC-BY): one file with the creature, its skeleton and five clips.
    (find("HollowWalker.glb"), "/HorrorPrivate/Monsters/HollowWalker"),
    (find("Superhero_Male_FullBody.gltf"), "/HorrorPrivate/Characters/Human"),
    # The pale skin, which the glTF does not reference. The game swaps it in for the monster.
    (find("T_Superhero_Male_Ligh.png"), "/HorrorPrivate/Characters/Human/Skins"),
    (find("UAL1_Standard.glb"), "/HorrorPrivate/Characters/UAL1"),
]
for path, folder in jobs:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for asset_path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True):
        asset = unreal.load_asset(asset_path)
        extra = ""
        if isinstance(asset, unreal.StaticMesh):
            box = asset.get_bounding_box()
            extra = f" min=({box.min.x:.0f} {box.min.y:.0f} {box.min.z:.0f}) max=({box.max.x:.0f} {box.max.y:.0f} {box.max.z:.0f}) slots={len(asset.get_editor_property('static_materials'))}"
        if isinstance(asset, unreal.SkeletalMesh):
            size = asset.get_bounds().box_extent
            extra = f" half-size=({size.x:.0f} {size.y:.0f} {size.z:.0f}) skeleton={asset.get_editor_property('skeleton').get_path_name()}"
        if isinstance(asset, unreal.AnimSequence):
            extra = f" skeleton={asset.get_editor_property('skeleton').get_path_name() if asset.get_editor_property('skeleton') else None} length={asset.get_play_length():.2f}"
        unreal.log(f"HGASSET {type(asset).__name__} {asset_path}{extra}")

# The toy pack ships one texture set for all its toys and no material. Make one and put it on every toy.
toys = "/HorrorPrivate/Ward/Toys"
colour = unreal.load_asset(f"{toys}/Textures/T_Toys_BaseColor")
normal = unreal.load_asset(f"{toys}/Textures/T_Toys_Normal")
orm = unreal.load_asset(f"{toys}/Textures/T_Toys_ORM")
if colour and normal and orm:
    normal.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    normal.set_editor_property("srgb", False)
    orm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    orm.set_editor_property("srgb", False)
    for texture in (normal, orm):
        unreal.EditorAssetLibrary.save_asset(texture.get_path_name().split(".")[0], only_if_is_dirty=False)
    lib = unreal.MaterialEditingLibrary
    path = f"{toys}/M_Toys"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Toys", toys, unreal.Material, unreal.MaterialFactoryNew())

    def sample(texture, kind, y):
        node = lib.create_material_expression(material, unreal.MaterialExpressionTextureSample, -500, y)
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", kind)
        return node

    c = sample(colour, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0)
    n = sample(normal, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 260)
    m = sample(orm, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 520)
    lib.connect_material_property(c, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(n, "RGB", unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(m, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    lib.connect_material_property(m, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(m, "B", unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    for asset_path in unreal.EditorAssetLibrary.list_assets(toys, recursive=True):
        mesh = unreal.load_asset(asset_path)
        if isinstance(mesh, unreal.StaticMesh):
            for index in range(len(mesh.get_editor_property("static_materials"))):
                mesh.set_material(index, material)
            unreal.EditorAssetLibrary.save_asset(asset_path.split(".")[0], only_if_is_dirty=False)
    unreal.log("HGASSET toys material made and assigned")
else:
    unreal.log_warning("HGASSET toy textures missing; toys left with their imported materials")

unreal.log("HGASSET characters imported")
