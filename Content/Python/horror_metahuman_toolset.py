"""MCP tools for the MetaHuman steps that Epic's own MetaHumanGenerator toolset does not cover.

Epic's toolset (plugin MetaHumanGenerator, registered only when the MetaHuman Creator Core Data
is installed) creates a character and sets body shape, skin tone and eye color. This one adds
hair, clothing, the cloud steps (textures, auto-rig), a preview actor and assembly, so a whole
character can be made through the editor's MCP server without the MetaHuman Creator window.

While a character is open for editing, the wardrobe lives in a separate "preview collection";
edits go there and are copied back to the asset with on_edit_preview_collection. Epic's example
scripts edit the asset's own collection instead, which in 5.8 fails with "the Collection is not
built".

See docs/METAHUMAN.md for which of these have been run for real.
"""

from __future__ import annotations

import json
import os

import unreal
import toolset_registry

_SUBSYSTEM = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)

_OPTIONAL_DIR = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_plugins_dir()),
    "MetaHuman", "MetaHumanCharacter", "Content", "Optional")


def _load(character_path: str) -> unreal.MetaHumanCharacter:
    character = unreal.load_asset(character_path)
    if not isinstance(character, unreal.MetaHumanCharacter):
        raise ValueError(f"{character_path} is not a MetaHumanCharacter asset")
    return character


class _Editing:
    """Opens the character for editing for one call, unless something else already has it open."""

    def __init__(self, character: unreal.MetaHumanCharacter):
        self.character = character
        self.opened_here = False

    def __enter__(self):
        if not _SUBSYSTEM.is_object_added_for_editing(self.character):
            if not _SUBSYSTEM.try_add_object_to_edit(self.character):
                raise RuntimeError("Could not open the character for editing")
            self.opened_here = True
        return self.character

    def __exit__(self, exc_type, exc_value, traceback):
        if self.opened_here and _SUBSYSTEM.is_object_added_for_editing(self.character):
            _SUBSYSTEM.remove_object_to_edit(self.character)
        return False


def _preview_instance(character: unreal.MetaHumanCharacter):
    collection = _SUBSYSTEM.get_preview_collection(character)
    if not collection:
        raise RuntimeError("The character has no preview collection; it is not open for editing")
    return collection, collection.default_instance


def _selections(instance) -> list:
    return [entry.selection for entry in instance.get_slot_selection_data()]


def _selected_item(instance, slot_name: str):
    for selection in _selections(instance):
        if str(selection.slot_name) == slot_name:
            return selection.selected_item
    raise ValueError(f"Nothing is selected in slot {slot_name}")


def _parameters(character: unreal.MetaHumanCharacter, slot_name: str) -> list:
    """Builds the preview and returns the parameters of the item selected in a slot."""
    _SUBSYSTEM.assemble_for_preview(character=character)
    _, instance = _preview_instance(character)
    item_path = unreal.MetaHumanPaletteItemPath(item_key=_selected_item(instance, slot_name))
    return list(instance.get_instance_parameters(item_path=item_path))


def _parameter(character: unreal.MetaHumanCharacter, slot_name: str, parameter_name: str):
    parameters = _parameters(character, slot_name)
    for parameter in parameters:
        if str(parameter.name) == parameter_name:
            return parameter
    names = sorted(str(parameter.name) for parameter in parameters)
    raise ValueError(f"No parameter {parameter_name} in slot {slot_name}. It has: {names}")


