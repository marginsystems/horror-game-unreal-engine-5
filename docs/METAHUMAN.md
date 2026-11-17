# MetaHuman characters through the editor's MCP server

Written 2026-11-17 while making "Mom". State now:

- **Done:** three people are MetaHumans, each a character asset rigged and assembled into a
  Blueprint under `/HorrorPrivate/MetaHumans/<Name>/BP_<Name>`:
  - **Mom** (from the Jelena preset): a sweater, jeans and flats, long straight hair with a
    fringe. She stands by the bed, looks down at the child and glances at the door by turning
    her head and neck; her body turns only when he has gone well round her.
  - **DoctorWoman** and **DoctorSecond** (Grace and Vivian): hospital whites. Both are women;
    "DoctorMan" (Walter) was the second doctor until
    2026-11-18 and is no longer used by the game. At ten they walk up
    the hall and stop in the doorway, looking in at him. In scene three the same two Blueprints
    are spawned a second time, as the people standing in the corridor: every adult in the
    building has one of those two faces. They stay hidden until the corridor begins.
  Clothes and hair are free MetaHuman packages from Fab (see "Clothes and hair from Fab" below).
  The bodies are moved by `UHGPersonAnim` (`Source/HorrorGame/HGPersonAnim.h`): one looping
  clip, played in place, with the neck and head turned on top of it. It can also fold the arms
  up to carry a child (`SetHold`; the arms then keep the idle clip's first frame, so a walk does
  not swing arms that have a child in them), raise the right hand with something in it
  (`SetPresent`) and draw out the neck and head (`SetWrong`).
  The faces are moved by `UHGFaceAnim` (`Source/HorrorGame/HGFaceAnim.h`). A MetaHuman head is
  its own mesh with its own animation step; the stock one (`ABP_Face`) only follows the body
  and leaves every expression control at zero, so everyone looked blank. Ours follows the body
  the same way (the engine's copy-pose node, run by hand) and then sets RigLogic's raw
  controls by name (`CTRL_expressions_mouthCornerPullL` and so on), which the head mesh's
  post-process step `ABP_Face_PostProcess` turns into skin. That gives mom a soft smile, a sad
  one, and blinking, and gives the doctors the look they take on in scene two. `Speak(line)`
  moves the jaw and lips with a voice line, from the mouth tracks `Scripts/make_lipsync.py`
  makes out of the recordings (whisper.cpp word timings plus loudness).
- **Not done:** caps and head mirrors, a real mask (the second doctor's surgical mask in the chair is a plain shape on her head bone), and a real white coat (Fab has no
  free one for MetaHumans; the paid ones are $10 to $21). Walking is one clip slid along the
  floor, so feet slip a little. The carrying arms are two bones turned by hand, not a real
  carrying animation. Beards and eyebrows glitter under the hall lamps (strand hair
  highlights at this resolution).

Helper used below: `Scripts/mcp.py call call_tool '{"toolset_name":..., "tool_name":..., "arguments":{...}}' 20000`.
Toolset names: `E` = `metahuman_toolset.metahuman.MetaHumanToolset` (Epic's),
`H` = `horror_metahuman_toolset.HorrorMetaHumanToolset` (this project's).

## One-time setup (done, verified)

1. **Plugins.** `HorrorGame.uproject` now enables `MetaHumanCharacter` and `MetaHumanGenerator`
   (Epic's experimental MCP toolset). MCP `PluginToolset.SetPluginEnabled` returns success but
   does not write the `.uproject`, so the entries were added to the file by hand. Then quit the
   editor, `Scripts/build.sh` (4 seconds), start the editor again.
2. **MetaHuman Creator Core Data.** An Epic Games Launcher install option of the engine
   (6.18 GB, 1,816 files, lands in
   `UE_5.8/Engine/Plugins/MetaHuman/MetaHumanCharacter/Content/Optional`). Without it there is
   no hair, clothing, preset or skin texture model, and Epic's toolset does not register. The
   launcher installed it while the editor was closed (about 3 minutes). Check with `H.status`.
3. **Project toolset.** `Content/Python/horror_metahuman_toolset.py`, registered by
   `Content/Python/init_unreal.py`. Needed because the stock MCP server cannot run editor Python
   or console commands, and Epic's toolset only does create, body shape, skin tone and eye color.
   After changing the Python file the editor has to be restarted.
4. **The editor app must not be hidden.** While it is hidden (Cmd+H or hidden by another tool)
   `SlateInspectorToolset` returns no windows, `CaptureEditorImage` fails and `CaptureViewport`
   fails with "Viewport has zero size" (`FMacWindow::IsVisible()` is false for a hidden app).
   Asset and object tools still work.

## Steps that worked for Mom, in order

1. Look at the presets. `CaptureAssetImage` refuses MetaHumanCharacter assets, so the face
   textures embedded in `Optional/Presets/*.uasset` were pulled out with a small script (first
   PNG in each file). Fair-skinned women: Jelena, Vivian, Celeste, Aera. Jelena was used.
2. `AssetTools.duplicate {"path":"/MetaHumanCharacter/Optional/Presets/Jelena","new_path":"/HorrorPrivate/MetaHumans/Mom/Mom"}`
3. `E.begin_edit {"object_path":"/HorrorPrivate/MetaHumans/Mom/Mom.Mom"}` returns a session
   (`{"refPath":"/Engine/Transient.MetaHumanEditSession_0"}`) that the next three calls take.
4. `E.set_eye_color {"session":S,"eye_color":{"temperature":0.1,"brightness":0.85}}`
5. `E.set_skin_tone {"session":S,"skin_tone":{"lightness":0.13,"redness":0.5}}`
6. `E.set_body_shape {"session":S,"body_shape":{"masculine_feminine":0.8,"fat":0.38,"muscularity":0.3,"height_cm":166}}`
7. `H.list_slots {"character_path":C}` shows what she wears. The preset came with
   `WI_Hair_S_UpdoBraids`, `WI_Eyebrows_L_Shaded`, `WI_Eyelashes_L_ThickCurl`,
   `WI_Peachfuzz_M_Thin`, `WI_DefaultGarment`.
8. `H.set_wardrobe_item {"character_path":C,"slot_name":"Hair","wardrobe_item_path":"/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_Straight.WI_Hair_L_Straight"}`
9. `H.list_item_parameters {"character_path":C,"slot_name":"Hair"}` then
   `H.set_item_float` Hair `Melanin` 0.27, Hair `Redness` 0.35, Eyebrows `Melanin` 0.4,
   Eyebrows `Redness` 0.25. (Melanin 0.12 gave white hair.)
10. `H.set_item_color` Outfits `PrimaryColorShirt` and `SecondaryColorShirt` 0.16 0.22 0.30,
    `PrimaryColorShort` and `SecondaryColorShort` 0.03 0.03 0.04.
11. Picture: `H.spawn_preview_actor {"character_path":C}` (call it once; a second call adds a
    second actor), `SceneTools.add_to_scene_from_class` a DirectionalLight (Intensity 2 through
    `ObjectTools.set_properties` on `...DirectionalLight_0.LightComponent0`) and a PointLight,
    move the actor away from the PlayerStart marker with `ActorTools.set_actor_transform`,
    then `EditorAppToolset.CaptureViewport` with a full `captureTransform` and `annotations`
    (it rejects `{}`). Two directional lights print a warning into the picture; use one.
12. `E.end_edit {"session":S}` (the preview actor goes away), remove the lights,
    `AssetTools.save_assets {"asset_paths":["/HorrorPrivate/MetaHumans/Mom/Mom"]}`.
    After a restart the saved hair, eyes and colors were still there (checked with a new preview).

For a doctor or nurse: same steps with another preset, other values in 4 to 6, other items in 8.
Hair items are in `/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/` (`WI_Hair_S_*` short,
`_M_*` medium, `_L_*` long), brows and lashes in the sibling folders.

## Animation (verified in the editor, not yet in the game)

- The MetaHuman body skeleton is `metahuman_base_skel` (342 bones, UE5 mannequin bone names).
- Manny animations play on it as they are. No retarget asset, no IK rig and no "compatible
  skeletons" entry was needed. Tested twice through MCP by setting on the body's
  `SkeletalMeshComponent`: `{"AnimationMode":"AnimationSingleNode","AnimationData":{"animToPlay":"/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle","bSavedPlaying":false,"savedPosition":1.0}}`
  - on Epic's template body `/MetaHumanCharacter/Body/IdentityTemplate/SKM_Body`, idle and
    `MF_Unarmed_Walk_Fwd` at 0.3 s: `Saved/Generated/notes/metahuman-anim-test.png`
    (left reference pose, middle idle, right mid-stride);
  - on Mom's preview actor (`...MetaHumanDefaultEditorPipelineActor_0.Body`), idle: head, hair
    and the cloth outfit followed. The pictures in `mom-metahuman.png` are in that idle pose.
- Three files were copied from the engine's template content to make this possible:
  `Content/Characters/Mannequins/Meshes/SK_Mannequin.uasset`,
  `Content/Characters/Mannequins/Anims/Unarmed/MM_Idle.uasset`,
  `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.uasset`.
  They must stay under `/Game/Characters/Mannequins/` because they refer to each other by path.
- C++: `Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);`
  `Body->PlayAnimation(LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")), true);`
  walk: `/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd`.
  `Body` is the body component of the assembled Blueprint; the face runs its own `ABP_Face`.
- Not checked: the assembled Blueprint, Play-In-Editor, a packaged build.
- Epic also ships MetaHuman locomotion clips in the Core Data
  (`/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Stand_Idle_Loop`,
  `AS_MH_Neutral_Walk_Loop_F`, runs, starts, stops). Not tried.
- `/MetaHumanCharacter/Animation/Retargeting/RTG_MH_IKRig` is MetaHuman to MetaHuman only.

## What failed and why

- **Cloud textures: not signed in.** `H.download_textures` returned false. Log:
  `LogEOSSDK: Warning: LogEOSAuth: No existing persistent auth credentials were found for automatic login.`
  `LogMetaHumanAuth: Display: Login failed - error code: EOS_InvalidAuth`
  `LogMetaHumanCharacterEditor: Error: User not logged in, please autorig before downloading source face textures`
  Nothing appeared on screen. `H.can_assemble` is false.
- **Epic's Python examples are out of date for 5.8.** They add wardrobe items to
  `character.internal_collection`; reading parameters then fails with
  `GetCurrentInstanceParametersForItem failed ... because the Collection is not built`, and
  `try_add_slot_selection` adds a second Hair selection instead of replacing the first. While a
  character is open for editing, use `subsystem.get_preview_collection(character)`,
  `set_single_slot_selection`, then `on_edit_preview_collection(character)`. The project
  toolset does this.
- **Editor stuck on quit.** One AppleScript quit left the editor on a modal window (640x526,
  could not be read; most likely "Save Content"). A modal window stops the MCP server, so it
  could not be dismissed through MCP, and SIGTERM was ignored. The process was killed with
  SIGKILL after checking that `Mom.uasset` was already saved. Before quitting, run
  `AssetTools.is_dirty` on the character and save it.
- **Second editor by accident.** The restart script went on to `open -n` while the first editor
  was stuck, which started a second editor; it was closed 60 seconds later with SIGTERM. Check
  `pgrep -x UnrealEditor` is empty before `open`.
- **Port 8000 not bound.** Starting the editor right after the previous one exited once gave
  `LogHttpListener: Error: HttpListener unable to bind to 127.0.0.1:8000` and no MCP server.
  Wait about ten seconds after exit, then check the log line `Created new HttpListener on 127.0.0.1:8000`.
- `PluginToolset.SetPluginEnabled`: no effect on disk.
- `CaptureAssetImage` on a MetaHumanCharacter: "Asset type does not support image capture".
- `CaptureViewport {}`: "input param captureTransform needs a default value".
- `AssetTools.find_assets` wants `asset_type` as `{"refPath":"/Script/Engine.Skeleton"}`.
- **Clothing.** The Core Data has one outfit, `WI_DefaultGarment`: a T-shirt and shorts, no
  shoes. Its only style parameters are the four colors. A sweater, skirt, trousers or shoes
  need another MetaHuman outfit asset or a skeletal mesh in the clothing slot.
- **Hair length.** `WI_Hair_L_Straight` reaches the shoulders, not clearly past them. The other
  long straight one is `WI_Hair_L_StraightBangs` (not tried).

## Sign-in, rig, textures, assembly (run 2026-11-17, worked)

`C` = `/HorrorPrivate/MetaHumans/Mom/Mom.Mom`.

1. `E.begin_edit {"object_path":C}`, then
   `H.auto_rig {"character_path":C,"with_blend_shapes":false,"blocking":false}`. With nobody
   signed in, the editor opens an Epic page in the default browser (a device code, valid ten
   minutes) and a person has to confirm it there. The page can end on "Invalid Client: Redirect
   URL is not known to the client" and the sign-in has still worked; the log says
   `LogEOSAuth: ... Login complete: EOS_Success`. The token is kept, so later runs do not ask.
   If the code expires (`EOS_Auth_PinGrantExpired`), call `auto_rig` again.
2. After the sign-in the rig starts by itself: `Auto-Rigging finished in 24 seconds`.
3. `H.download_textures {"character_path":C}` returned true in under a minute.
4. `H.can_assemble` true, then
   `H.assemble {"character_path":C,"build_path":"/HorrorPrivate/MetaHumans","common_path":"/HorrorPrivate/MetaHumans/Common","quality":"High"}`.
   A plugin folder is accepted. The call comes back as an error listing "Cannot break link" in
   two face Control Rig graphs, yet the log says `MetaHuman Character assembly succeeded` and
   the assets are there. About 50 seconds.
5. Nothing is on disk yet: `AssetTools.find_assets {"folder_path":"/HorrorPrivate/MetaHumans","name":""}`
   (182 assets), then `AssetTools.save_assets` in batches. `E.end_edit`, save the character again.
   The character asset grows from 9 MB to 121 MB (textures and rig inside it).
6. The editor then asks for three renderer settings for the face mesh; they are in
   `Config/DefaultEngine.ini` now (`r.GPUSkin.Support16BitBoneIndex`,
   `r.GPUSkin.UnlimitedBoneInfluences`, `r.SkinCache.CompileShaders`).
7. In the game: `LoadClass<AActor>(nullptr, TEXT("/HorrorPrivate/MetaHumans/Mom/BP_Mom.BP_Mom_C"))`,
   spawn it, find the skeletal mesh component named `Body`, `PlayAnimation`. The body faces
   along the actor's +Y, so take 90 degrees off the yaw to face a point.
   See `AHGGameMode::PopulateWard` and `TickMom`.

## Clothes and hair from Fab, with no editor window (run 2026-11-17, worked)

Fab sells and gives away MetaHuman packages (`.mhpkg`): outfits that resize to any MetaHuman
body, and grooms. Epic's own free set (Sweater, Jeans, Flats, T Shirt Variants and more, "MHC
Web App to Parametric Clothing Set") is listed for UE 5.7 and works in 5.8. Everything below
runs in the Python commandlet, so no editor opens.

1. Download the `.mhpkg` files into one folder. They are Fab Standard License: the imported
   assets go in the private plugin only.
2. Import them. A `.mhpkg` is a zip of `.uasset` files with a manifest, and the engine has a
   factory for it, so an ordinary `AssetImportTask` works:

   ```bash
   bash -c 'source Scripts/env.sh; HG_SOURCE=<folder> "$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" -run=pythonscript -script="$PROJECT_DIR/Scripts/import_metahuman_packages.py" -unattended -nosplash -nullrhi -abslog=<log>'
   ```

   Each package lands in `/HorrorPrivate/MetaHumans/Wardrobe/<name>`; the log lines starting
   `HGASSET` name the wardrobe item (`WI_...`) of each outfit.
3. Dress and assemble. `Scripts/dress_people.py` makes the character from a preset if it is
   new, rigs it and fetches its textures from Epic's cloud, puts the items on, sets their
   colours, and with `HG_BUILD=1` saves and assembles `BP_<Name>`. `HG_WHO` says who (`mom`,
   `doctor_woman`, `doctor_man`, separated by commas; default `mom`). Without `HG_BUILD` it
   only logs each item's adjustable parameters and changes nothing on disk.

   Someone who has been assembled before, in the commandlet:

   ```bash
   bash -c 'source Scripts/env.sh; HG_BUILD=1 HG_WHO=mom "$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" -run=pythonscript -script="$PROJECT_DIR/Scripts/dress_people.py" -unattended -nosplash -AllowCommandletRendering -RenderOffScreen -abslog=<log>'
   ```

   A new person, in the full editor with no window (about five minutes each; `HG_QUIT=1`
   closes the editor when the script ends):

   ```bash
   bash -c 'source Scripts/env.sh; HG_BUILD=1 HG_QUIT=1 HG_WHO=doctor_woman "$UE_EDITOR" "$PROJECT" -ExecutePythonScript="$PROJECT_DIR/Scripts/dress_people.py" -RenderOffScreen -unattended -nosplash -nosound -abslog=<log>'
   ```

   One person per run, and check free disk and swap first: each editor run took about 6 GB of
   swap on this Mac.

What had to be found out:

- **Several outfits at once.** The `Outfits` slot allows multiple selections, though the
  editor's own UI does not show it: `set_single_slot_selection` for the first item, then
  `try_add_slot_selection` for each further one. Epic's default garment is one item (T-shirt
  and shorts), so it is replaced, not added to.
- **A groom with no wardrobe item.** Third-party grooms come as a groom binding. Duplicate
  Epic's `WI_Hair_L_Straight`, which carries the groom pipeline, and set its `principal_asset`
  to the new binding (`unreal.EditorOnlyAssetReference()` with its `asset` property set).
- **A groom with one level of detail is dropped.** Assembly at High quality starts the face at
  LOD 1 and skips any groom not visible there, with no message: she came out bald. Giving the
  groom more levels (`hair_groups_lod`, set with `import_text`) fixes it.
- **Assembling twice mixes up materials.** A second assembly over the first one's output in
  `Mom/Clothing` paired the sweater with the shoes' material. The script deletes that folder's
  assets (all generated) before each assembly.
- **A changed groom does not reach the game.** Assembly keeps the copy of a groom already in
  `<Name>/Grooms`, so new strand settings on the source groom did nothing until the script
  deleted that copy (and its binding) before assembling.
- **Adding an item twice.** `try_add_item_from_wardrobe_item` adds a second copy named
  `..._None_1` when the item is already in the character's palette; look it up first with
  `get_item_keys_for_wardrobe_item`.
- **In a commandlet the asset registry has not scanned the plugin.** `does_asset_exist` says
  no for an asset that is on disk; `load_asset` finds it.
- **A new character's first assembly cannot run in the commandlet.** Assembly bakes the face
  textures (`<Name>/Face/Baked/T_EyeIris...`) with TextureGraph, which is not started in a
  commandlet: `Couldn't find baked texture ... This should have been produced by the texture
  graph`, `Collection build failed`, and then the process dies on a Slate assertion. Mom could
  be reassembled headless only because her baked face textures already existed from her first
  assembly in the editor. The cloud rig and texture download do work headless (the sign-in took
  two and a half minutes to come through, the rig 166 seconds). So a new person is done in
  the full editor instead, started with `-ExecutePythonScript` and `-RenderOffScreen`: no
  window opens, TextureGraph runs, and the same script does everything (rig 19 to 27 seconds
  there). After that first assembly the commandlet can reassemble them.
- **Colours that are not wardrobe parameters.** The Fab shirt exposes none; its colours are in
  its material instance (a `UseCustomColors` switch and five colour parameters), so the script
  edits that asset. Every character wearing the shirt therefore gets the same colours.
- **"Failed to generated pre-baked groom textures".** Logged on every commandlet assembly
  (`TextureGraph engine is not initialised`). It bakes brows into the far face textures; the
  assembly still succeeds and nothing visible in the ward is missing.
