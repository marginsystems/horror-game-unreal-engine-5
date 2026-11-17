"""Dresses the mother: sweater, jeans, flats and longer straight hair, then assembles her again.

The clothes and the hair are MetaHuman packages from Fab, imported into the private plugin by
import_metahuman_packages.py. This script puts them on the character asset and, with HG_BUILD=1,
saves it and assembles /HorrorPrivate/MetaHumans/Mom/BP_Mom again. Without HG_BUILD it changes
nothing on disk and only logs what each item's adjustable parameters are.

Run through the Python commandlet (see Scripts/make-assets.sh for the pattern). It needs no
window: opening a character for editing and assembling it both work in a commandlet. It can be
run again: items already on the character are reused, not added a second time.
"""
import os

import unreal

BUILD = os.environ.get("HG_BUILD", "") == "1"
CHARACTER = "/HorrorPrivate/MetaHumans/Mom/Mom.Mom"
WARDROBE = "/HorrorPrivate/MetaHumans/Wardrobe"

# The Outfits slot takes several items at once; the first replaces what she wore before.
OUTFITS = [
    f"{WARDROBE}/Sweater/WI_OA_Sweater",
    f"{WARDROBE}/Jeans/WI_OA_Jeans",
    f"{WARDROBE}/Flats/WI_OA_Flats",
]

# The Fab groom comes as a groom binding with no wardrobe item, so one is made from Epic's
# long straight hair item (which carries the groom pipeline) and pointed at the new binding.
HAIR_GROOM = f"{WARDROBE}/HairStraightBangs/Hair_StraightHairwithAirBangs/StraightHairwithAirBangs"
HAIR_BINDING = f"{WARDROBE}/HairStraightBangs/GB_Hair_StraightHairwithAirBangs"
HAIR_ITEM = f"{WARDROBE}/HairStraightBangs/WI_Hair_StraightBangs"
HAIR_TEMPLATE = "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_Straight"

# The Fab groom has one level of detail. Assembly at High quality starts the face at LOD 1 and
# drops any groom that is not visible there (MetaHumanGroomEditorPipeline.cpp, "we may skip this
# binding"), which left her bald. These add thinner, thicker-stranded levels, strands only,
# because the groom has no hair cards.
HAIR_LODS = (
    "(AutoLODBias=0.0,LODs=((),"
    "(CurveDecimation=0.7,VertexDecimation=0.5,AngularThreshold=2.0,ScreenSize=0.5,ThicknessScale=2.4),"
    "(CurveDecimation=0.4,VertexDecimation=0.4,AngularThreshold=4.0,ScreenSize=0.25,ThicknessScale=3.2),"
    "(CurveDecimation=0.2,VertexDecimation=0.3,AngularThreshold=8.0,ScreenSize=0.125,ThicknessScale=4.5),"
    "(CurveDecimation=0.1,VertexDecimation=0.25,AngularThreshold=16.0,ScreenSize=0.0625,ThicknessScale=6.0,Simulation=Disable)))"
)

# The groom's own material, which ships a pink-to-brown dye. Dark blonde at the roots, lighter tips.
HAIR_MATERIAL = f"{WARDROBE}/HairStraightBangs/Hair_StraightHairwithAirBangs/Materials/MI_Groom_Inst"
HAIR_COLORS = {"Root Colour": (0.32, 0.19, 0.08), "Tip Colour": (0.52, 0.34, 0.15)}

# Linear colors, by wardrobe item. A parameter an item does not have is logged and skipped.
COLORS = {
    # Dusty rose with a darker trim: warm, and nothing like the white coats that come later.
    "WI_OA_Sweater": {"PrimaryColorSweater": (0.58, 0.19, 0.20), "SecondaryColorSweater": (0.42, 0.12, 0.13)},
    # Ordinary mid-blue denim.
    "WI_OA_Jeans": {"PrimaryColorJeans": (0.061, 0.114, 0.262)},
    # Dark brown house shoes.
    "WI_OA_Flats": {"PrimaryColorFlats": (0.03, 0.025, 0.02)},
}


def log(*parts):
    unreal.log("HGASSET " + " ".join(str(part) for part in parts))


def prepare_hair():
    groom = unreal.load_asset(HAIR_GROOM)
    material = unreal.load_asset(HAIR_MATERIAL)
    if not groom or not material:
        raise RuntimeError(f"HGASSET FAILED groom {groom} material {material}")
    groups = list(groom.get_editor_property("hair_groups_lod"))
    for group in groups:
        group.import_text(HAIR_LODS)
    groom.set_editor_property("hair_groups_lod", groups)
    log("HAIR LODS", [len(group.get_editor_property("LODs")) for group in groom.get_editor_property("hair_groups_lod")])
    library = unreal.MaterialEditingLibrary
    for name, color in HAIR_COLORS.items():
        # The return value is false even when the value is taken, so read it back instead.
        library.set_material_instance_vector_parameter_value(
            material, name, unreal.LinearColor(color[0], color[1], color[2], 1.0))
        now = library.get_material_instance_vector_parameter_value(material, name)
        log("HAIR COLOR", name, f"{now.r:.2f} {now.g:.2f} {now.b:.2f}")
    library.update_material_instance(material)
    if BUILD:
        unreal.EditorAssetLibrary.save_loaded_asset(groom, only_if_is_dirty=False)
        unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)


