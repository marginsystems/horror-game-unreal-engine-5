"""Imports voice lines and other licensed audio into the private assets plugin.

HG_SOURCE is a folder of WAV files. V_*.wav are voice lines and X_*.wav are licensed sound
effects (Splice). They go to /HorrorPrivate/Audio under the same names, which is where HG::Voice
and HG::Extra look for them. The ones named in LOOPS are marked as looping.
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set")
LOOPS = ("X_ward_room", "X_ward_birds", "X_clock", "X_hall", "X_hall_low", "X_heart", "X_hall_voices", "X_ring", "X_escape_drone")
files = sorted(glob.glob(os.path.join(SOURCE, "V_*.wav")) + glob.glob(os.path.join(SOURCE, "X_*.wav")))
if not files:
    raise RuntimeError(f"HGASSET FAILED no V_*.wav or X_*.wav files in {SOURCE}")

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
done = 0
for path in files:
    name = os.path.splitext(os.path.basename(path))[0]
    sound = unreal.load_asset(f"/HorrorPrivate/Audio/{name}")
    if not isinstance(sound, unreal.SoundWave):
        unreal.log_error(f"HGASSET FAILED {name} did not import")
        continue
    sound.set_editor_property("looping", name in LOOPS)
    if unreal.EditorAssetLibrary.save_asset(f"/HorrorPrivate/Audio/{name}", only_if_is_dirty=False):
        done += 1
unreal.log(f"HGASSET private audio imported={done}/{len(files)}")
if done != len(files):
    raise RuntimeError("HGASSET FAILED some audio did not import")
