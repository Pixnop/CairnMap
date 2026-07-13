# CairnMap UI builder — runs inside UnrealEditor-Cmd (-run=pythonscript).
# Builds W_CairnMapPanel: a self-contained WidgetBlueprint (engine UMG only,
# no game refs) whose cooked style assets survive shipping-game stripping.
# One row per layer: [checkbox] [colored dot] [label]; plus a title and two
# counter texts. The C++ side loads it, reads the named checkboxes, and drives
# per-layer visibility (SPEC 2.5). Named widgets: cb_<key>, dot_<key>,
# txt_count_effigies, txt_count_notes, cb_master.
import unreal

CONTENT_DIR = "/Game/CairnMap"
PANEL_PKG = f"{CONTENT_DIR}/W_CairnMapPanel"

# key, label, RGBA dot color (0-1)  — mirrors the C++ LAYERS registry order.
LAYERS = [
    ("Effigies",   "Effigies",       (0.35, 1.00, 0.20)),
    ("Notes",      "Notes",          (0.20, 0.88, 1.00)),
    ("Coal",       "Coal",           (0.13, 0.13, 0.13)),
    ("Copper",     "Copper",         (0.85, 0.47, 0.17)),
    ("Quartz",     "Quartz",         (0.91, 0.91, 0.97)),
    ("Sulfur",     "Sulfur",         (0.85, 0.78, 0.17)),
    ("Hexolite",   "Hexolite",       (0.22, 0.82, 0.82)),
    ("Oil",        "Oil",            (0.30, 0.20, 0.15)),
    ("SkyOre",     "Sky Island ore", (0.25, 0.85, 1.00)),
    ("TreeOre",    "World Tree ore", (0.30, 1.00, 0.40)),
    ("Magma",      "Magma rock",     (1.00, 0.35, 0.10)),
    ("NightStone", "Night stone",    (0.65, 0.40, 1.00)),
    ("DogCoin",    "Dog coins",      (1.00, 0.85, 0.20)),
    ("Lotus",      "Lotus flowers",  (1.00, 0.45, 0.80)),
]


def _linear(r, g, b, a=1.0):
    return unreal.LinearColor(r, g, b, a)


def make_widget_blueprint(pkg_path):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    name = pkg_path.rsplit("/", 1)[1]
    folder = pkg_path.rsplit("/", 1)[0]
    existing = unreal.load_asset(pkg_path)
    if existing:
        unreal.EditorAssetLibrary.delete_asset(pkg_path)
    factory = unreal.WidgetBlueprintFactory()
    # WidgetBlueprintFactory defaults its parent class to UserWidget; set it
    # only if the property is exposed on this engine version.
    if hasattr(factory, "parent_class"):
        factory.set_editor_property("parent_class", unreal.UserWidget)
    return tools.create_asset(name, folder, unreal.WidgetBlueprint, factory)


def add_child(parent, widget):
    slot = parent.add_child(widget)
    return slot


def build():
    wbp = make_widget_blueprint(PANEL_PKG)
    # WidgetTree root: a vertical box inside a border (background)
    tree = wbp.get_editor_property("widget_tree")

    root_border = tree.construct_widget(unreal.Border, "RootBorder")
    root_border.set_brush_color(_linear(0.03, 0.03, 0.05, 0.82))
    root_border.set_padding(unreal.Margin(8, 8, 8, 8))
    tree.set_editor_property("root_widget", root_border)

    vbox = tree.construct_widget(unreal.VerticalBox, "VBox")
    root_border.set_content(vbox)

    # title
    title = tree.construct_widget(unreal.TextBlock, "Title")
    title.set_text("CairnMap")
    title.set_color_and_opacity(unreal.SlateColor(_linear(1, 1, 1)))
    tslot = vbox.add_child_to_vertical_box(title)
    tslot.set_padding(unreal.Margin(2, 2, 2, 6))

    # master toggle row
    master_row = tree.construct_widget(unreal.HorizontalBox, "MasterRow")
    cb_master = tree.construct_widget(unreal.CheckBox, "cb_master")
    cb_master.set_is_checked(True)
    master_row.add_child_to_horizontal_box(cb_master)
    master_lbl = tree.construct_widget(unreal.TextBlock, "MasterLabel")
    master_lbl.set_text("Show all")
    master_lbl.set_color_and_opacity(unreal.SlateColor(_linear(0.9, 0.9, 0.9)))
    mlslot = master_row.add_child_to_horizontal_box(master_lbl)
    mlslot.set_padding(unreal.Margin(6, 0, 0, 0))
    vbox.add_child_to_vertical_box(master_row)

    # one row per layer
    for key, label, (r, g, b) in LAYERS:
        row = tree.construct_widget(unreal.HorizontalBox, f"Row_{key}")

        cb = tree.construct_widget(unreal.CheckBox, f"cb_{key}")
        cb.set_is_checked(True)
        row.add_child_to_horizontal_box(cb)

        dot = tree.construct_widget(unreal.Image, f"dot_{key}")
        dot.set_color_and_opacity(_linear(r, g, b))
        dot.set_brush_size(unreal.Vector2D(12, 12))
        dslot = row.add_child_to_horizontal_box(dot)
        dslot.set_padding(unreal.Margin(6, 2, 4, 2))
        dslot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)

        lbl = tree.construct_widget(unreal.TextBlock, f"lbl_{key}")
        lbl.set_text(label)
        lbl.set_color_and_opacity(unreal.SlateColor(_linear(0.92, 0.92, 0.92)))
        lslot = row.add_child_to_horizontal_box(lbl)
        lslot.set_padding(unreal.Margin(4, 2, 2, 2))

        rslot = vbox.add_child_to_vertical_box(row)
        rslot.set_padding(unreal.Margin(0, 1, 0, 1))

    # counters
    counters = tree.construct_widget(unreal.HorizontalBox, "Counters")
    ce = tree.construct_widget(unreal.TextBlock, "txt_count_effigies")
    ce.set_text("0/407 effigies")
    ce.set_color_and_opacity(unreal.SlateColor(_linear(0.35, 1.0, 0.2)))
    ceslot = counters.add_child_to_horizontal_box(ce)
    ceslot.set_padding(unreal.Margin(2, 4, 8, 2))
    cn = tree.construct_widget(unreal.TextBlock, "txt_count_notes")
    cn.set_text("0/64 notes")
    cn.set_color_and_opacity(unreal.SlateColor(_linear(0.2, 0.88, 1.0)))
    counters.add_child_to_horizontal_box(cn)
    vbox.add_child_to_vertical_box(counters)

    unreal.EditorAssetLibrary.save_loaded_asset(wbp)
    unreal.log(f"[CairnMap] built {PANEL_PKG}")


def import_dot_texture(png_path):
    task = unreal.AssetImportTask()
    task.filename = png_path
    task.destination_path = CONTENT_DIR
    task.destination_name = "T_Dot"
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(f"{CONTENT_DIR}/T_Dot")
    if tex:
        tex.set_editor_property("compression_settings",
                                unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("mip_gen_settings",
                                unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)


def main():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    import_dot_texture(os.path.join(here, "T_Dot.png"))
    build()
    unreal.log("[CairnMap] build_panel.py OK")


main()
