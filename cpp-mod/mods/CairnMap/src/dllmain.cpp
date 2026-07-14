// CairnMap — map collectables for Palworld 1.0+ (see docs/SPEC-2.0.md)
// P1: static layers rendered as tinted dots in our own canvas, positioned by
// the boss-tower-anchored projection (cairn_project.hpp, unit-tested natively).
// ABI target: UE4SS v3.0.1 (Okaetsu experimental-palworld, c2ac246, MSVC).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

#include "cairn_data.hpp"
#include "cairn_icons.hpp"
#include "cairn_project.hpp"

namespace CairnMap
{
    using namespace RC;
    using namespace RC::Unreal;

    // ------------------------------------------------------------ engine facade
    // ⚠ SPEC 2.6: everything here runs on the game thread (on_update).
    namespace Engine
    {
        // BlueprintCallable param blocks (natural MSVC layout, x64).
        struct ParamsGetChildrenCount
        {
            int32_t ReturnValue{};
        };
        struct ParamsGetChildAt
        {
            int32_t Index{};
            UObject* ReturnValue{};
        };
        struct ParamsIsVisible
        {
            bool ReturnValue{};
        };
        struct ParamsAddChildToCanvas
        {
            UObject* Content{};
            UObject* ReturnValue{};
        };
        struct FVector2D_
        {
            double X{}, Y{};
        };
        struct ParamsSetPosition
        {
            FVector2D_ InPosition{};
        };
        struct ParamsSetSize
        {
            FVector2D_ InSize{};
        };
        struct ParamsSetAlignment
        {
            FVector2D_ InAlignment{};
        };
        struct ParamsSetAutoSize
        {
            bool bAutoSize{};
        };
        struct FLinearColor_
        {
            float R{}, G{}, B{}, A{};
        };
        struct ParamsSetColorAndOpacity
        {
            FLinearColor_ InColorAndOpacity{};
        };
        struct ParamsSetVisibility
        {
            uint8_t InVisibility{};
        };
        struct ParamsSetZOrder
        {
            int32_t InZOrder{};
        };
        struct ParamsSetOffsets
        {
            float Left{}, Top{}, Right{}, Bottom{};   // FMargin
        };
        struct ParamsSetAnchors
        {
            double MinX{}, MinY{}, MaxX{}, MaxY{};   // FAnchors
        };
        struct ParamsSetBrushFromTexture
        {
            UObject* Texture{};
            bool bMatchSize{};
        };
        struct ParamsSetIsChecked
        {
            bool InIsChecked{};
        };
        struct ParamsIsChecked
        {
            bool ReturnValue{};
        };
        struct ParamsSetText
        {
            FText InText{};
        };
        struct FVector_
        {
            double X{}, Y{}, Z{};
        };
        struct ParamsGetActorLocation
        {
            FVector_ ReturnValue{};
        };
        struct ParamsSetContent
        {
            UObject* Content{};
        };
        struct ParamsSetCanCache
        {
            bool CanCache{};
        };
        // UKismetSystemLibrary::LoadAsset_Blocking(TSoftObjectPtr<UObject>) -> UObject*
        // Loads a texture asset from the player's OWN installed game (nothing is
        // bundled/redistributed). TSoftObjectPtr = FWeakObjectPtr + tag + FSoftObjectPath.
        struct ParamsLoadAssetBlocking
        {
            int32_t weak_index{};
            int32_t weak_serial{};
            int32_t tag_at_last_test{};
            int32_t _pad0{};
            FName package_name{};   // FSoftObjectPath.AssetPath.PackageName
            FName asset_name{};     // FSoftObjectPath.AssetPath.AssetName
            void* subpath_data{};   // FString SubPathString (empty)
            int32_t subpath_num{};
            int32_t subpath_max{};
            UObject* ReturnValue{};
        };

        enum : uint8_t
        {
            Vis_Visible = 0,
            Vis_Collapsed = 1,
            Vis_Hidden = 2,
            Vis_HitTestInvisible = 3,
            Vis_SelfHitTestInvisible = 4,
        };

        template <typename Params>
        auto call(UObject* obj, const wchar_t* fn_name, Params& params) -> bool
        {
            if (!obj)
            {
                return false;
            }
            UFunction* fn = obj->GetFunctionByNameInChain(FName(fn_name, FNAME_Find));
            if (!fn)
            {
                return false;
            }
            obj->ProcessEvent(fn, &params);
            return true;
        }

        inline auto children_count(UObject* panel) -> int32_t
        {
            ParamsGetChildrenCount p{};
            return call(panel, L"GetChildrenCount", p) ? p.ReturnValue : 0;
        }

        inline auto child_at(UObject* panel, int32_t i) -> UObject*
        {
            ParamsGetChildAt p{i, nullptr};
            return call(panel, L"GetChildAt", p) ? p.ReturnValue : nullptr;
        }

        inline auto widget_name(UObject* w) -> std::wstring
        {
            return w ? w->GetName() : std::wstring{};
        }

        // World-space actor location via K2_GetActorLocation (FVector return).
        inline auto actor_location(UObject* actor, double& x, double& y, double& z) -> bool
        {
            ParamsGetActorLocation p{};
            if (!call(actor, L"K2_GetActorLocation", p))
            {
                return false;
            }
            x = p.ReturnValue.X;
            y = p.ReturnValue.Y;
            z = p.ReturnValue.Z;
            return true;
        }

        // Load a texture object from an asset path already present in the player's
        // game files (nothing bundled). Returns nullptr if absent or load fails.
        inline auto load_game_texture(const wchar_t* package_path, const wchar_t* asset_name) -> UObject*
        {
            auto* lib = UObjectGlobals::StaticFindObject(nullptr, nullptr,
                                                         STR("/Script/Engine.Default__KismetSystemLibrary"));
            if (!lib)
            {
                return nullptr;
            }
            ParamsLoadAssetBlocking p{};
            p.package_name = FName(package_path, FNAME_Add);
            p.asset_name = FName(asset_name, FNAME_Add);
            if (!call(lib, L"LoadAsset_Blocking", p))
            {
                return nullptr;
            }
            return p.ReturnValue;
        }

        inline auto class_name(UObject* w) -> std::wstring
        {
            return (w && w->GetClassPrivate()) ? w->GetClassPrivate()->GetName() : std::wstring{};
        }

        inline auto find_descendant(UObject* w, const std::wstring& name, int depth = 0) -> UObject*
        {
            if (!w || depth > 8)
            {
                return nullptr;
            }
            if (widget_name(w) == name)
            {
                return w;
            }
            const int32_t n = children_count(w);
            for (int32_t i = 0; i < n; ++i)
            {
                if (auto* r = find_descendant(child_at(w, i), name, depth + 1))
                {
                    return r;
                }
            }
            return nullptr;
        }

        // Canvas slot offsets: LayoutData.Offsets (FMargin, 4 floats) leads the struct.
        inline auto slot_position(UObject* widget, double& x, double& y) -> bool
        {
            if (!widget)
            {
                return false;
            }
            auto** slot = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Slot"));
            if (!slot || !*slot)
            {
                return false;
            }
            auto* layout = (*slot)->GetValuePtrByPropertyNameInChain<float>(STR("LayoutData"));
            if (!layout)
            {
                return false;
            }
            x = layout[0];   // Offsets.Left
            y = layout[1];   // Offsets.Top
            return true;
        }
    } // namespace Engine


    // ----------------------------------------------------------- dot styling
    // Round dots without any texture: FSlateBrush.DrawAs = RoundedBox with a
    // fixed corner radius, written via reflection-resolved offsets.
    namespace Style
    {
        struct Offsets
        {
            int32_t draw_as = -1, outline = -1, radii = -1, rounding = -1, tint = -1;
            int32_t outline_color = -1, outline_width = -1;
            bool resolved = false;
        };

        inline auto resolve(Offsets& off) -> bool
        {
            if (off.resolved)
            {
                return true;
            }
            auto* brush_struct =
                UObjectGlobals::StaticFindObject<UStruct*>(nullptr, nullptr, STR("/Script/SlateCore.SlateBrush"));
            auto* outline_struct = UObjectGlobals::StaticFindObject<UStruct*>(
                nullptr, nullptr, STR("/Script/SlateCore.SlateBrushOutlineSettings"));
            if (!brush_struct || !outline_struct)
            {
                return false;
            }
            for (FProperty* prop : brush_struct->ForEachProperty())
            {
                if (prop->GetName() == STR("DrawAs"))
                {
                    off.draw_as = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("OutlineSettings"))
                {
                    off.outline = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("TintColor"))
                {
                    off.tint = prop->GetOffset_Internal();
                }
            }
            for (FProperty* prop : outline_struct->ForEachProperty())
            {
                if (prop->GetName() == STR("CornerRadii"))
                {
                    off.radii = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("RoundingType"))
                {
                    off.rounding = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("Color"))
                {
                    off.outline_color = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("Width"))
                {
                    off.outline_width = prop->GetOffset_Internal();
                }
            }
            off.resolved = off.draw_as >= 0 && off.outline >= 0 && off.radii >= 0 && off.rounding >= 0;
            return off.resolved;
        }