def hair_item():
    # load_asset, not does_asset_exist: the asset registry has not scanned the plugin yet in a commandlet.
    item = unreal.load_asset(HAIR_ITEM)
    if not item:
        item = unreal.EditorAssetLibrary.duplicate_asset(HAIR_TEMPLATE, HAIR_ITEM)
    binding = unreal.load_asset(HAIR_BINDING)
    if not item or not binding:
        raise RuntimeError(f"HGASSET FAILED hair item {item} binding {binding}")
    reference = unreal.EditorOnlyAssetReference()
    reference.set_editor_property("asset", binding)
    item.set_editor_property("principal_asset", reference)
    item.set_editor_property("thumbnail_name", "Straight, long, with bangs")
    if BUILD:
        unreal.EditorAssetLibrary.save_loaded_asset(item, only_if_is_dirty=False)
    return item


def item_key(collection, slot, wardrobe_item):
    """The key of a wardrobe item in the character's palette, adding the item only if it is new."""
    existing = list(collection.get_item_keys_for_wardrobe_item(wardrobe_item))
    return existing[0] if existing else collection.try_add_item_from_wardrobe_item(slot, wardrobe_item)


prepare_hair()

subsystem = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
character = unreal.load_asset(CHARACTER)
if not subsystem.try_add_object_to_edit(character):
    raise RuntimeError("HGASSET FAILED could not open the character for editing")

try:
    collection = subsystem.get_preview_collection(character)
    instance = collection.default_instance

    first = True
    for path in OUTFITS:
        item = unreal.load_asset(path)
        if not item:
            raise RuntimeError(f"HGASSET FAILED no wardrobe item at {path}")
        key = item_key(collection, "Outfits", item)
        if first:
            instance.set_single_slot_selection(slot_name="Outfits", item_key=key)
            first = False
        else:
            selection = unreal.MetaHumanPipelineSlotSelection()
            selection.set_editor_property("slot_name", "Outfits")
            selection.set_editor_property("selected_item", key)
            if not instance.try_add_slot_selection(selection):
                raise RuntimeError(f"HGASSET FAILED could not add {path} to Outfits")

    instance.set_single_slot_selection(slot_name="Hair", item_key=item_key(collection, "Hair", hair_item()))

    subsystem.on_edit_preview_collection(character)
    subsystem.assemble_for_preview(character=character)

    for entry in instance.get_slot_selection_data():
        selection = entry.selection
        name = selection.selected_item.to_asset_name_string()
        slot = str(selection.slot_name)
        if slot == "Character":
            continue
        parameters = list(instance.get_instance_parameters(
            item_path=unreal.MetaHumanPaletteItemPath(item_key=selection.selected_item)))
        log("WEARS", slot, name, [str(parameter.name) for parameter in parameters])
        by_name = {str(parameter.name): parameter for parameter in parameters}
        for parameter_name, color in COLORS.get(name, {}).items():
            done = parameter_name in by_name and by_name[parameter_name].set_color(
                value=unreal.LinearColor(color[0], color[1], color[2], 1.0))
            log("COLOR", name, parameter_name, color, "set" if done else "NOT SET")
    subsystem.on_edit_preview_collection(character)

    if BUILD:
        if not subsystem.can_build_meta_human(character=character):
            raise RuntimeError("HGASSET FAILED the character cannot be assembled (rig or textures missing)")
        # Assembly reuses the names in her Clothing folder. Left in place, the old meshes and
        # material instances get paired up wrongly (the sweater once came out wearing the shoes'
        # material), so everything there, all of it generated, goes before each assembly.
        clothing = "/HorrorPrivate/MetaHumans/Mom/Clothing"
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([clothing], True)
        for asset_path in unreal.EditorAssetLibrary.list_assets(clothing, recursive=True):
            unreal.EditorAssetLibrary.delete_asset(asset_path)
        parameters = unreal.MetaHumanCharacterEditorBuildParameters()
        parameters.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
        parameters.pipeline_quality = unreal.MetaHumanQualityLevel.HIGH
        parameters.absolute_build_path = "/HorrorPrivate/MetaHumans"
        parameters.common_folder_path = "/HorrorPrivate/MetaHumans/Common"
        subsystem.build_meta_human(character=character, params=parameters)
        log("ASSEMBLED")
finally:
    subsystem.remove_object_to_edit(character)

if BUILD:
    unreal.EditorAssetLibrary.save_loaded_asset(character, only_if_is_dirty=False)
    unreal.EditorAssetLibrary.save_directory("/HorrorPrivate/MetaHumans", only_if_is_dirty=True, recursive=True)
    log("SAVED")
log("DONE build" if BUILD else "DONE dry run")
