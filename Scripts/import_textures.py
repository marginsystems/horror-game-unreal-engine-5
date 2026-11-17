"""Imports the textures fetched by fetch_textures.py into Content/Textures.

Run it through Scripts/make-assets.sh, which says where the files are in HG_SOURCE.
Normal maps and roughness maps are marked as data, not colour, so the material reads them
correctly. Anything that does not import or save is reported as "HGASSET FAILED".
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set; run this through Scripts/make-assets.sh")
FOLDER = "/Game/Textures"

files = sorted(glob.glob(os.path.join(SOURCE, "T_*.jpg")))
if not files:
    raise RuntimeError(f"HGASSET FAILED no textures in {SOURCE}")

tasks = []
for path in files:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = FOLDER
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

done = 0
for path in files:
    name = os.path.splitext(os.path.basename(path))[0]
    asset_path = f"{FOLDER}/{name}"
    texture = unreal.load_asset(asset_path)
    if not isinstance(texture, unreal.Texture2D):
        unreal.log_error(f"HGASSET FAILED {asset_path} did not import")
        continue
    if name.endswith("_nor"):
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("srgb", False)
    elif name.endswith("_rough"):
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("srgb", False)
    else:
        texture.set_editor_property("srgb", True)
    if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
        unreal.log_error(f"HGASSET FAILED {asset_path} could not be saved")
        continue
    done += 1
unreal.log(f"HGASSET textures imported={done}/{len(files)}")
if done != len(files):
    raise RuntimeError(f"HGASSET FAILED only {done} of {len(files)} textures imported")