@unreal.uclass()
class HorrorMetaHumanToolset(unreal.ToolsetDefinition):
    """Hair, clothing, cloud textures, auto-rig, preview and assembly for MetaHuman characters."""

    @toolset_registry.tool_call
    def status() -> str:
        """Reports whether MetaHuman characters can be made in this editor.

        Returns:
            JSON with core_data_installed (the MetaHuman Creator Core Data, an Epic Games
            Launcher install option; without it there is no hair, clothing or skin texture
            model) and the folder that was checked.
        """
        installed = all(
            os.path.isdir(os.path.join(_OPTIONAL_DIR, name)) for name in ("TextureSynthesis", "BodyTextures", "Grooms"))
        return json.dumps({"core_data_installed": installed, "checked_folder": _OPTIONAL_DIR})

    @toolset_registry.tool_call
    def list_slots(character_path: str) -> str:
        """Lists what the character wears: every wardrobe slot that has an item selected.

        Args:
            character_path: MetaHumanCharacter asset, e.g. /HorrorPrivate/MetaHumans/Mom/Mom.Mom

        Returns:
            JSON list of {slot, item}.
        """
        character = _load(character_path)
        with _Editing(character):
            _, instance = _preview_instance(character)
            return json.dumps([
                {"slot": str(selection.slot_name), "item": selection.selected_item.to_asset_name_string()}
                for selection in _selections(instance)])

    @toolset_registry.tool_call
    def set_wardrobe_item(character_path: str, slot_name: str, wardrobe_item_path: str) -> str:
        """Puts a wardrobe item (hair, eyebrows, outfit) in a slot, replacing what was there.

        Args:
            character_path: MetaHumanCharacter asset
            slot_name: Hair, Eyebrows, Eyelashes, Beard, Mustache, Peachfuzz or Outfits
            wardrobe_item_path: e.g. /MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_Straight.WI_Hair_L_Straight

        Returns:
            The name of the item that is now selected in the slot.
        """
        character = _load(character_path)
        wardrobe_item = unreal.load_asset(wardrobe_item_path)
        if not wardrobe_item:
            raise ValueError(f"No wardrobe item at {wardrobe_item_path}")
        with _Editing(character):
            collection, instance = _preview_instance(character)
            item_key = collection.try_add_item_from_wardrobe_item(slot_name, wardrobe_item)
            instance.set_single_slot_selection(slot_name=slot_name, item_key=item_key)
            _SUBSYSTEM.on_edit_preview_collection(character)
            _SUBSYSTEM.assemble_for_preview(character=character)
            return _selected_item(instance, slot_name).to_asset_name_string()

    @toolset_registry.tool_call
    def clear_slot(character_path: str, slot_name: str) -> None:
        """Empties a wardrobe slot, e.g. Outfits to take the clothing off.

        Args:
            character_path: MetaHumanCharacter asset
            slot_name: The slot to empty
        """
        character = _load(character_path)
        with _Editing(character):
            _, instance = _preview_instance(character)
            instance.set_single_slot_selection(slot_name=slot_name, item_key=unreal.MetaHumanPaletteItemKey())
            _SUBSYSTEM.on_edit_preview_collection(character)
            _SUBSYSTEM.assemble_for_preview(character=character)

    @toolset_registry.tool_call
    def list_item_parameters(character_path: str, slot_name: str) -> str:
        """Lists the adjustable parameters (colors, melanin and so on) of the item in a slot.

        Args:
            character_path: MetaHumanCharacter asset
            slot_name: The slot whose selected item to inspect

        Returns:
            JSON list of {name, type}.
        """
        character = _load(character_path)
        with _Editing(character):
            return json.dumps([
                {"name": str(parameter.name), "type": str(parameter.type).split(".")[-1]}
                for parameter in _parameters(character, slot_name)])

    @toolset_registry.tool_call
    def set_item_float(character_path: str, slot_name: str, parameter_name: str, value: float) -> None:
        """Sets a number parameter on the item in a slot, e.g. Hair Melanin.

        Args:
            character_path: MetaHumanCharacter asset
            slot_name: The slot whose selected item to change
            parameter_name: A name from list_item_parameters
            value: The new value
        """
        character = _load(character_path)
        with _Editing(character):
            if not _parameter(character, slot_name, parameter_name).set_float(value=value):
                raise RuntimeError(f"{parameter_name} did not accept a number")
            _SUBSYSTEM.on_edit_preview_collection(character)

    @toolset_registry.tool_call
    def set_item_bool(character_path: str, slot_name: str, parameter_name: str, value: bool) -> None:
        """Sets an on/off parameter on the item in a slot.

        Args:
            character_path: MetaHumanCharacter asset
            slot_name: The slot whose selected item to change
            parameter_name: A name from list_item_parameters
            value: The new value
        """
        character = _load(character_path)
        with _Editing(character):
            if not _parameter(character, slot_name, parameter_name).set_bool(value=value):
                raise RuntimeError(f"{parameter_name} did not accept a bool")
            _SUBSYSTEM.on_edit_preview_collection(character)

    @toolset_registry.tool_call
    def set_item_color(character_path: str, slot_name: str, parameter_name: str, red: float, green: float, blue: float) -> None:
        """Sets a color parameter on the item in a slot.

        Args:
            character_path: MetaHumanCharacter asset
            slot_name: The slot whose selected item to change
            parameter_name: A name from list_item_parameters
            red: Linear red, 0 to 1
            green: Linear green, 0 to 1
            blue: Linear blue, 0 to 1
        """
        character = _load(character_path)
        with _Editing(character):
            color = unreal.LinearColor(red, green, blue, 1.0)
            if not _parameter(character, slot_name, parameter_name).set_color(value=color):
                raise RuntimeError(f"{parameter_name} did not accept a color")
            _SUBSYSTEM.on_edit_preview_collection(character)

    @toolset_registry.tool_call
    def spawn_preview_actor(character_path: str) -> unreal.Actor:
        """Puts a live preview of the character in the level, for pictures before assembly.

        The character must already be open for editing (Epic's begin_edit), and the actor goes
        away when editing ends.

        Args:
            character_path: MetaHumanCharacter asset

        Returns:
            The preview actor.
        """
        character = _load(character_path)
        if not _SUBSYSTEM.is_object_added_for_editing(character):
            raise RuntimeError("Open the character with begin_edit first")
        _SUBSYSTEM.assemble_for_preview(character=character)
        return _SUBSYSTEM.spawn_meta_human_actor(character=character)

    @toolset_registry.tool_call
    def download_textures(character_path: str) -> bool:
        """Downloads the high resolution skin textures from Epic's cloud. Needs an Epic sign-in in the editor.

        Args:
            character_path: MetaHumanCharacter asset

        Returns:
            True when the character now has high resolution textures.
        """
        character = _load(character_path)
        with _Editing(character):
            request = unreal.MetaHumanCharacterTextureRequestParams()
            request.blocking = True
            request.report_progress = False
            _SUBSYSTEM.request_texture_sources(character, request)
        return bool(character.has_high_resolution_textures)

    @toolset_registry.tool_call
    def auto_rig(character_path: str, with_blend_shapes: bool, blocking: bool) -> None:
        """Rigs the face through Epic's cloud. This is also the call that starts the Epic sign-in.

        When nobody is signed in, the editor opens the Epic account page in the browser and
        waits for a person to sign in. With blocking false the editor stays usable while it
        waits, but the character must already be open for editing (Epic's begin_edit) and must
        stay open until can_assemble turns true.

        Args:
            character_path: MetaHumanCharacter asset
            with_blend_shapes: False for joints only (smaller, enough for a game)
            blocking: True to wait for the result inside this call
        """
        character = _load(character_path)
        if not blocking and not _SUBSYSTEM.is_object_added_for_editing(character):
            raise RuntimeError("Open the character with begin_edit first when blocking is false")
        with _Editing(character):
            request = unreal.MetaHumanCharacterAutoRiggingRequestParams()
            request.blocking = blocking
            request.report_progress = False
            request.rig_type = (
                unreal.MetaHumanRigType.JOINTS_AND_BLENDSHAPES if with_blend_shapes else unreal.MetaHumanRigType.JOINTS_ONLY)
            _SUBSYSTEM.request_auto_rigging(character, request)

    @toolset_registry.tool_call
    def can_assemble(character_path: str) -> bool:
        """Tells whether assemble would work now. It needs auto_rig and download_textures first.

        Args:
            character_path: MetaHumanCharacter asset

        Returns:
            True when the character is rigged and has its textures.
        """
        character = _load(character_path)
        with _Editing(character):
            return bool(_SUBSYSTEM.can_build_meta_human(character=character))

    @toolset_registry.tool_call
    def assemble(character_path: str, build_path: str, common_path: str, quality: str) -> None:
        """Assembles the character into game assets (meshes, materials, grooms, Blueprint).

        Args:
            character_path: MetaHumanCharacter asset
            build_path: Folder for this character's assets, e.g. /HorrorPrivate/MetaHumans
            common_path: Folder for assets shared by all MetaHumans, e.g. /HorrorPrivate/MetaHumans/Common
            quality: Cinematic, High, Medium or Low. Cinematic uses the full pipeline, the others the optimized one.
        """
        character = _load(character_path)
        with _Editing(character):
            if not _SUBSYSTEM.can_build_meta_human(character=character):
                raise RuntimeError("Not ready: run auto_rig and download_textures first")
            parameters = unreal.MetaHumanCharacterEditorBuildParameters()
            if quality.lower() == "cinematic":
                parameters.pipeline_type = unreal.MetaHumanDefaultPipelineType.CINEMATIC
                parameters.pipeline_quality = unreal.MetaHumanQualityLevel.CINEMATIC
            else:
                parameters.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
                parameters.pipeline_quality = getattr(unreal.MetaHumanQualityLevel, quality.upper())
            parameters.absolute_build_path = build_path
            parameters.common_folder_path = common_path
            _SUBSYSTEM.build_meta_human(character=character, params=parameters)