        // paint a raw FSlateBrush block as a tinted rounded box
        inline auto paint_brush(uint8_t* brush, const Offsets& off, float r, float g, float b, float a,
                                double radius) -> void
        {
            brush[off.draw_as] = 4;   // RoundedBox
            uint8_t* outline = brush + off.outline;
            outline[off.rounding] = 0;   // FixedRadius
            auto* radii = reinterpret_cast<double*>(outline + off.radii);
            radii[0] = radii[1] = radii[2] = radii[3] = radius;
            // white border for contrast against the map (Elio/TrueGuardian32 feedback)
            if (off.outline_width >= 0)
            {
                *reinterpret_cast<float*>(outline + off.outline_width) = 0.0f;
            }
            if (off.outline_color >= 0)
            {
                auto* oc = reinterpret_cast<float*>(outline + off.outline_color);
                oc[0] = 1.0f;
                oc[1] = 1.0f;
                oc[2] = 1.0f;
                oc[3] = 0.9f;
            }
            if (off.tint >= 0)
            {
                // FSlateColor: FLinearColor SpecifiedColor leads the struct
                auto* col = reinterpret_cast<float*>(brush + off.tint);
                col[0] = r;
                col[1] = g;
                col[2] = b;
                col[3] = a;
            }
        }

        // Style a runtime CheckBox so it is visible AND clickable in a shipped
        // game (default brushes are stripped -> 0px invisible). Unchecked = dark
        // box, checked = accent box; hovered/pressed mirror them.
        inline auto make_checkbox(UObject* cb, float ar, float ag, float ab) -> void
        {
            static Offsets off;
            if (!resolve(off) || off.tint < 0)
            {
                return;
            }
            static int32_t style_off = -1;
            static int32_t f_unchecked = -1, f_unhover = -1, f_unpress = -1;
            static int32_t f_checked = -1, f_chkhover = -1, f_chkpress = -1;
            if (style_off < 0)
            {
                if (auto* cb_class = cb->GetClassPrivate())
                {
                    for (FProperty* p : cb_class->ForEachPropertyInChain())
                    {
                        if (p->GetName() == STR("WidgetStyle"))
                        {
                            style_off = p->GetOffset_Internal();
                        }
                    }
                }
                auto* cbs = UObjectGlobals::StaticFindObject<UStruct*>(nullptr, nullptr,
                                                                       STR("/Script/SlateCore.CheckBoxStyle"));
                if (cbs)
                {
                    for (FProperty* p : cbs->ForEachProperty())
                    {
                        const auto n = p->GetName();
                        if (n == STR("UncheckedImage")) f_unchecked = p->GetOffset_Internal();
                        else if (n == STR("UncheckedHoveredImage")) f_unhover = p->GetOffset_Internal();
                        else if (n == STR("UncheckedPressedImage")) f_unpress = p->GetOffset_Internal();
                        else if (n == STR("CheckedImage")) f_checked = p->GetOffset_Internal();
                        else if (n == STR("CheckedHoveredImage")) f_chkhover = p->GetOffset_Internal();
                        else if (n == STR("CheckedPressedImage")) f_chkpress = p->GetOffset_Internal();
                    }
                }
            }
            if (style_off < 0 || f_unchecked < 0 || f_checked < 0)
            {
                return;
            }
            auto* style = cb->GetValuePtrByPropertyNameInChain<uint8_t>(STR("WidgetStyle"));
            if (!style)
            {
                return;
            }
            auto dark = [&](int32_t f) {
                if (f >= 0) paint_brush(style + f, off, 0.12f, 0.12f, 0.14f, 1.0f, 3.0);
            };
            auto accent = [&](int32_t f) {
                if (f >= 0) paint_brush(style + f, off, ar, ag, ab, 1.0f, 3.0);
            };
            dark(f_unchecked);
            dark(f_unhover);
            dark(f_unpress);
            accent(f_checked);
            accent(f_chkhover);
            accent(f_chkpress);
        }

        // shrink a TextBlock's font (FSlateFontInfo.Size) via reflection
        inline auto set_font_size(UObject* text_widget, int32_t size) -> void
        {
            static int32_t size_off = -1;
            if (size_off < 0)
            {
                auto* fi = UObjectGlobals::StaticFindObject<UStruct*>(nullptr, nullptr,
                                                                     STR("/Script/SlateCore.SlateFontInfo"));
                if (fi)
                {
                    for (FProperty* p : fi->ForEachProperty())
                    {
                        if (p->GetName() == STR("Size"))
                        {
                            size_off = p->GetOffset_Internal();
                        }
                    }
                }
            }
            if (size_off < 0)
            {
                return;
            }
            auto* font = text_widget->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Font"));
            if (font)
            {
                *reinterpret_cast<int32_t*>(font + size_off) = size;
            }
        }

        inline auto make_image(UObject* image_widget) -> void
        {
            static Offsets off;
            if (!resolve(off))
            {
                return;
            }
            auto* brush = image_widget->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Brush"));
            if (brush)
            {
                brush[off.draw_as] = 0;   // ESlateBrushDrawType::Image (full frame, no crop)
            }
        }

        inline auto make_round(UObject* image_widget) -> void
        {
            static Offsets off;
            if (!resolve(off))
            {
                return;
            }
            auto* brush = image_widget->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Brush"));
            if (!brush)
            {
                return;
            }
            brush[off.draw_as] = 4;   // ESlateBrushDrawType::RoundedBox
            uint8_t* outline = brush + off.outline;
            outline[off.rounding] = 0;   // ESlateBrushRoundingType::FixedRadius
            auto* radii = reinterpret_cast<double*>(outline + off.radii);
            radii[0] = radii[1] = radii[2] = radii[3] = 6.0;
            // white border so dots stay legible against the map background
            if (off.outline_width >= 0)
            {
                *reinterpret_cast<float*>(outline + off.outline_width) = 0.0f;
            }
            if (off.outline_color >= 0)
            {
                auto* oc = reinterpret_cast<float*>(outline + off.outline_color);
                oc[0] = 1.0f;
                oc[1] = 1.0f;
                oc[2] = 1.0f;
                oc[3] = 0.9f;
            }
        }

        // Switch a dot's brush back to plain Image draw so an assigned texture is
        // drawn as a full rectangle (SetBrushFromTexture leaves DrawAs untouched, so
        // the RoundedBox rounding from make_round would otherwise clip the icon).
        inline auto draw_as_image(UObject* image_widget) -> void
        {
            static Offsets off;
            if (!resolve(off))
            {
                return;
            }
            if (auto* brush = image_widget->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Brush")))
            {
                brush[off.draw_as] = 3;   // ESlateBrushDrawType::Image
            }
        }
    } // namespace Style

    // -------------------------------------------------- collected state (P1.5)
    // Truth source: PalPlayerRecordData's <X>ObtainForInstanceFlag arrays;
    // keys are the actors' LevelObjectInstanceId as 32-hex FName strings
    // (grounded live 2026-07-13, probe run).
    namespace Collected
    {
        struct Offsets
        {
            int32_t items = -1, key = -1, value = -1, item_size = 0;
            bool resolved = false;
        };

        inline auto resolve(Offsets& off) -> bool
        {
            if (off.resolved)
            {
                return true;
            }
            auto* wrapper_struct = UObjectGlobals::StaticFindObject<UStruct*>(
                nullptr, nullptr, STR("/Script/Pal.PalPlayerRecordDataRepInfoArrayThreadSafe_BoolVal"));
            auto* item_struct = UObjectGlobals::StaticFindObject<UStruct*>(
                nullptr, nullptr, STR("/Script/Pal.PalPlayerRecordDataRepInfoThreadSafe_BoolVal"));
            if (!wrapper_struct || !item_struct)
            {
                return false;
            }
            for (FProperty* prop : wrapper_struct->ForEachProperty())
            {
                if (prop->GetName() == STR("Items"))
                {
                    off.items = prop->GetOffset_Internal();
                }
            }
            for (FProperty* prop : item_struct->ForEachProperty())
            {
                if (prop->GetName() == STR("Key"))
                {
                    off.key = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("Value"))
                {
                    off.value = prop->GetOffset_Internal();
                }
            }
            off.item_size = item_struct->GetStructureSize();
            off.resolved = off.items >= 0 && off.key >= 0 && off.value >= 0 && off.item_size > 0;
            return off.resolved;
        }

