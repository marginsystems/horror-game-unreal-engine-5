"""Imports MetaHuman packages (.mhpkg from Fab: outfits, shoes, grooms) into the private plugin.

HG_SOURCE is the folder that holds the downloaded .mhpkg files. Each one is imported into its
own folder under /HorrorPrivate/MetaHumans/Wardrobe, and every asset that lands there is logged
so the wardrobe item to put on a character can be read from the output. HG_ONLY, if set, is one
file name (without .mhpkg) to import instead of all of them.

Run through the Python commandlet (see Scripts/make-assets.sh for the pattern).
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set (the folder with the .mhpkg files)")

ROOT = "/HorrorPrivate/MetaHumans/Wardrobe"
# File name (without .mhpkg) to the folder it is imported into.
FOLDERS = {
    "oa_jeans": "Jeans",
    "oa_flats": "Flats",
    "oa_sweater": "Sweater",
    "oa_shirt_04": "ShirtLongSleeve",
    "gb_hair_straighthairwithairbangs": "HairStraightBangs",
}

ONLY = os.environ.get("HG_ONLY", "")
packages = sorted(glob.glob(os.path.join(SOURCE, f"{ONLY or '*'}.mhpkg")))
if not packages:
    raise RuntimeError(f"HGASSET FAILED no .mhpkg files in {SOURCE}")

for path in packages:
    name = os.path.splitext(os.path.basename(path))[0]
    folder = f"{ROOT}/{FOLDERS.get(name, name)}"
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    assets = unreal.EditorAssetLibrary.list_assets(folder, recursive=True)
    if not assets:
        unreal.log_error(f"HGASSET FAILED nothing imported from {path}")
        continue
    unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=False, recursive=True)
    for asset_path in assets:
        data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
        kind = str(data.asset_class_path.asset_name)
        # The wardrobe item, outfit and groom binding are what a character is pointed at.
        if kind in ("MetaHumanWardrobeItem", "ChaosOutfitAsset", "GroomBindingAsset", "GroomAsset"):
            unreal.log(f"HGASSET {name} {kind} {asset_path}")
    unreal.log(f"HGASSET DONE {name} assets={len(assets)}")
