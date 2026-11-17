"""Imports the WAV files written by make_sounds.py into Content/Audio as SoundWave assets.

Run it through Scripts/make-assets.sh, which says where the WAV files are in HG_SOURCE.
Sounds whose name starts with a prefix in LOOPS are marked as looping. Any sound that does
not import or save is reported as "HGASSET FAILED" and the script ends with an error.
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set; run this through Scripts/make-assets.sh")
FOLDER = "/Game/Audio"
LOOPS = ("S_amb_", "S_mouth_loop")

files = sorted(glob.glob(os.path.join(SOURCE, "S_*.wav")))
if not files:
    raise RuntimeError(f"HGASSET FAILED no WAV files in {SOURCE}")

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
    sound = unreal.load_asset(asset_path)
    if not isinstance(sound, unreal.SoundWave):
        unreal.log_error(f"HGASSET FAILED {asset_path} did not import")
        continue
    sound.set_editor_property("looping", name.startswith(LOOPS))
    if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
        unreal.log_error(f"HGASSET FAILED {asset_path} could not be saved")
        continue
    done += 1
unreal.log(f"HGASSET sounds imported={done}/{len(files)}")
if done != len(files):
    raise RuntimeError(f"HGASSET FAILED only {done} of {len(files)} sounds imported")