        // union of all true-flag keys across the relic/note arrays
        inline auto gather(std::unordered_set<std::wstring>& out) -> bool
        {
            static Offsets off;
            if (!resolve(off))
            {
                return false;
            }
            auto* util_cdo =
                UObjectGlobals::StaticFindObject(nullptr, nullptr, STR("/Script/Pal.Default__PalUtility"));
            auto* world_ctx = UObjectGlobals::FindFirstOf(STR("PalPlayerController"));
            if (!util_cdo || !world_ctx)
            {
                return false;
            }
            struct
            {
                UObject* WorldContextObject{};
                UObject* ReturnValue{};
            } rec{world_ctx, nullptr};
            if (!Engine::call(util_cdo, L"GetLocalRecordData", rec) || !rec.ReturnValue)
            {
                return false;
            }
            static const wchar_t* kFlagArrays[] = {
                STR("RelicObtainForInstanceFlag_CapturePower"),
                STR("RelicObtainForInstanceFlag_HungerReduction"),
                STR("RelicObtainForInstanceFlag_SwimSpeed"),
                STR("RelicObtainForInstanceFlag_FoodDecayReduction"),
                STR("RelicObtainForInstanceFlag_JumpPower"),
                STR("RelicObtainForInstanceFlag_GliderSpeed"),
                STR("RelicObtainForInstanceFlag_ClimbSpeed"),
                STR("RelicObtainForInstanceFlag_StatusAilmentResist"),
                STR("RelicObtainForInstanceFlag_StaminaReduction"),
                STR("RelicObtainForInstanceFlag_SphereHoming"),
                STR("RelicObtainForInstanceFlag_ExpBonus"),
                STR("RelicObtainForInstanceFlag_RainbowPassiveRate"),
                STR("RelicObtainForInstanceFlag_MoveSpeed"),
                STR("NoteObtainForInstanceFlag"),
            };
            out.clear();
            for (const auto* arr_name : kFlagArrays)
            {
                auto* wrapper = rec.ReturnValue->GetValuePtrByPropertyNameInChain<uint8_t>(arr_name);
                if (!wrapper)
                {
                    continue;
                }
                struct RawArray
                {
                    uint8_t* data;
                    int32_t num;
                    int32_t max;
                };
                const auto* arr = reinterpret_cast<const RawArray*>(wrapper + off.items);
                for (int32_t i = 0; i < arr->num; ++i)
                {
                    const uint8_t* item = arr->data + static_cast<size_t>(i) * off.item_size;
                    if (*(item + off.value) == 0)
                    {
                        continue;
                    }
                    const auto* key = reinterpret_cast<const FName*>(item + off.key);
                    out.insert(key->ToString());
                }
            }
            return true;
        }

        inline auto guid_key(const uint32_t g[4]) -> std::wstring
        {
            wchar_t buf[36];
            swprintf(buf, 36, L"%08X%08X%08X%08X", g[0], g[1], g[2], g[3]);
            return buf;
        }
    } // namespace Collected

    // ---------------------------------------------------------------- the mod
    class Mod final : public RC::CppUserModBase
    {
        bool m_unreal_ready = false;
        std::chrono::steady_clock::time_point m_last_tick{};

        // per-map-body state (SPEC 2.4): pool keyed by the live canvas
        std::wstring m_canvas_full_name;
        UObject* m_layer_canvas = nullptr;                 // our own CanvasPanel
        UObject* m_inv_box = nullptr;                       // InvalidationBox wrapping it
        std::unordered_map<int, UObject*> m_layer_icon;    // layer_id -> loaded game texture
        UObject* m_layer_slot = nullptr;                   // its canvas slot
        uint8_t m_mask_geom[64] = {};                      // last-seen mask LayoutData
        // layer ids: 0..N-1 = Data::kLayers index; 1000 = effigies, 1001 = notes,
        // 1002 = live eggs (no static positions; enumerated from loaded actors)
        static constexpr int kEffigyLayer = 1000;
        static constexpr int kNoteLayer = 1001;
        static constexpr int kEggLayer = 1002;
        static constexpr int kMasterLayer = -1;   // "show all" master toggle
        struct Dot
        {
            UObject* widget;
            UObject* slot;
            const wchar_t* icon;   // nullptr = plain dot
            bool icon_applied;
            double base_size;
            int layer_id;
            bool base_hidden;   // collected effigy/note: hidden regardless of toggle
        };
        std::vector<Dot> m_dots;                           // pooled dot widgets
        std::unordered_map<int, bool> m_layer_on;          // toggle state per layer id

        // Persist toggle choices between sessions (LOCALAPPDATA\CairnMap\toggles.cfg).
        static auto config_path() -> std::filesystem::path
        {
            const char* base = std::getenv("LOCALAPPDATA");
            std::filesystem::path dir =
                base ? std::filesystem::path(base) / "CairnMap" : std::filesystem::path("CairnMap");
            return dir / "toggles.cfg";
        }
        auto save_toggles() -> void
        {
            const char* base = std::getenv("LOCALAPPDATA");
            if (!base || !*base)
            {
                return;   // no writable location: skip silently
            }
            std::error_code ec;
            std::filesystem::path dir = std::filesystem::path(base) / "CairnMap";
            std::filesystem::create_directories(dir, ec);   // non-throwing overload
            if (ec)
            {
                return;
            }
            std::ofstream f(dir / "toggles.cfg", std::ios::trunc);
            if (!f)
            {
                return;
            }
            for (const auto& [id, on] : m_layer_on)
            {
                f << id << ' ' << (on ? 1 : 0) << '\n';
            }
        }
        auto load_toggles() -> void
        {
            try
            {
                std::ifstream f(config_path());
                int id = 0, on = 0;
                while (f >> id >> on)
                {
                    m_layer_on[id] = (on != 0);
                }
            }
            catch (...)
            {
            }
        }

        // interface panel (P2): screen-fixed rows with native checkboxes
        UObject* m_panel_canvas = nullptr;
        std::wstring m_panel_root_name;
        struct PanelRow
        {
            UObject* checkbox;
            int layer_id;
            bool last_checked;
        };
        std::vector<PanelRow> m_panel_rows;
        bool m_panel_first_poll = true;

        auto is_layer_on(int layer_id) const -> bool
        {
            auto it = m_layer_on.find(layer_id);
            return it == m_layer_on.end() ? true : it->second;
        }

        // A panel entry: the title banner, a category header, or a toggle row.
        struct PanelItem
        {
            enum Kind
            {
                Title,
                Header,
                Row
            } kind;
            const wchar_t* label;
            int id;            // layer id (Row only)
            float r, g, b;     // accent (Row only)
        };

        // Look up a kLayers entry's accent colour by array index.
        auto layer_row(int idx) -> PanelItem
        {
            const auto& l = Data::kLayers[idx];
            return {PanelItem::Row, l.key, idx, l.r / 255.0f, l.g / 255.0f, l.b / 255.0f};
        }

        // Grouped, ordered panel model: title, then category headers with their
        // toggle rows. Row order within a category is curated for readability.
        auto panel_items() -> std::vector<PanelItem>
        {
            std::vector<PanelItem> v;
            v.push_back({PanelItem::Title, L"CairnMap", 0, 0, 0, 0});

            v.push_back({PanelItem::Header, L"COLLECTABLES", 0, 0, 0, 0});
            v.push_back({PanelItem::Row, L"Effigies", kEffigyLayer, 0.35f, 1.0f, 0.20f});
            v.push_back({PanelItem::Row, L"Notes", kNoteLayer, 0.20f, 0.88f, 1.0f});
            v.push_back({PanelItem::Row, L"Eggs (nearby)", kEggLayer, 1.0f, 0.82f, 0.15f});

            v.push_back({PanelItem::Header, L"ORES", 0, 0, 0, 0});
            for (int idx : {0, 1, 2, 3, 4, 6, 7, 8, 9})   // Coal..Hexolite, Sky/Tree/Magma/NightStone
            {
                v.push_back(layer_row(idx));
            }

            v.push_back({PanelItem::Header, L"RESOURCES", 0, 0, 0, 0});
            for (int idx : {5, 10, 11, 15})   // Oil, DogCoin, Lotus, FruitTree
            {
                v.push_back(layer_row(idx));
            }

            v.push_back({PanelItem::Header, L"POINTS OF INTEREST", 0, 0, 0, 0});
            for (int idx : {12, 14, 13})   // Chest, Outpost, Junk
            {
                v.push_back(layer_row(idx));
            }
            return v;
        }
        // A collectable dot whose visibility depends on the live obtained set.
        // key = 32-hex instance GUID (effigies) or NoteRowName (notes); both are
        // matched against Collected::gather()'s union of obtained keys.
        struct GuidDot
        {
            size_t dot_index;
            std::wstring key;
        };
        std::vector<GuidDot> m_guid_dots;                  // effigy/note dots for refresh
        double m_applied_zoom = 1.0;
        std::optional<Project::Calibration> m_calibration;
        bool m_placed = false;
        bool m_collapsed = true;
        int m_log_budget = 20;
        bool m_flag_probe_done = false;
        bool m_icon_probe_done = false;

