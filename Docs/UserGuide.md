# Model Constructor user guide

Model Constructor 1.0.0, an editor plugin for Unreal Engine 5.4 to 5.8. It imports a Datasmith file into its own content folder and level, prepares it for Nanite, and lets you re-import or remove it later. For an overview, see the [README](../README.md).

Imported models do not depend on this plugin. You can disable or remove it and the models still open and package.

## Setup

1. Copy the `ModelConstructor` folder into your project's `Plugins` folder.
2. Make sure the Datasmith Content and Datasmith Importer plugins can be enabled. Model Constructor enables them for you.
3. Open the project and choose **Yes** when the editor offers to build the missing modules, or build from your IDE. This needs Visual Studio 2022 with the C++ workload, and works in Blueprint-only projects too.
4. Open the tool from the toolbar button **Model Constructor**, or from **Tools > Model Constructor**.

To use it in every project on one engine install instead, install it as an engine plugin. The [README](../README.md#as-an-engine-plugin) has the steps. Keep only one copy: a project copy and an engine copy of the same plugin conflict.

The tool has four pages, left to right:

| Page | What it is for |
|---|---|
| Model Manager | Find a model, see where it came from, open its level, re-import it or remove it |
| Import | Choose a template, a file and a folder, then import |
| Settings | View, duplicate and edit templates, and the General options |
| Guide | How each page works, what every setting means, examples, and links |

## Templates

A template holds the settings that decide how a model is processed: import options, Nanite, collision, textures and materials. It can also set the destination folder. Every import uses a template exactly as it is saved on the Settings page.

| Template | Collision | Translucent materials | Schedule materials | CPU access |
|---|---|---|---|---|
| Datasmith Model | Simple Box: one box per mesh | Kept translucent | Not handled | Off |
| Datasmith Model with Collision | Complex: traces hit the real surface | Kept translucent | Not handled | Off |
| Twin Sync Model | Simple Box: one box per mesh | Masked with a dither | Keep animating | Off |
| Twin Sync Model with Collision | Complex: traces hit the real surface | Masked with a dither | Keep animating | On |

The Twin Sync templates are for models from [Twin Sync Studio](https://graphl.tech/twin-sync-studio/) that play a construction schedule.

- Built-in templates can't be changed. Select one on the Settings page and press **Duplicate** to make your own.
- Changes are kept on a copy until you press **Save changes**. **Revert** goes back to the saved version. You're asked before leaving a template with unsaved changes.
- Each setting has a short description beside it. Hover over the name or the description for more.
- Tick **Destination** on a project template to make it import into a set folder.
- Project templates are text files in `Config/ModelConstructor/Templates`. Commit them to share with your team, or copy them to another project and choose **More > Reload templates**.
- Each model records the settings and template it was imported with. Model Manager shows the template, and re-import can reuse the settings.

### General

**General**, at the top of the Settings list, is not a template. It holds the options that apply to every import, whichever template you choose: what happens when the model folder already exists, whether each model gets its own level, what happens when the run finishes, saving, and batch sizes. These are saved per user, per project.

| Setting | Default | Notes |
|---|---|---|
| Create Level Beside Model | On | Each model gets `L_<Folder>` in its folder. Needed for re-import |
| When The Run Finishes | Return to previous level | Or Open the model level |
| Save After Import | On | Remove and re-import need a saved import |
| Mesh Batch Size | 128 | Lower it if memory runs short |
| Texture and Material Batch Size | 32 | Lower them if memory runs short |

### Collision

- **Simple Box** is the lightest. Traces hit a box around each mesh, not its surface, and no triangle collision is built.
- **Complex** uses the mesh surface. With Nanite on, that is the fallback mesh, so **Fallback Percent Triangles** sets how detailed the collision is. Meshes over **Complex Collision Triangle Limit** (default 1,500,000) get no collision and are listed. Meshes over **Navigation Triangle Limit** (default 150,000) are left out of navigation.
- Traces against complex collision do not need CPU access. CPU access is for runtime code that reads mesh vertices, and it adds memory to every mesh.
- On schedule models, hidden elements still have their collision unless your schedule code turns it off.

![The Settings page, with the template list and the selected template's settings](Images/settings-page.png)

## Import a model

1. **Template:** choose one. The line under it says what it does. **Edit...** opens it on the Settings page.
2. **Source file:** pick a `.udatasmith` file.
3. **Destination:** the content folder that holds your models, for example `/Game/Models`.
4. **Model name** (optional): tick it and type a name to use instead of the file name. The folder uses the name with spaces and symbols replaced by underscores.
5. Check the line above the **Import** button. It shows the folder the import will create, or why it can't run.
6. Press **Import**. A progress window shows each stage, and you can cancel it.

When the import finishes, the stage table shows what each stage did and the warnings box under it lists anything worth checking. Collision Mode None is a choice, so the meshes it affects are only counted in the Output Log, not listed as warnings.

![The Import page after an import, with the results table for each stage](Images/import-page.png)

### What the import does

| Stage | What happens |
|---|---|
| Prepare | Checks the file, disk space and memory, and offers to save open changes |
| Level | Creates `L_<Folder>` in the model folder |
| Import | Runs the Datasmith import into the model folder |
| Finish import builds | Waits for the import's builds, clears its undo history and frees memory. The detail says what it waited on, such as distance fields |
| Meshes | One pass per mesh: collision, Nanite and build settings, then one rebuild in batches |
| Texture | Caps texture size and sets virtual texture streaming, in batches |
| Material | Converts translucent materials to masked with a dither, sets the Nanite flag and matches sampler types to the textures, in batches |
| Finish pass builds | Waits for the last mesh, texture, distance field, mesh card and shader builds, and says how long each took |
| Report | Counts assets and actors and adds warnings |
| Save | Saves the model folder in batches, then the scene, then the level once |

Every pass checks before it changes anything, so running the passes again reports everything as already OK.

### Large models

The run is built not to hold too much in memory at once:

- Meshes rebuild in batches, by count and by triangle total, and each batch finishes before the next starts. Very large meshes build on their own.
- Textures and materials change in batches and wait for their builds.
- The model's actors are taken out of the renderer, physics and navigation while the passes run, and put back once. Viewports stop redrawing in real time during the run.
- Packages save in batches, and each batch is written to the Output Log, so a log shows where a save stopped.
- Before importing, the tool refuses a `.udatasmith` over about 1.9 GB (Datasmith cannot load a scene description that large) and warns from 1 GB, or when disk space or memory looks short.

Batch sizes are under **General** on the Settings page. Lower them if memory runs short.

If the editor closes during a run, the Import page shows which run and stage it was on the next time you open the tool.

### Schedule animation

Schedule Animation Support only acts when a material calls the schedule animation function. Those materials keep showing and hiding elements as the schedule plays. When such materials are found, the report warns if the schedule actor did not come in, which usually means the schedule plugin is not enabled in the project.

### Textures and materials

- **Max Texture Size** caps only textures whose source is larger. 0 turns the cap off.
- **Virtual Texture Streaming** is off by default. It helps when a model has many large textures, such as 4K to 8K or UDIM sets, and does little for typical building models with a few textures at 2048 or less. A virtual texture needs a virtual sampler in every material that uses it, so the pass switches textures and the samplers in the model's own materials together. Textures that a material outside the model folder also uses are left as they are and listed. The project needs virtual textures turned on.
- **Convert Translucent To Masked With Dither** only changes materials in the model folder. Material instances whose parent material is outside the folder, such as shared library materials, are listed in the report if they are still translucent.
- **Set Used With Nanite** marks the model's materials so packaged builds draw them on Nanite meshes. Parent materials outside the folder are not changed; the report lists any that are missing the flag.

### Import times

Most of the time goes on building meshes: Nanite data, then distance fields and mesh cards for Lumen. The results table shows how long each stage took, and the Finish builds rows say what they waited on.

The engine keeps built data in its derived data cache. Importing a file it has seen before, with the same settings, is much faster because most builds load from the cache. Changing a template setting that affects meshes means the next import builds them again. To time a first import, point the editor at an empty cache folder with **Global Local DDC Path** in Editor Preferences.

## Model Manager

Lists every Datasmith scene in the project. Tick **Include plugins** to also list scenes in project and marketplace content plugins. Select a scene to see its level, scene asset, template, source file and times. An orange note appears when the source file has changed or moved since the import.

| Button | What it does |
|---|---|
| Re-import... | Opens the re-import section under the details |
| Show in Content Browser | Opens the model folder |
| Remove... | Deletes the model. See below |
| More > Copy scene path, Copy source path | Copies the scene asset's object path, or the file it was imported from |
| More > Rename... | Changes the name shown for the model. No asset or folder is renamed |
| More > Save import settings as template... | Makes a project template from the model's recorded settings and opens it on the Settings page |
| Open level | Opens the selected level that uses the model |

![The Model Manager page, with the model list and the selected model's details](Images/model-manager.png)

## Re-import a model

Re-import replaces a model with a fresh import of its source, in the same folder, under the same name, with its level at the same path.

1. Select the model in Model Manager and press **Re-import...**. The re-import section opens under the details.
2. **Source file:** defaults to the recorded file. Pick another file if it moved.
3. **Settings:** by default the settings recorded at the last import. You can choose a template instead. The line under it says what they do. The General settings always come from your current ones.
4. Check the line above **Re-import...** in the section: it names the folder that will be replaced, or why it can't run. Press it. The window shows what will be deleted. Press **Re-import** in the window to go ahead, or **Cancel**.

To change one setting for a re-import, use **More > Save import settings as template...**, change it on the Settings page, then choose that template under Settings.

The run then:

1. Closes the open level if it is the model level or streams it, and opens a blank level.
2. Removes the old version: unloads it, deletes its files, and updates the asset registry.
3. Checks the editor is ready: nothing of the old version is loaded, listed in the asset registry or still on disk. The import does not start until this passes.
4. Imports as normal into the same folder and level path, and keeps the model's name and id.
5. Finishes as set in **When The Run Finishes**.

Levels that stream the model level keep working, because the level comes back at the same path. Actors you added to the model level yourself are lost. Models imported into an existing level cannot be re-imported; remove them and import again.

## Remove a model

Remove deletes a model's folder and the assets its import created. It is built to never touch anything else.

- It only works on models imported with Model Constructor, with saving on.
- It deletes only assets that the import recorded. If anything else is in the folder, it refuses and lists those assets.
- It refuses if anything outside the folder uses the model, and lists those uses.
- The confirmation window shows the folder, the disk path, the asset count by type, the files and size on disk, and every package that will be deleted. Nothing is deleted until you press **Remove** in the window.
- It checks the folder again just before deleting. If anything changed after you reviewed it, nothing is deleted.
- If the open level is part of the model or streams it, it is closed and a blank level opens.
- Files are deleted from disk without loading the model first, which is fast on large models. With source control on, the engine delete is used and the files are marked for delete; that loads the model first and is slower.
- Files that are not assets are left in place, and so is their folder.

Deleted files cannot be restored from the editor.

## Python and Blueprint

`UMCLibrary` exposes the same functions: `ImportModel`, `GetTemplateNames`, `ApplyTemplate`, `SaveTemplate`, `ReimportModel`, `PlanReimportModel`, `GetRecordedSettings`, `RunPasses`, `ResolveModelFolder`, `FindScenes`, `OpenLevel`, `SetModelDisplayName`, `PlanRemoveModel` and `RemoveModel`. `RemoveModel` and `ReimportModel` need the fingerprint from their plan, so a script always reviews the change before it runs.

`ImportModel(None)` imports with the Import page's saved settings as they are. The Import page itself applies the chosen template first. `after_run` takes `RETURN_TO_PREVIOUS` or `OPEN_MODEL_LEVEL`.

```python
import unreal
lib = unreal.MCLibrary
print(lib.get_template_names())
lib.apply_template("Twin Sync Model", None)  # None: the Import page settings
```

```python
import unreal
lib = unreal.MCLibrary
scene = "/Game/Models/MyModel/MyModel/MyModel.MyModel"
plan = lib.plan_reimport_model(scene)
print(plan.can_remove, plan.blockers, plan.notes, len(plan.packages))
if plan.can_remove:
    # None: the template settings recorded at the last import, with the current General settings.
    result = lib.reimport_model(scene, plan.fingerprint, None)
    print(result.succeeded, result.error)
```
