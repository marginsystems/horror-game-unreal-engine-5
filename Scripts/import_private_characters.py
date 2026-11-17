"""Imports the rigged human and the animation library into the private assets plugin.

Run through Scripts/make-private-assets.sh. HG_SOURCE is the folder that holds the unpacked
Quaternius packs (Universal Base Characters and Universal Animation Library).
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set")

def find(pattern):
    hits = glob.glob(os.path.join(SOURCE, "**", pattern), recursive=True)
    if not hits:
        raise RuntimeError(f"HGASSET FAILED {pattern} not found under {SOURCE}")
    return hits[0]

jobs = [
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
        if isinstance(asset, unreal.SkeletalMesh):
            extra = f" skeleton={asset.get_editor_property('skeleton').get_path_name()}"
        if isinstance(asset, unreal.AnimSequence):
            extra = f" skeleton={asset.get_editor_property('skeleton').get_path_name() if asset.get_editor_property('skeleton') else None} length={asset.get_play_length():.2f}"
        unreal.log(f"HGASSET {type(asset).__name__} {asset_path}{extra}")
unreal.log("HGASSET characters imported")