      public:
        Mod()
        {
            ModVersion = STR("2.0.0-p1");
            ModName = STR("CairnMap");
            ModAuthors = STR("Pixnop");
            ModDescription = STR("CairnMap: map collectables for Palworld 1.0+");
            load_toggles();   // restore per-layer on/off from last session
            Output::send<LogLevel::Default>(STR("[CairnMap] loaded (P1)\n"));
        }

        auto on_unreal_init() -> void override
        {
            m_unreal_ready = true;
            Output::send<LogLevel::Default>(STR("[CairnMap] Unreal initialized\n"));
        }

        auto on_update() -> void override
        {
            if (!m_unreal_ready)
            {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - m_last_tick < std::chrono::milliseconds(300))
            {
                return;
            }
            m_last_tick = now;
            tick();
        }

      private:
        auto log_once(const wchar_t* msg) -> void
        {
            if (m_log_budget > 0)
            {
                --m_log_budget;
                Output::send<LogLevel::Default>(STR("[CairnMap] {}\n"), msg);
            }
        }

        // Find the visible map body's root (screen-fixed canvas) + Canvas_MapBody
        // (pans/zooms) + Canvas_ForIcon_Mask.
        auto find_map(UObject*& out_root, UObject*& out_map_body_canvas, UObject*& out_mask_canvas) -> bool
        {
            std::vector<UObject*> bodies;
            UObjectGlobals::FindAllOf(STR("WBP_Map_Body_C"), bodies);
            UObject* best_root = nullptr;
            UObject* best_mask = nullptr;
            UObject* best_body_canvas = nullptr;
            int best_pins = 0;
            for (auto* body : bodies)
            {
                if (!body)
                {
                    continue;
                }
                Engine::ParamsIsVisible vis{};
                if (!Engine::call(body, L"IsVisible", vis) || !vis.ReturnValue)
                {
                    continue;
                }
                auto** tree = body->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (!tree || !*tree)
                {
                    continue;
                }
                auto** root = (*tree)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                if (!root || !*root)
                {
                    continue;
                }
                UObject* body_canvas = Engine::find_descendant(*root, L"Canvas_MapBody");
                UObject* mask =
                    body_canvas ? Engine::find_descendant(body_canvas, L"Canvas_ForIcon_Mask", 0) : nullptr;
                if (!mask)
                {
                    continue;
                }
                int pins = 0;
                const int32_t n = Engine::children_count(mask);
                for (int32_t i = 0; i < n; ++i)
                {
                    if (Engine::class_name(Engine::child_at(mask, i)) == L"WBP_Map_IconFTTower_C")
                    {
                        ++pins;
                    }
                }
                if (pins + 1 > best_pins)
                {
                    best_pins = pins + 1;
                    best_mask = mask;
                    best_body_canvas = body_canvas;
                    best_root = *root;
                }
            }
            out_root = best_root;
            out_map_body_canvas = best_body_canvas;
            out_mask_canvas = best_mask;
            return best_mask != nullptr;
        }

        auto read_pins(UObject* mask, const wchar_t* cls, std::vector<Project::Vec2>& out) -> void
        {
            out.clear();
            const int32_t n = Engine::children_count(mask);
            for (int32_t i = 0; i < n; ++i)
            {
                UObject* c = Engine::child_at(mask, i);
                if (Engine::class_name(c) == cls)
                {
                    double x{}, y{};
                    if (Engine::slot_position(c, x, y))
                    {
                        out.push_back({x, y});
                    }
                }
            }
        }

