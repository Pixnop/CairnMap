# CairnMap UI builder — runs inside UnrealEditor-Cmd (-run=pythonscript).
# P2 phase 1: import T_Dot and verify scripted asset authoring works headless.
# P2 phase 2 (next): author W_CairnMapPanel's widget tree from the LAYERS
# registry (rows: label + color dot + checkbox with self-contained style).
import unreal

CONTENT_DIR = "/Game/CairnMap"


def import_dot_texture(png_path: str) -> unreal.Texture2D:
    task = unreal.AssetImportTask()
    task.filename = png_path
    task.destination_path = CONTENT_DIR
    task.destination_name = "T_Dot"
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(f"{CONTENT_DIR}/T_Dot")
    assert tex is not None, "T_Dot import failed"
    # crisp dot at small sizes
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def main():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    tex = import_dot_texture(os.path.join(here, "T_Dot.png"))
    unreal.log(f"[CairnMap] imported {tex.get_path_name()}")
    unreal.log("[CairnMap] build_panel.py phase 1 OK")


main()
