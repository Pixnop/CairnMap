import unreal
out = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
if unreal.EditorAssetLibrary.does_asset_exist("/Game/CairnMap/W_Probe"):
    unreal.EditorAssetLibrary.delete_asset("/Game/CairnMap/W_Probe")
f = unreal.WidgetBlueprintFactory()
wbp = tools.create_asset("W_Probe", "/Game/CairnMap", unreal.WidgetBlueprint, f)
out.append("type=" + str(type(wbp)))
try:
    t = wbp.widget_tree
    out.append("wbp.widget_tree OK: " + str(type(t)))
    try:
        w = t.construct_widget(unreal.VerticalBox, "TestVBox")
        out.append("construct_widget OK: " + str(type(w)))
        t.set_editor_property("root_widget", w)
        out.append("set root_widget OK")
    except Exception as e:
        out.append("construct/root FAIL: " + str(e))
except Exception as e:
    out.append("wbp.widget_tree FAIL: " + str(e))
open("/tmp/probe_result.txt", "w").write("\n".join(out) + "\n")