        auto ensure_layer_canvas(UObject* map_body_canvas, UObject* mask) -> bool
        {
            if (m_layer_canvas)
            {
                return true;
            }
            auto* canvas_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.CanvasPanel"));
            if (!canvas_class)
            {
                log_once(L"CanvasPanel class not found");
                return false;
            }
            FStaticConstructObjectParameters params{canvas_class, map_body_canvas};
            m_layer_canvas = UObjectGlobals::StaticConstructObject(params);
            if (!m_layer_canvas)
            {
                log_once(L"layer canvas construction failed");
                return false;
            }
            // Wrap our canvas in an InvalidationBox so Slate caches its ~7k dot
            // widgets: panning the map then only re-transforms the cached layer
            // instead of re-laying-out every dot per frame (fixes the pan freeze
            // reported by users). Defensive: fall back to a direct attach if the
            // box can't be created, so behaviour never regresses.
            UObject* attach = m_layer_canvas;
            if (auto* inv_class = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/UMG.InvalidationBox")))
            {
                FStaticConstructObjectParameters ip{inv_class, map_body_canvas};
                if (UObject* box = UObjectGlobals::StaticConstructObject(ip))
                {
                    Engine::ParamsSetContent sc{m_layer_canvas};
                    Engine::ParamsSetCanCache cc{true};
                    if (Engine::call(box, L"SetContent", sc))
                    {
                        Engine::call(box, L"SetCanCache", cc);
                        m_inv_box = box;
                        attach = box;
                        Output::send<LogLevel::Default>(STR("[CairnMap] invalidation box active\n"));
                    }
                }
            }
            // ⚠ SPEC 2.4: our icons live ONLY in our own canvas; the game's
            // Canvas_ForIcon_Mask children are rebuilt/iterated every open and
            // foreign widgets in there crash the second open (proven).
            Engine::ParamsAddChildToCanvas add{attach, nullptr};
            if (!Engine::call(map_body_canvas, L"AddChildToCanvas", add) || !add.ReturnValue)
            {
                log_once(L"AddChildToCanvas(map body) failed");
                m_layer_canvas = nullptr;
                m_inv_box = nullptr;
                return false;
            }
            m_layer_slot = add.ReturnValue;
            Engine::ParamsSetZOrder z{100};
            Engine::call(m_layer_slot, L"SetZOrder", z);
            std::fill(std::begin(m_mask_geom), std::end(m_mask_geom), 0);
            sync_layer_geometry(mask);   // proper Set* calls: they invalidate Slate
            Output::send<LogLevel::Default>(STR("[CairnMap] layer canvas created\n"));
            return true;
        }

        // Mirror the game's mask-canvas slot geometry (zoom/layout changes)
        // using the proper slot setters so Slate invalidates immediately.
        auto sync_layer_geometry(UObject* mask) -> void
        {
            if (!m_layer_slot)
            {
                return;
            }
            auto** mask_slot = mask->GetValuePtrByPropertyNameInChain<UObject*>(STR("Slot"));
            auto* layout = (mask_slot && *mask_slot)
                               ? (*mask_slot)->GetValuePtrByPropertyNameInChain<uint8_t>(STR("LayoutData"))
                               : nullptr;
            if (!layout)
            {
                return;
            }
            if (std::memcmp(m_mask_geom, layout, sizeof(m_mask_geom)) == 0)
            {
                return;
            }
            std::memcpy(m_mask_geom, layout, sizeof(m_mask_geom));
            const auto* margins = reinterpret_cast<const float*>(layout);        // FMargin
            const auto* anchors = reinterpret_cast<const double*>(layout + 16);  // FAnchors + alignment
            Engine::ParamsSetOffsets offs{margins[0], margins[1], margins[2], margins[3]};
            Engine::call(m_layer_slot, L"SetOffsets", offs);
            Engine::ParamsSetAnchors anch{anchors[0], anchors[1], anchors[2], anchors[3]};
            Engine::call(m_layer_slot, L"SetAnchors", anch);
            Engine::ParamsSetAlignment align{{anchors[4], anchors[5]}};
            Engine::call(m_layer_slot, L"SetAlignment", align);
        }

        // Full asset package path of a layer's icon, resolved at runtime from the
        // player's own game files (approach B, nothing bundled). Item icons live
        // under InventoryItemIcon; map/POI markers under the InGame compass set.
        // nullptr = keep coloured dot (no clean icon exists).
        static auto layer_icon_path(int layer_id) -> const wchar_t*
        {
            switch (layer_id)
            {
            case kEffigyLayer:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Relic");
            case kNoteLayer:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Consume_TechnologyBook_G1");
            case kEggLayer:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_PalEgg");
            case 0:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_Coal");
            case 1:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_CopperOre");
            case 2:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_Quartz");
            case 3:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_Sulfur");
            case 4:   // Hexolite -> crystal
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_Pal_crystal_L");
            case 5:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_CrudeOil");
            case 6:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_SkyIslandOre");
            case 7:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_WorldTreeOre");
            case 8:   // Magma -> lava
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_Lava_Ancient");
            case 9:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_NightStone");
            case 10:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Material_Money");
            case 11:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Food_Lotus_attack_01");
            case 12:   // Chest -> treasure map marker
                return STR("/Game/Pal/Texture/UI/InGame/T_icon_compass_Search_Treasure");
            case 13:   // Junk -> junk marker
                return STR("/Game/Pal/Texture/UI/InGame/T_icon_compass_Search_Junk");
            case 14:   // Outpost -> enemy camp marker
                return STR("/Game/Pal/Texture/UI/InGame/T_icon_compass_EnemyCamp");
            case 15:
                return STR("/Game/Others/InventoryItemIcon/Texture/T_itemicon_Consume_AffectionFruit_01");
            default:
                return nullptr;   // Hexolite / Magma -> coloured dot
            }
        }

        // Isolation switch: when false, no runtime icon loading / SetBrushFromTexture
        // happens (dots stay coloured). Used to confirm the heap-corruption source.
        static constexpr bool g_load_game_icons = false;

        // Load each layer's icon once from the player's install, cached.
        auto ensure_layer_icons() -> void
        {
            if (!g_load_game_icons)
            {
                return;
            }
            for (const auto& li : panel_items())
            {
                if (li.kind != PanelItem::Row || m_layer_icon.count(li.id))
                {
                    continue;
                }
                const wchar_t* path = layer_icon_path(li.id);
                if (!path)
                {
                    m_layer_icon[li.id] = nullptr;   // dot fallback, don't retry
                    continue;
                }
                std::wstring full = path;
                std::wstring name = full.substr(full.find_last_of(L'/') + 1);
                m_layer_icon[li.id] = Engine::load_game_texture(full.c_str(), name.c_str());
            }
        }

        auto layer_texture_for(int layer_id) -> UObject*
        {
            auto it = m_layer_icon.find(layer_id);
            return it != m_layer_icon.end() ? it->second : nullptr;
        }

        // one dot: pooled construction, canvas attach, styling. Returns index or SIZE_MAX.
        auto emit_dot(UClass* image_class, double px, double py, const Engine::FLinearColor_& color,
                      const wchar_t* icon, double base_size, bool visible, int layer_id) -> size_t
        {
            UObject* dot = nullptr;
            if (m_emit_cursor < m_dots.size())
            {
                dot = m_dots[m_emit_cursor].widget;
            }
            else
            {
                FStaticConstructObjectParameters params{image_class, m_layer_canvas};
                dot = UObjectGlobals::StaticConstructObject(params);
                if (!dot)
                {
                    return SIZE_MAX;
                }
                Style::make_round(dot);
                m_dots.push_back({dot, nullptr, nullptr, false, base_size, layer_id, false});
            }
            Dot& entry = m_dots[m_emit_cursor];
            entry.icon = g_icons_enabled ? icon : nullptr;
            entry.base_size = base_size;
            entry.layer_id = layer_id;
            entry.base_hidden = !visible;

            // Real game icon for this layer (loaded from the player's own install).
            // When present, paint it as the brush and show it untinted; otherwise the
            // RoundedBox stays and gets the category tint below.
            if (UObject* tex = layer_texture_for(layer_id))
            {
                Engine::ParamsSetBrushFromTexture brush{tex, false};
                Engine::call(dot, L"SetBrushFromTexture", brush);
                Style::draw_as_image(dot);   // full rectangle, no RoundedBox clipping
                entry.icon_applied = true;
            }

            Engine::ParamsAddChildToCanvas add{dot, nullptr};
            if (!Engine::call(m_layer_canvas, L"AddChildToCanvas", add) || !add.ReturnValue)
            {
                return SIZE_MAX;
            }
            entry.slot = add.ReturnValue;
            const Engine::FLinearColor_ col_val = entry.icon_applied
                                                     ? Engine::FLinearColor_{1.0f, 1.0f, 1.0f, 1.0f}
                                                     : color;
            Engine::ParamsSetColorAndOpacity col{col_val};
            Engine::call(dot, L"SetColorAndOpacity", col);
            Engine::ParamsSetVisibility vis{visible ? Engine::Vis_HitTestInvisible : Engine::Vis_Collapsed};
            Engine::call(dot, L"SetVisibility", vis);
            Engine::ParamsSetAutoSize aut{false};
            Engine::call(entry.slot, L"SetAutoSize", aut);
            Engine::ParamsSetAlignment align{{0.5, 0.5}};
            Engine::call(entry.slot, L"SetAlignment", align);
            const double sz = std::clamp(entry.base_size / m_applied_zoom, 6.0, 40.0);
            Engine::ParamsSetSize size{{sz, sz}};
            Engine::call(entry.slot, L"SetSize", size);
            Engine::ParamsSetPosition setpos{{px, py}};
            Engine::call(entry.slot, L"SetPosition", setpos);
            return m_emit_cursor++;
        }

        // Enumerate PalEgg loot actors currently streamed in and place a dot per
        // egg (projected from its world location). Returns the count placed.
        auto place_live_eggs(UClass* image_class) -> size_t
        {
            if (!m_calibration)
            {
                return 0;
            }
            std::vector<UObject*> eggs;
            UObjectGlobals::FindAllOf(STR("PalMapObjectPalEgg"), eggs);
            size_t shown = 0;
            for (auto* egg : eggs)
            {
                double ex = 0, ey = 0, ez = 0;
                if (!egg || !Engine::actor_location(egg, ex, ey, ez) || (ex == 0 && ey == 0))
                {
                    continue;
                }
                const auto pos = m_calibration->transform.apply(ex, ey);
                if (pos.x != pos.x || pos.x < -2000 || pos.x > 6000 || pos.y < -2000 || pos.y > 6000)
                {
                    continue;
                }
                if (emit_dot(image_class, pos.x, pos.y, {1.0f, 0.82f, 0.15f, 1.0f}, nullptr, 18.0, true,
                             kEggLayer) != SIZE_MAX)
                {
                    ++shown;
                }
            }
            return shown;
        }

        size_t m_emit_cursor = 0;
        static constexpr bool g_icons_enabled = false;   // item icons unreachable from C++ (Lua object-space barrier); colored dots

        std::unordered_map<std::wstring, UObject*> m_tex_index;
        size_t m_tex_index_size = 0;

        // Build a short-name -> loaded-texture index from all resident Texture2D
        // objects (FindObject-by-path is unreliable here; the preloader keeps the
        // textures resident, so enumeration is the robust way to reach them).
        auto rebuild_texture_index() -> void
        {
            std::vector<UObject*> texs;
            UObjectGlobals::FindAllOf(STR("Texture2D"), texs);
            m_tex_index.clear();
            for (auto* t : texs)
            {
                if (t)
                {
                    m_tex_index[t->GetName()] = t;
                }
            }
            m_tex_index_size = texs.size();
        }

        auto layer_texture(const wchar_t* icon) -> UObject*
        {
            if (!icon)
            {
                return nullptr;
            }
            auto it = m_tex_index.find(icon);
            if (it != m_tex_index.end() && it->second)
            {
                return it->second;
            }
            return nullptr;
        }

        auto place_dots() -> void
        {
            auto* image_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            if (!image_class || !m_calibration)
            {
                return;
            }
            ensure_layer_icons();   // load real item icons from the player's install
            m_emit_cursor = 0;
            size_t placed = 0;
            const auto t0 = std::chrono::steady_clock::now();
            int layer_index = 0;
            for (const auto& layer : Data::kLayers)
            {
                const int this_layer = layer_index++;
                if (!layer.default_on)
                {
                    continue;
                }
                const Engine::FLinearColor_ color{layer.r / 255.0f, layer.g / 255.0f, layer.b / 255.0f,
                                                  layer.a / 255.0f};
                for (size_t i = 0; i < layer.count; ++i)
                {
                    const auto pos = m_calibration->transform.apply(layer.points[i].x, layer.points[i].y);
                    if (pos.x != pos.x || pos.x < -2000 || pos.x > 6000 || pos.y < -2000 || pos.y > 6000)
                    {
                        continue;
                    }
                    if (emit_dot(image_class, pos.x, pos.y, color, layer.icon, 14.0, true, this_layer) !=
                        SIZE_MAX)
                    {
                        ++placed;
                    }
                }
            }
            // effigies & notes, filtered by the live collected set
            std::unordered_set<std::wstring> collected;
            const bool have_flags = Collected::gather(collected);
            // one-shot diagnostic: reveals the obtain-flag key format vs our GUIDs
            if (!m_flag_probe_done)
            {
                m_flag_probe_done = true;
                std::wstring samples;
                int n = 0;
                for (const auto& k : collected)
                {
                    samples += k;
                    samples += L' ';
                    if (++n >= 4)
                    {
                        break;
                    }
                }
                Output::send<LogLevel::Default>(
                    STR("[CairnFlag] gathered {} obtained keys; samples: {}\n"), collected.size(),
                    samples);
                Output::send<LogLevel::Default>(
                    STR("[CairnFlag] our eff[0]={} note[0]={}\n"),
                    Collected::guid_key(Data::kEffigies[0].guid), std::wstring(Data::kNotes[0].row));
            }
            // one-shot probe: can we load a game icon from the player's own install
            // via LoadAsset_Blocking (approach B, nothing bundled)? Validates the
            // TSoftObjectPtr layout before wiring icons to every dot.
            if (!m_icon_probe_done)
            {
                m_icon_probe_done = true;
                UObject* tex = Engine::load_game_texture(
                    STR("/Game/Others/InventoryItemIcon/Texture/T_icon_item_BossDefeatReward_Anubis"),
                    STR("T_icon_item_BossDefeatReward_Anubis"));
                Output::send<LogLevel::Default>(STR("[CairnIcon] LoadAsset_Blocking -> {} (class {})\n"),
                                                tex ? tex->GetName() : std::wstring(L"NULL"),
                                                tex ? Engine::class_name(tex) : std::wstring(L"-"));
            }
            size_t hidden = 0;
            // Generic collectable placement: key_fn(i) yields the obtained-set key
            // (instance GUID for effigies, NoteRowName for notes).
            auto place_collectables = [&](size_t count, auto coord_fn, auto key_fn,
                                          const Engine::FLinearColor_& color, const wchar_t* icon,
                                          double base_size, int layer_id) -> size_t {
                size_t layer_hidden = 0;
                for (size_t i = 0; i < count; ++i)
                {
                    std::wstring key = key_fn(i);
                    const bool is_collected = have_flags && collected.contains(key);
                    if (is_collected)
                    {
                        ++hidden;
                        ++layer_hidden;
                    }
                    const auto xy = coord_fn(i);
                    const auto pos = m_calibration->transform.apply(xy.first, xy.second);
                    if (pos.x < -2000 || pos.x > 6000 || pos.y < -2000 || pos.y > 6000)
                    {
                        continue;
                    }
                    const size_t idx =
                        emit_dot(image_class, pos.x, pos.y, color, icon, base_size, !is_collected, layer_id);
                    if (idx != SIZE_MAX)
                    {
                        m_guid_dots.push_back({idx, std::move(key)});
                        ++placed;
                    }
                }
                return layer_hidden;
            };
            const size_t eff_hidden = place_collectables(
                std::size(Data::kEffigies),
                [](size_t i) { return std::pair<int, int>{Data::kEffigies[i].x, Data::kEffigies[i].y}; },
                [](size_t i) { return Collected::guid_key(Data::kEffigies[i].guid); },
                {0.35f, 1.0f, 0.20f, 1.0f}, Data::kEffigyIcon, 20.0, kEffigyLayer);
            const size_t note_hidden = place_collectables(
                std::size(Data::kNotes),
                [](size_t i) { return std::pair<int, int>{Data::kNotes[i].x, Data::kNotes[i].y}; },
                [](size_t i) { return std::wstring(Data::kNotes[i].row); },
                {0.20f, 0.88f, 1.0f, 1.0f}, Data::kNoteIcon, 20.0, kNoteLayer);
            Output::send<LogLevel::Default>(
                STR("[CairnFlag] hidden effigies={}/{} notes={}/{}\n"), eff_hidden,
                std::size(Data::kEffigies), note_hidden, std::size(Data::kNotes));
            // live eggs: no static positions (random lottery placement + respawn),
            // so enumerate the PalEgg loot actors currently loaded around the player
            // and place a dot at each. Only covers the streamed-in area near you; the
            // set refreshes whenever the map is reopened from a new location.
            const size_t egg_shown = place_live_eggs(image_class);
            placed += egg_shown;
            Output::send<LogLevel::Default>(STR("[CairnLoot] live eggs placed={}\n"), egg_shown);
            // collapse any leftover pooled dots beyond this pass
            for (size_t i = m_emit_cursor; i < m_dots.size(); ++i)
            {
                Engine::ParamsSetVisibility vis{Engine::Vis_Collapsed};
                Engine::call(m_dots[i].widget, L"SetVisibility", vis);
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
            Output::send<LogLevel::Default>(
                STR("[CairnMap] {} dots placed, {} collected hidden (pool {}) in {}ms\n"),
                placed, hidden, m_dots.size(), ms);
            m_placed = true;
            m_collapsed = false;
        }

        // Visibility = layer toggled on AND not collected. Cheap per-tick-safe
        // diff (only SetVisibility, never re-parent).
        auto apply_layer_visibility() -> void
        {
            for (const auto& d : m_dots)
            {
                if (!d.slot)
                {
                    continue;
                }
                const bool show = is_layer_on(d.layer_id) && !d.base_hidden;
                Engine::ParamsSetVisibility vis{show ? Engine::Vis_HitTestInvisible : Engine::Vis_Collapsed};
                Engine::call(d.widget, L"SetVisibility", vis);
            }
        }

        // ⚠ never re-parent pooled widgets on refresh: 5k AddChild churn per
        // open crashed both the Lua prototype and the first P1.5 build.
        auto refresh_collected() -> void
        {
            if (m_guid_dots.empty())
            {
                return;
            }
            std::unordered_set<std::wstring> collected;
            if (!Collected::gather(collected))
            {
                return;
            }
            size_t hidden = 0;
            for (const auto& gd : m_guid_dots)
            {
                const bool is_collected = collected.contains(gd.key);
                hidden += is_collected ? 1 : 0;
                m_dots[gd.dot_index].base_hidden = is_collected;
            }
            apply_layer_visibility();
            Output::send<LogLevel::Default>(STR("[CairnMap] collected refresh: {} hidden\n"), hidden);
        }

        // retry lazy icon textures (game loads them as the player encounters items)
        // Paint at most `budget` pending icon textures per call (non-blocking).
        // Runs across ticks so placement stays instant and icons pop in smoothly.
        size_t m_icon_scan = 0;
        bool m_icons_ready = false;
        int m_icon_diag_ticks = 0;
        int m_icon_rebuilds = 0;
        auto paint_icons_batch(size_t budget) -> void
        {
            if (!g_icons_enabled || m_dots.empty())
            {
                return;
            }
            // rebuild the texture index until all layer icons resolve: the
            // preloader may make them resident after the first build (stale
            // index otherwise never picks them up).
            if (!m_icons_ready && m_icon_rebuilds < 8)
            {
                ++m_icon_rebuilds;
                rebuild_texture_index();
                bool all = true;
                int resolved = 0, want = 0;
                for (const auto& l : Data::kLayers)
                {
                    if (l.icon)
                    {
                        ++want;
                        if (layer_texture(l.icon))
                        {
                            ++resolved;
                        }
                        else
                        {
                            all = false;
                        }
                    }
                }
                if (m_icon_diag_ticks < 3)
                {
                    ++m_icon_diag_ticks;
                    Output::send<LogLevel::Default>(
                        STR("[CairnMap] icon index: {} textures, {}/{} layer icons resolved\n"),
                        m_tex_index_size, resolved, want);
                }
                if (all && (layer_texture(Data::kEffigyIcon) && layer_texture(Data::kNoteIcon)))
                {
                    m_icons_ready = true;
                    Output::send<LogLevel::Default>(STR("[CairnMap] all icon textures indexed\n"));
                }
            }
            size_t painted = 0, scanned = 0;
            const size_t n = m_dots.size();
            while (scanned < n && painted < budget)
            {
                Dot& d = m_dots[m_icon_scan % n];
                ++m_icon_scan;
                ++scanned;
                if (!d.icon || d.icon_applied || !d.widget)
                {
                    continue;
                }
                auto* tex = layer_texture(d.icon);
                if (!tex)
                {
                    continue;
                }
                Engine::ParamsSetBrushFromTexture brush{tex, false};
                Engine::call(d.widget, L"SetBrushFromTexture", brush);
                Style::make_image(d.widget);   // full icon, no circular crop
                Engine::ParamsSetColorAndOpacity col{{1.0f, 1.0f, 1.0f, 1.0f}};
                Engine::call(d.widget, L"SetColorAndOpacity", col);
                d.icon_applied = true;
                ++painted;
            }
        }

        // cumulative render scale up the widget chain = current map zoom
        auto current_zoom(UObject* mask) -> double
        {
            double zoom = 1.0;
            UObject* w = mask;
            for (int i = 0; i < 6 && w; ++i)
            {
                if (auto* xf = w->GetValuePtrByPropertyNameInChain<double>(STR("RenderTransform")))
                {
                    const double sx = xf[2];   // Translation(2d), then Scale.X
                    if (sx > 0.01 && sx < 100.0)
                    {
                        zoom *= sx;
                    }
                }
                struct
                {
                    UObject* ReturnValue{};
                } parent{};
                if (!Engine::call(w, L"GetParent", parent))
                {
                    break;
                }
                w = parent.ReturnValue;
            }
            return zoom;
        }

        auto sync_dot_scale(UObject* mask) -> void
        {
            const double zoom = current_zoom(mask);
            if (zoom <= 0.0 || std::abs(zoom - m_applied_zoom) / m_applied_zoom < 0.05)
            {
                return;
            }
            m_applied_zoom = zoom;
            for (const auto& d : m_dots)
            {
                if (!d.slot)
                {
                    continue;
                }
                const double sz = std::clamp(d.base_size / zoom, 6.0, 40.0);
                Engine::ParamsSetSize size{{sz, sz}};
                Engine::call(d.slot, L"SetSize", size);
            }
        }

        // Find a full-screen canvas to host the panel (the map body root is
        // inset by the decorative frame; WBP_Map_Base's root spans the screen).
        auto screen_canvas(UObject* fallback) -> UObject*
        {
            std::vector<UObject*> bases;
            UObjectGlobals::FindAllOf(STR("WBP_Map_Base_C"), bases);
            for (auto* base : bases)
            {
                if (!base)
                {
                    continue;
                }
                Engine::ParamsIsVisible vis{};
                if (!Engine::call(base, L"IsVisible", vis) || !vis.ReturnValue)
                {
                    continue;
                }
                auto** tree = base->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (!tree || !*tree)
                {
                    continue;
                }
                auto** r = (*tree)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                if (r && *r && Engine::class_name(*r) == L"CanvasPanel")
                {
                    return *r;
                }
            }
            return fallback;
        }

        // Build the interface panel once per map instance, in a screen-fixed
        // canvas (not the panning map canvas). Rows: [checkbox][dot][label].
        auto build_panel(UObject* root_in) -> void
        {
            if (m_panel_canvas || !root_in)
            {
                return;
            }
            UObject* root = screen_canvas(root_in);
            auto* canvas_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.CanvasPanel"));
            auto* image_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* cb_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.CheckBox"));
            auto* txt_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!canvas_class || !image_class || !cb_class || !txt_class)
            {
                return;
            }
            // panel container
            {
                FStaticConstructObjectParameters params{canvas_class, root};
                m_panel_canvas = UObjectGlobals::StaticConstructObject(params);
            }
            if (!m_panel_canvas)
            {
                return;
            }
            Engine::ParamsAddChildToCanvas add{m_panel_canvas, nullptr};
            if (!Engine::call(root, L"AddChildToCanvas", add) || !add.ReturnValue)
            {
                m_panel_canvas = nullptr;
                return;
            }
            // top-left, size derived from the categorized item list
            const auto items = panel_items();
            auto item_h = [](const PanelItem& it) -> double {
                switch (it.kind)
                {
                case PanelItem::Title:
                    return 28.0;
                case PanelItem::Header:
                    return 22.0;   // includes top gap before the header
                default:
                    return 19.0;
                }
            };
            const double width = 190.0;
            double content_h = 6.0;   // top padding
            for (const auto& it : items)
            {
                content_h += item_h(it);
            }
            const double height = content_h + 8.0;   // bottom padding
            {
                Engine::ParamsSetAnchors anch{0, 0, 0, 0};
                Engine::call(add.ReturnValue, L"SetAnchors", anch);
                Engine::ParamsSetAlignment align{{0.0, 0.0}};
                Engine::call(add.ReturnValue, L"SetAlignment", align);
                Engine::ParamsSetOffsets offs{12.0f, 12.0f, static_cast<float>(width),
                                              static_cast<float>(height)};
                Engine::call(add.ReturnValue, L"SetOffsets", offs);
            }
            Output::send<LogLevel::Default>(STR("[CairnMap] panel parent: {}\n"),
                                            (root == root_in) ? STR("map-body-root") : STR("map-base-screen"));
            // background
            auto add_to_panel = [&](UObject* w, double x, double y, double w_, double h_) -> UObject* {
                Engine::ParamsAddChildToCanvas a{w, nullptr};
                if (!Engine::call(m_panel_canvas, L"AddChildToCanvas", a) || !a.ReturnValue)
                {
                    return nullptr;
                }
                Engine::ParamsSetAlignment al{{0.0, 0.0}};
                Engine::call(a.ReturnValue, L"SetAlignment", al);
                Engine::ParamsSetAutoSize aut{false};
                Engine::call(a.ReturnValue, L"SetAutoSize", aut);
                Engine::ParamsSetSize sz{{w_, h_}};
                Engine::call(a.ReturnValue, L"SetSize", sz);
                Engine::ParamsSetPosition p{{x, y}};
                Engine::call(a.ReturnValue, L"SetPosition", p);
                return a.ReturnValue;
            };
            {
                FStaticConstructObjectParameters params{image_class, m_panel_canvas};
                UObject* bg = UObjectGlobals::StaticConstructObject(params);
                if (bg)
                {
                    Style::make_round(bg);
                    Engine::ParamsSetColorAndOpacity c{{0.02f, 0.02f, 0.04f, 0.78f}};
                    Engine::call(bg, L"SetColorAndOpacity", c);
                    Engine::ParamsSetVisibility v{Engine::Vis_HitTestInvisible};
                    Engine::call(bg, L"SetVisibility", v);
                    add_to_panel(bg, 0, 0, width, height);
                }
            }
            // a thin label helper (title / category header), no checkbox
            auto add_label = [&](const wchar_t* text, double x, double y, double w_, int font,
                                 float r, float g, float b, float a) {
                FStaticConstructObjectParameters tp{txt_class, m_panel_canvas};
                UObject* txt = UObjectGlobals::StaticConstructObject(tp);
                if (!txt)
                {
                    return;
                }
                Engine::ParamsSetText st{FText(text)};
                Engine::call(txt, L"SetText", st);
                Style::set_font_size(txt, font);
                Engine::ParamsSetColorAndOpacity tc{{r, g, b, a}};
                Engine::call(txt, L"SetColorAndOpacity", tc);
                Engine::ParamsSetVisibility v{Engine::Vis_HitTestInvisible};
                Engine::call(txt, L"SetVisibility", v);
                add_to_panel(txt, x, y, w_, 16);
            };
            // a 1px separator line
            auto add_rule = [&](double x, double y, double w_) {
                FStaticConstructObjectParameters params{image_class, m_panel_canvas};
                UObject* line = UObjectGlobals::StaticConstructObject(params);
                if (!line)
                {
                    return;
                }
                Style::make_round(line);
                Engine::ParamsSetColorAndOpacity c{{1.0f, 1.0f, 1.0f, 0.12f}};
                Engine::call(line, L"SetColorAndOpacity", c);
                Engine::ParamsSetVisibility v{Engine::Vis_HitTestInvisible};
                Engine::call(line, L"SetVisibility", v);
                add_to_panel(line, x, y, w_, 1.0);
            };

            // a small layer icon (loaded game texture) for the legend, left of label
            auto add_icon = [&](UObject* tex, double x, double y, double sz) {
                if (!tex)
                {
                    return;
                }
                FStaticConstructObjectParameters params{image_class, m_panel_canvas};
                UObject* img = UObjectGlobals::StaticConstructObject(params);
                if (!img)
                {
                    return;
                }
                Engine::ParamsSetBrushFromTexture brush{tex, false};
                Engine::call(img, L"SetBrushFromTexture", brush);
                Style::draw_as_image(img);
                Engine::ParamsSetColorAndOpacity c{{1.0f, 1.0f, 1.0f, 1.0f}};
                Engine::call(img, L"SetColorAndOpacity", c);
                Engine::ParamsSetVisibility v{Engine::Vis_HitTestInvisible};
                Engine::call(img, L"SetVisibility", v);
                add_to_panel(img, x, y, sz, sz);
            };

            ensure_layer_icons();   // legend uses the same loaded game textures

            // collected counters for effigies / notes (X / total)
            std::unordered_set<std::wstring> coll;
            const bool have_coll = Collected::gather(coll);
            size_t eff_got = 0, note_got = 0;
            if (have_coll)
            {
                for (const auto& e : Data::kEffigies)
                {
                    eff_got += coll.count(Collected::guid_key(e.guid)) ? 1 : 0;
                }
                for (const auto& n : Data::kNotes)
                {
                    note_got += coll.count(std::wstring(n.row)) ? 1 : 0;
                }
            }

            m_panel_rows.clear();
            double y = 6.0;
            for (const auto& it : items)
            {
                if (it.kind == PanelItem::Title)
                {
                    add_label(it.label, 12, y + 4, width - 20, 15, 0.96f, 0.97f, 1.0f, 1.0f);
                    add_rule(10, y + 25, width - 20);
                    y += item_h(it);
                    continue;
                }
                if (it.kind == PanelItem::Header)
                {
                    add_label(it.label, 10, y + 8, width - 16, 9, 0.55f, 0.60f, 0.72f, 1.0f);
                    y += item_h(it);
                    continue;
                }
                // toggle row: [checkbox][icon][label]. Neutral checkbox (the icon
                // now identifies the layer), real game icon as the legend glyph.
                FStaticConstructObjectParameters cbp{cb_class, m_panel_canvas};
                UObject* cb = UObjectGlobals::StaticConstructObject(cbp);
                if (cb)
                {
                    Style::make_checkbox(cb, 0.82f, 0.86f, 0.95f);
                    Engine::ParamsSetVisibility v{Engine::Vis_Visible};
                    Engine::call(cb, L"SetVisibility", v);
                    add_to_panel(cb, 12, y + 2, 14, 14);
                    if (auto* st = cb->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CheckedState")))
                    {
                        *st = is_layer_on(it.id) ? 1 : 0;   // ECheckBoxState::Checked
                    }
                    Engine::ParamsSetIsChecked chk{is_layer_on(it.id)};
                    Engine::call(cb, L"SetIsChecked", chk);
                    m_panel_rows.push_back({cb, it.id, is_layer_on(it.id)});
                }
                add_icon(layer_texture_for(it.id), 30, y + 1, 16);
                std::wstring lbl = it.label;
                if (have_coll && it.id == kEffigyLayer)
                {
                    lbl += L"  " + std::to_wstring(eff_got) + L"/" +
                           std::to_wstring(std::size(Data::kEffigies));
                }
                else if (have_coll && it.id == kNoteLayer)
                {
                    lbl += L"  " + std::to_wstring(note_got) + L"/" +
                           std::to_wstring(std::size(Data::kNotes));
                }
                add_label(lbl.c_str(), 50, y + 2, width - 58, 11, 0.90f, 0.92f, 0.98f, 1.0f);
                y += item_h(it);
            }
            m_panel_root_name = root->GetFullName();
            Output::send<LogLevel::Default>(STR("[CairnMap] panel built ({} rows)\n"), m_panel_rows.size());
        }

        // Force a checkbox to a given checked state (visual + property).
        auto set_checkbox(UObject* cb, bool on) -> void
        {
            if (!cb)
            {
                return;
            }
            if (auto* st = cb->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CheckedState")))
            {
                *st = on ? 1 : 0;
            }
            Engine::ParamsSetIsChecked chk{on};
            Engine::call(cb, L"SetIsChecked", chk);
        }

        // Poll checkbox states; on change, update toggle + layer visibility + save.
        auto poll_panel() -> void
        {
            bool changed = false;
            for (auto& row : m_panel_rows)
            {
                if (!row.checkbox)
                {
                    continue;
                }
                Engine::ParamsIsChecked p{};
                if (!Engine::call(row.checkbox, L"IsChecked", p))
                {
                    continue;
                }
                if (m_panel_first_poll)
                {
                    row.last_checked = p.ReturnValue;
                    continue;
                }
                if (p.ReturnValue != row.last_checked)
                {
                    row.last_checked = p.ReturnValue;
                    m_layer_on[row.layer_id] = p.ReturnValue;
                    changed = true;
                }
            }
            if (m_panel_first_poll)
            {
                m_panel_first_poll = false;
            }
            if (changed)
            {
                apply_layer_visibility();
                save_toggles();
            }
        }

        auto tick() -> void
        {
            UObject* root = nullptr;
            UObject* map_body_canvas = nullptr;
            UObject* mask = nullptr;
            if (!find_map(root, map_body_canvas, mask))
            {
                // map closed: collapse our overlay once (widgets stay pooled)
                if (m_layer_canvas && !m_collapsed)
                {
                    Engine::ParamsSetVisibility vis{Engine::Vis_Collapsed};
                    Engine::call(m_layer_canvas, L"SetVisibility", vis);
                    m_collapsed = true;
                }
                return;
            }

            // ⚠ UE4SS object identity is only stable via full names (SPEC 2.3)
            const std::wstring full_name = mask->GetFullName();
            if (full_name != m_canvas_full_name)
            {
                // new map body instance: old widgets died with the previous tree
                m_canvas_full_name = full_name;
                m_layer_canvas = nullptr;
                m_inv_box = nullptr;
                m_layer_icon.clear();
                m_layer_slot = nullptr;
                m_dots.clear();
                m_guid_dots.clear();
                m_emit_cursor = 0;
                m_applied_zoom = 1.0;
                m_icon_scan = 0;
                m_tex_index.clear();
                m_icons_ready = false;
                m_icon_rebuilds = 0;
                m_calibration.reset();
                m_placed = false;
                m_collapsed = true;
                m_panel_canvas = nullptr;   // died with the tree
                m_panel_rows.clear();
                m_panel_first_poll = true;
            }

            if (!m_calibration)
            {
                std::vector<Project::Vec2> boss_pins, statue_pins;
                read_pins(mask, L"WBP_Map_IconTower_C", boss_pins);
                read_pins(mask, L"WBP_Map_IconFTTower_C", statue_pins);
                if (boss_pins.size() < 4 || statue_pins.size() < 100)
                {
                    return;   // pins still populating: retry next tick (debounce)
                }
                std::vector<Project::Vec2> boss_world, statue_world;
                for (const auto& p : Data::kBossTowers)
                {
                    boss_world.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
                }
                for (const auto& p : Data::kStatues)
                {
                    statue_world.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
                }
                m_calibration = Project::calibrate(boss_world, boss_pins, statue_world, statue_pins);
                if (!m_calibration)
                {
                    log_once(L"calibration failed");
                    return;
                }
                Output::send<LogLevel::Default>(
                    STR("[CairnMap] calibrated: seed {:.1f}px refine {:.2f}px ({} anchors)\n"),
                    m_calibration->seed_residual_px, m_calibration->refine_residual_px,
                    m_calibration->matched_statues);
            }

            if (!ensure_layer_canvas(map_body_canvas, mask))
            {
                return;
            }
            sync_layer_geometry(mask);   // follow zoom / layout changes
            if (m_placed && !m_collapsed)
            {
                sync_dot_scale(mask);        // keep dots readable across zoom levels
                paint_icons_batch(400);      // stream icon textures in, non-blocking
            }
            if (!m_placed)
            {
                place_dots();
                apply_layer_visibility();   // honor toggles from the start
            }
            else if (m_collapsed)
            {
                Engine::ParamsSetVisibility vis{Engine::Vis_SelfHitTestInvisible};
                Engine::call(m_layer_canvas, L"SetVisibility", vis);
                m_collapsed = false;
                refresh_collected();   // visibility-only diff, no re-parenting
            }

            // interface panel: build once, then poll toggles each tick
            build_panel(root);
            poll_panel();
        }
    };
} // namespace CairnMap

#define MOD_EXPORT __declspec(dllexport)
extern "C"
{
    MOD_EXPORT RC::CppUserModBase* start_mod()
    {
        return new CairnMap::Mod();
    }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
