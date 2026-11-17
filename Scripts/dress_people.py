"""Makes the MetaHuman people of the game wear what the story says, then assembles them.

HG_WHO names who to do, separated by commas: mom, doctor_woman, doctor_man (default: mom).
For each one the script

- makes the character from one of Epic's presets if it does not exist yet,
- rigs it and fetches its skin textures from Epic's cloud if that was never done (this needs
  the Epic sign-in that the editor keeps; see docs/METAHUMAN.md),
- puts the clothes and hair on (MetaHuman packages from Fab, imported into the private plugin
  by import_metahuman_packages.py) and sets their colours,
- and, with HG_BUILD=1, saves the character and assembles /HorrorPrivate/MetaHumans/<Name>/BP_<Name>.

Without HG_BUILD nothing is written to disk; the run only logs what each item's adjustable
parameters are. It can be run again: items already on a character are reused, and the generated
clothing assets are cleared before each assembly.

Two ways to run it (docs/METAHUMAN.md has the full commands):

- In the Python commandlet with -AllowCommandletRendering -RenderOffScreen. This is enough for a
  person who has been assembled before.
- In the full editor, with -ExecutePythonScript=<this file> and HG_QUIT=1 so the editor closes
  when the script ends. A new person's first assembly needs this: it bakes the face textures
  with TextureGraph, which a commandlet does not start.
"""
import os

import unreal

BUILD = os.environ.get("HG_BUILD", "") == "1"
ROOT = "/HorrorPrivate/MetaHumans"
WARDROBE = f"{ROOT}/Wardrobe"
PRESETS = "/MetaHumanCharacter/Optional/Presets"

SWEATER = f"{WARDROBE}/Sweater/WI_OA_Sweater"
JEANS = f"{WARDROBE}/Jeans/WI_OA_Jeans"
FLATS = f"{WARDROBE}/Flats/WI_OA_Flats"
SHIRT = f"{WARDROBE}/ShirtLongSleeve/WI_OA_Shirt_04"

# Hospital whites: off-white, a touch warm, so they do not blow out under the ward's light.
WHITE = (0.80, 0.79, 0.74)
DARK_SHOE = (0.02, 0.02, 0.02)

# The Outfits slot takes several items at once; the first replaces what the character wore.
# Colours are linear, by wardrobe item; a parameter an item does not have is logged and skipped.
PEOPLE = {
    "mom": {
        "name": "Mom",
        "preset": "Jelena",
        "outfits": [SWEATER, JEANS, FLATS],
        "colors": {
            # Dusty rose with a darker trim: warm, and nothing like the whites that come later.
            "WI_OA_Sweater": {"PrimaryColorSweater": (0.58, 0.19, 0.20), "SecondaryColorSweater": (0.42, 0.12, 0.13)},
            # Ordinary mid-blue denim.
            "WI_OA_Jeans": {"PrimaryColorJeans": (0.061, 0.114, 0.262)},
            # Dark brown house shoes.
            "WI_OA_Flats": {"PrimaryColorFlats": (0.03, 0.025, 0.02)},
        },
        "fab_hair": True,
    },
    # The doctor who holds him on her lap, and the one who does the work. Ordinary people in
    # hospital whites. Fab has no free white coat for MetaHumans, so a long-sleeved collared
    # shirt and trousers, both turned white, stand in for it.
    "doctor_woman": {
        "name": "DoctorWoman",
        "preset": "Grace",
        "outfits": [SHIRT, JEANS, FLATS],
        "colors": {},
    },
    # The owner remembers both doctors as women, so this is the second one. She replaced
    # "DoctorMan" (the Walter preset) on 2026-11-18; nothing in the game uses him any more.
    "doctor_second": {
        "name": "DoctorSecond",
        "preset": "Vivian",
        "outfits": [SHIRT, JEANS, FLATS],
        "colors": {},
    },
    "doctor_man": {
        "name": "DoctorMan",
        "preset": "Walter",
        "outfits": [SHIRT, JEANS, FLATS],
        "colors": {},
    },
}
for person in ("doctor_woman", "doctor_second", "doctor_man"):
    PEOPLE[person]["colors"] = {
        "WI_OA_Jeans": {"PrimaryColorJeans": WHITE, "SecondaryColorJeans": WHITE},
        "WI_OA_Flats": {"PrimaryColorFlats": DARK_SHOE, "SecondaryColorFlats": DARK_SHOE, "TertiaryColorFlats": DARK_SHOE},
    }

