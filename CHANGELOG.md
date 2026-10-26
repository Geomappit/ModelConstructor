# Changelog

Notable changes to Model Constructor, newest first. Version numbers follow [Semantic Versioning](https://semver.org/).

## 1.0.0

First public release. Supports Unreal Engine 5.4 to 5.8 on Windows, as a project plugin or an engine plugin.

- **Import page:** import a `.udatasmith` file into its own content folder and level with a chosen template. A progress window shows each stage, and the results table lists what changed, what was already correct and how long each stage took.
- **Templates:** four built-in templates, Datasmith Model, Datasmith Model with Collision, Twin Sync Model and Twin Sync Model with Collision. Duplicate one to make a project template, saved in `Config/ModelConstructor/Templates` so a team can share it.
- **Nanite and mesh build settings** applied in batches, with a triangle budget per batch so memory stays steady on large models.
- **Collision:** one box per mesh, or complex collision on the real surface, with limits for very large meshes and for navigation.
- **Textures and materials:** texture size cap, optional virtual texture streaming, translucent materials converted to a dithered mask, and materials marked for use with Nanite.
- **Twin Sync models:** materials that drive schedule animation keep animating, and the report warns when the schedule actor is missing.
- **Model Manager:** lists every Datasmith scene in the project with its folder, level, source file and template, and notes when the source file has changed or moved.
- **Re-import and remove:** replace a model at the same paths, or delete only the assets its import made, after a plan that lists every affected package.
- **Guide page** inside the tool, and a [user guide](Docs/UserGuide.md).
- **Python and Blueprint** access through `UMCLibrary`.
