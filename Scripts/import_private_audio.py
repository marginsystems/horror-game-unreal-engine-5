"""Imports voice lines and other licensed audio into the private assets plugin.

HG_SOURCE is a folder of WAV files. V_*.wav are voice lines; they go to /HorrorPrivate/Audio
under the same names, which is where HG::Voice looks for them.
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set")
files = sorted(glob.glob(os.path.join(SOURCE, "V_*.wav")))
if not files:
    raise RuntimeError(f"HGASSET FAILED no V_*.wav files in {SOURCE}")

tasks = []
for path in files:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/HorrorPrivate/Audio"
    task.automated = True
    task.replace_existing = True
    task.save = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
done = sum(1 for path in files if unreal.EditorAssetLibrary.does_asset_exist("/HorrorPrivate/Audio/" + os.path.splitext(os.path.basename(path))[0]))
unreal.log(f"HGASSET voice lines imported={done}/{len(files)}")
if done != len(files):
    raise RuntimeError("HGASSET FAILED some voice lines did not import")