# Mom's hair. The Fab groom comes as a groom binding with no wardrobe item, so one is made from
# Epic's long straight hair item (which carries the groom pipeline) and pointed at the binding.
HAIR_GROOM = f"{WARDROBE}/HairStraightBangs/Hair_StraightHairwithAirBangs/StraightHairwithAirBangs"
HAIR_BINDING = f"{WARDROBE}/HairStraightBangs/GB_Hair_StraightHairwithAirBangs"
HAIR_ITEM = f"{WARDROBE}/HairStraightBangs/WI_Hair_StraightBangs"
HAIR_TEMPLATE = "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_Straight"

# The Fab groom has one level of detail. Assembly at High quality starts the face at LOD 1 and
# drops any groom that is not visible there (MetaHumanGroomEditorPipeline.cpp, "we may skip this
# binding"), which left her bald. These add thinner, thicker-stranded levels, strands only,
# because the groom has no hair cards. The first level is what the ward shows. Keeping 0.9 of
# the strands there instead of 0.7 looked barely fuller; what it costs was not measured (the
# ward's frame rate read anywhere from 27 to 54 on identical content on a busy machine).
HAIR_LODS = (
    "(AutoLODBias=0.0,LODs=((),"
    "(CurveDecimation=0.7,VertexDecimation=0.5,AngularThreshold=2.0,ScreenSize=0.5,ThicknessScale=2.4),"
    "(CurveDecimation=0.4,VertexDecimation=0.4,AngularThreshold=4.0,ScreenSize=0.25,ThicknessScale=3.2),"
    "(CurveDecimation=0.2,VertexDecimation=0.3,AngularThreshold=8.0,ScreenSize=0.125,ThicknessScale=4.5),"
    "(CurveDecimation=0.1,VertexDecimation=0.25,AngularThreshold=16.0,ScreenSize=0.0625,ThicknessScale=6.0,Simulation=Disable)))"
)

HAIR_TIP_SCALE = 0.5

# The groom's own material, which ships a pink-to-brown dye. Dark blonde at the roots, lighter tips.
HAIR_MATERIAL = f"{WARDROBE}/HairStraightBangs/Hair_StraightHairwithAirBangs/Materials/MI_Groom_Inst"
HAIR_COLORS = {"Root Colour": (0.32, 0.19, 0.08), "Tip Colour": (0.52, 0.34, 0.15)}

# The shirt's colours are not wardrobe parameters; they live in its one material instance, which
# ships a plaid texture. Its own switch swaps the texture for plain colours.
SHIRT_MATERIAL = f"{WARDROBE}/ShirtLongSleeve/Shirt_04/Materials/MI_Shirt_04"
SHIRT_COLORS = {
    "Color_Left_side": WHITE, "Color_right_side": WHITE, "Color_left_pocket": WHITE,
    "Color_right_pocket": WHITE, "Color_buttons": (0.55, 0.55, 0.52),
}

SUBSYSTEM = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)


def log(*parts):
    unreal.log("HGASSET " + " ".join(str(part) for part in parts))


