"""Imports the FBX files written by make_models.py into Content/Models as static meshes.

Run it through Scripts/make-assets.sh, which says where the FBX files are in HG_SOURCE.
No materials are imported: every slot is given M_HG, and the game colours each slot from its
name (see HG::AddModel). Any mesh that does not import or save is reported as "HGASSET FAILED"
and the script ends with an error.
"""
import glob
import os

import unreal

SOURCE = os.environ.get("HG_SOURCE", "")
if not SOURCE:
    raise RuntimeError("HGASSET FAILED HG_SOURCE is not set; run this through Scripts/make-assets.sh")
FOLDER = "/Game/Models"
MATERIAL = "/Game/Materials/M_HG"

files = sorted(glob.glob(os.path.join(SOURCE, "SM_*.fbx")))
if not files:
    raise RuntimeError(f"HGASSET FAILED no FBX files in {SOURCE}")
material = unreal.load_asset(MATERIAL)
if not material:
    raise RuntimeError(f"HGASSET FAILED {MATERIAL} is missing; run Scripts/make-assets.sh materials first")

tasks = []
for path in files:
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = False
    options.import_materials = False
    options.import_textures = False
    options.import_animations = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    data = options.static_mesh_import_data
    data.combine_meshes = True
    data.auto_generate_collision = False
    data.generate_lightmap_u_vs = False
    data.build_nanite = False
    data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = FOLDER
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.options = options
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

done = 0
wanted = set()
for path in files:
    name = os.path.splitext(os.path.basename(path))[0]
    asset_path = f"{FOLDER}/{name}"
    wanted.add(asset_path)
    mesh = unreal.load_asset(asset_path)
    if not isinstance(mesh, unreal.StaticMesh):
        unreal.log_error(f"HGASSET FAILED {asset_path} did not import")
        continue
    slots = mesh.get_editor_property("static_materials")
    names = [str(slot.material_slot_name) for slot in slots]
    for index in range(len(slots)):
        mesh.set_material(index, material)
    box = mesh.get_bounding_box()
    size = box.max - box.min
    unreal.log(f"HGASSET {name} size=({size.x:.0f} {size.y:.0f} {size.z:.0f}) min=({box.min.x:.0f} {box.min.y:.0f} {box.min.z:.0f}) slots={','.join(names)}")
    if not all(n.startswith("c_") for n in names):
        unreal.log_error(f"HGASSET FAILED {asset_path} did not import with its colour names")
        continue
    if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
        unreal.log_error(f"HGASSET FAILED {asset_path} could not be saved")
        continue
    done += 1

# The importer may leave other assets behind (materials, mostly). The game does not use them.
for extra in unreal.EditorAssetLibrary.list_assets(FOLDER, recursive=True):
    if extra.split(".")[0] not in wanted:
        unreal.EditorAssetLibrary.delete_asset(extra)
        unreal.log(f"HGASSET removed stray {extra}")
unreal.log(f"HGASSET models imported={done}/{len(files)}")
if done != len(files):
    raise RuntimeError(f"HGASSET FAILED only {done} of {len(files)} models imported")