def fab_hair_item():
    groom = unreal.load_asset(HAIR_GROOM)
    material = unreal.load_asset(HAIR_MATERIAL)
    binding = unreal.load_asset(HAIR_BINDING)
    if not groom or not material or not binding:
        raise RuntimeError(f"HGASSET FAILED groom {groom} material {material} binding {binding}")
    groups = list(groom.get_editor_property("hair_groups_lod"))
    for group in groups:
        group.import_text(HAIR_LODS)
    groom.set_editor_property("hair_groups_lod", groups)
    # Strands as wide at the tip as at the root looked like string where the hair thins out.
    rendering = list(groom.get_editor_property("hair_groups_rendering"))
    for group in rendering:
        geometry = group.get_editor_property("geometry_settings")
        geometry.set_editor_property("hair_tip_scale", HAIR_TIP_SCALE)
        group.set_editor_property("geometry_settings", geometry)
    groom.set_editor_property("hair_groups_rendering", rendering)
    library = unreal.MaterialEditingLibrary
    for name, color in HAIR_COLORS.items():
        # The return value is false even when the value is taken, so read it back instead.
        library.set_material_instance_vector_parameter_value(
            material, name, unreal.LinearColor(color[0], color[1], color[2], 1.0))
        now = library.get_material_instance_vector_parameter_value(material, name)
        log("HAIR COLOR", name, f"{now.r:.2f} {now.g:.2f} {now.b:.2f}")
    library.update_material_instance(material)

    # load_asset, not does_asset_exist: the asset registry has not scanned the plugin yet in a commandlet.
    item = unreal.load_asset(HAIR_ITEM)
    if not item:
        item = unreal.EditorAssetLibrary.duplicate_asset(HAIR_TEMPLATE, HAIR_ITEM)
    if not item:
        raise RuntimeError("HGASSET FAILED could not make the hair wardrobe item")
    reference = unreal.EditorOnlyAssetReference()
    reference.set_editor_property("asset", binding)
    item.set_editor_property("principal_asset", reference)
    item.set_editor_property("thumbnail_name", "Straight, long, with bangs")
    if BUILD:
        for asset in (groom, material, item):
            unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    return item


def whiten_shirt():
    material = unreal.load_asset(SHIRT_MATERIAL)
    if not material:
        raise RuntimeError(f"HGASSET FAILED no shirt material at {SHIRT_MATERIAL}")
    library = unreal.MaterialEditingLibrary
    library.set_material_instance_static_switch_parameter_value(material, "UseCustomColors", True)
    for name, color in SHIRT_COLORS.items():
        library.set_material_instance_vector_parameter_value(
            material, name, unreal.LinearColor(color[0], color[1], color[2], 1.0))
    library.update_material_instance(material)
    log("SHIRT", "custom colours", library.get_material_instance_static_switch_parameter_value(material, "UseCustomColors"))
    if BUILD:
        unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)


def item_key(collection, slot, wardrobe_item):
    """The key of a wardrobe item in the character's palette, adding the item only if it is new."""
    existing = list(collection.get_item_keys_for_wardrobe_item(wardrobe_item))
    return existing[0] if existing else collection.try_add_item_from_wardrobe_item(slot, wardrobe_item)


def character_for(person):
    path = f"{ROOT}/{person['name']}/{person['name']}"
    character = unreal.load_asset(path)
    if not character:
        # Through the loaded preset: in a commandlet the asset registry does not know the
        # engine plugin's content yet, so duplicating by path finds nothing.
        preset = unreal.load_asset(f"{PRESETS}/{person['preset']}")
        if not preset:
            raise RuntimeError(f"HGASSET FAILED no preset called {person['preset']}")
        character = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
            person["name"], f"{ROOT}/{person['name']}", preset)
        log("CREATED", path, "from", person["preset"])
        if BUILD:
            unreal.EditorAssetLibrary.save_loaded_asset(character, only_if_is_dirty=False)
    if not isinstance(character, unreal.MetaHumanCharacter):
        raise RuntimeError(f"HGASSET FAILED {path} is not a MetaHumanCharacter")
    return character


def rig_and_texture(character):
    """The two cloud steps a new character needs once before it can be assembled."""
    if SUBSYSTEM.can_build_meta_human(character=character):
        return
    rig = unreal.MetaHumanCharacterAutoRiggingRequestParams()
    rig.blocking = True
    rig.report_progress = False
    rig.rig_type = unreal.MetaHumanRigType.JOINTS_ONLY
    SUBSYSTEM.request_auto_rigging(character, rig)
    textures = unreal.MetaHumanCharacterTextureRequestParams()
    textures.blocking = True
    textures.report_progress = False
    SUBSYSTEM.request_texture_sources(character, textures)
    log("RIGGED", character.get_name(), "textures", bool(character.has_high_resolution_textures),
        "can assemble", bool(SUBSYSTEM.can_build_meta_human(character=character)))


def dress(person):
    name = person["name"]
    character = character_for(person)
    if not SUBSYSTEM.try_add_object_to_edit(character):
        raise RuntimeError(f"HGASSET FAILED could not open {name} for editing")
    try:
        if BUILD:
            rig_and_texture(character)
        collection = SUBSYSTEM.get_preview_collection(character)
        instance = collection.default_instance

        if SHIRT in person["outfits"]:
            whiten_shirt()
        first = True
        for path in person["outfits"]:
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
        if person.get("fab_hair"):
            instance.set_single_slot_selection(slot_name="Hair", item_key=item_key(collection, "Hair", fab_hair_item()))

        SUBSYSTEM.on_edit_preview_collection(character)
        SUBSYSTEM.assemble_for_preview(character=character)

        for entry in instance.get_slot_selection_data():
            selection = entry.selection
            item_name = selection.selected_item.to_asset_name_string()
            slot = str(selection.slot_name)
            if slot == "Character":
                continue
            parameters = list(instance.get_instance_parameters(
                item_path=unreal.MetaHumanPaletteItemPath(item_key=selection.selected_item)))
            log("WEARS", name, slot, item_name, [str(parameter.name) for parameter in parameters][:6])
            by_name = {str(parameter.name): parameter for parameter in parameters}
            for parameter_name, color in person["colors"].get(item_name, {}).items():
                done = parameter_name in by_name and by_name[parameter_name].set_color(
                    value=unreal.LinearColor(color[0], color[1], color[2], 1.0))
                log("COLOR", name, item_name, parameter_name, color, "set" if done else "NOT SET")
        SUBSYSTEM.on_edit_preview_collection(character)

        if BUILD:
            if not SUBSYSTEM.can_build_meta_human(character=character):
                raise RuntimeError(f"HGASSET FAILED {name} cannot be assembled (rig or textures missing)")
            # Assembly reuses the names in the Clothing folder. Left in place, the old meshes and
            # material instances get paired up wrongly (the sweater once came out wearing the shoes'
            # material), so everything there, all of it generated, goes before each assembly.
            clothing = f"{ROOT}/{name}/Clothing"
            unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([clothing], True)
            for asset_path in unreal.EditorAssetLibrary.list_assets(clothing, recursive=True):
                unreal.EditorAssetLibrary.delete_asset(asset_path)
            # The same goes for the copy of the Fab hair in her Grooms folder: assembly keeps an
            # existing copy, so a change to the groom's strands never reached the game.
            if person.get("fab_hair"):
                for asset_name in ("GB_Hair_StraightHairwithAirBangs", "StraightHairwithAirBangs"):
                    old_copy = f"{ROOT}/{name}/Grooms/{asset_name}"
                    if unreal.load_asset(old_copy):
                        log("CLEARED", old_copy, unreal.EditorAssetLibrary.delete_asset(old_copy))
            parameters = unreal.MetaHumanCharacterEditorBuildParameters()
            parameters.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
            parameters.pipeline_quality = unreal.MetaHumanQualityLevel.HIGH
            parameters.absolute_build_path = ROOT
            parameters.common_folder_path = f"{ROOT}/Common"
            SUBSYSTEM.build_meta_human(character=character, params=parameters)
            log("ASSEMBLED", name)
    finally:
        SUBSYSTEM.remove_object_to_edit(character)
    if BUILD:
        unreal.EditorAssetLibrary.save_loaded_asset(character, only_if_is_dirty=False)


try:
    for who in os.environ.get("HG_WHO", "mom").split(","):
        if who not in PEOPLE:
            raise RuntimeError(f"HGASSET FAILED nobody is called {who}; there is {sorted(PEOPLE)}")
        dress(PEOPLE[who])
    if BUILD:
        unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
        log("SAVED")
    log("DONE build" if BUILD else "DONE dry run")
except Exception as error:
    unreal.log_error(f"HGASSET FAILED {error}")
    raise
finally:
    # In the full editor (see the top of this file) nothing ends the run by itself.
    if os.environ.get("HG_QUIT", "") == "1":
        unreal.SystemLibrary.quit_editor()
