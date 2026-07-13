// CairnMap — map collectables for Palworld 1.0+ (see docs/SPEC-2.0.md)
// P1: static layers rendered as tinted dots in our own canvas, positioned by
// the boss-tower-anchored projection (cairn_project.hpp, unit-tested natively).
// ABI target: UE4SS v3.0.1 (Okaetsu experimental-palworld, c2ac246, MSVC).

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

#include "cairn_data.hpp"
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


    // ------------------------------------------------------- P1.5 probe (temp)
    // One-shot dump: location points + record-data obtain flags, to ground the
    // effigy/note collected-state design (docs/P1.5-effigies.md).
    namespace Probe
    {
        inline auto dump_guid(const uint32_t* g) -> std::wstring
        {
            wchar_t buf[40];
            swprintf(buf, 40, L"%08X-%08X-%08X-%08X", g[0], g[1], g[2], g[3]);
            return buf;
        }

        inline auto run() -> void
        {
            using namespace Engine;
            // --- location points
            std::vector<UObject*> pts;
            UObjectGlobals::FindAllOf(STR("PalLocationPointStatic"), pts);
            Output::send<LogLevel::Default>(STR("[CairnProbe] PalLocationPointStatic: {}\n"), pts.size());
            int shown = 0;
            for (auto* lp : pts)
            {
                if (!lp || shown >= 6)
                {
                    break;
                }
                auto* loc = lp->GetValuePtrByPropertyNameInChain<double>(STR("Location"));
                auto* id = lp->GetValuePtrByPropertyNameInChain<uint32_t>(STR("ID"));
                struct
                {
                    uint8_t ReturnValue{};
                } type_params;
                call(lp, L"GetType", type_params);
                if (loc && id)
                {
                    Output::send<LogLevel::Default>(STR("[CairnProbe] pt type={} pos=({:.0f},{:.0f},{:.0f}) id={}\n"),
                                                    type_params.ReturnValue, loc[0], loc[1], loc[2], dump_guid(id));
                    ++shown;
                }
            }
            // histogram of types
            int histo[64] = {};
            for (auto* lp : pts)
            {
                struct
                {
                    uint8_t ReturnValue{};
                } tp;
                if (lp && call(lp, L"GetType", tp) && tp.ReturnValue < 64)
                {
                    ++histo[tp.ReturnValue];
                }
            }
            for (int t = 0; t < 64; ++t)
            {
                if (histo[t] > 0)
                {
                    Output::send<LogLevel::Default>(STR("[CairnProbe] type {} x{}\n"), t, histo[t]);
                }
            }

            // --- record data obtain flags
            auto* util_cdo =
                UObjectGlobals::StaticFindObject(nullptr, nullptr, STR("/Script/Pal.Default__PalUtility"));
            auto* world_ctx = UObjectGlobals::FindFirstOf(STR("PalPlayerController"));
            if (!util_cdo || !world_ctx)
            {
                Output::send<LogLevel::Default>(STR("[CairnProbe] no util/ctx\n"));
                return;
            }
            struct
            {
                UObject* WorldContextObject{};
                UObject* ReturnValue{};
            } rec_params{world_ctx, nullptr};
            if (!Engine::call(util_cdo, L"GetLocalRecordData", rec_params) || !rec_params.ReturnValue)
            {
                Output::send<LogLevel::Default>(STR("[CairnProbe] GetLocalRecordData failed\n"));
                return;
            }
            UObject* record = rec_params.ReturnValue;

            // resolve Items offset + element layout via reflection, once
            auto* wrapper_struct = UObjectGlobals::StaticFindObject<UStruct*>(
                nullptr, nullptr, STR("/Script/Pal.PalPlayerRecordDataRepInfoArrayThreadSafe_BoolVal"));
            auto* item_struct = UObjectGlobals::StaticFindObject<UStruct*>(
                nullptr, nullptr, STR("/Script/Pal.PalPlayerRecordDataRepInfoThreadSafe_BoolVal"));
            if (!wrapper_struct || !item_struct)
            {
                Output::send<LogLevel::Default>(STR("[CairnProbe] flag structs not found\n"));
                return;
            }
            int32_t items_off = -1, key_off = -1, val_off = -1;
            for (FProperty* prop : wrapper_struct->ForEachProperty())
            {
                if (prop->GetName() == STR("Items"))
                {
                    items_off = prop->GetOffset_Internal();
                }
            }
            for (FProperty* prop : item_struct->ForEachProperty())
            {
                if (prop->GetName() == STR("Key"))
                {
                    key_off = prop->GetOffset_Internal();
                }
                if (prop->GetName() == STR("Value"))
                {
                    val_off = prop->GetOffset_Internal();
                }
            }
            const int32_t item_size = item_struct->GetStructureSize();
            Output::send<LogLevel::Default>(STR("[CairnProbe] offsets: items={} key={} value={} itemsize={}\n"),
                                            items_off, key_off, val_off, item_size);
            if (items_off < 0 || key_off < 0 || val_off < 0)
            {
                return;
            }
            const wchar_t* flag_names[] = {STR("RelicObtainForInstanceFlag_CapturePower"),
                                           STR("RelicObtainForInstanceFlag_HungerReduction"),
                                           STR("NoteObtainForInstanceFlag")};
            for (const auto* fname : flag_names)
            {
                auto* wrapper = record->GetValuePtrByPropertyNameInChain<uint8_t>(fname);
                if (!wrapper)
                {
                    Output::send<LogLevel::Default>(STR("[CairnProbe] {}: prop missing\n"), fname);
                    continue;
                }
                struct RawArray
                {
                    uint8_t* data;
                    int32_t num;
                    int32_t max;
                };
                const auto* arr = reinterpret_cast<const RawArray*>(wrapper + items_off);
                Output::send<LogLevel::Default>(STR("[CairnProbe] {}: {} entrees\n"), fname, arr->num);
                for (int32_t i = 0; i < arr->num && i < 4; ++i)
                {
                    const uint8_t* item = arr->data + static_cast<size_t>(i) * item_size;
                    const auto* key = reinterpret_cast<const FName*>(item + key_off);
                    const bool value = *(item + val_off) != 0;
                    Output::send<LogLevel::Default>(STR("[CairnProbe]   key={} val={}\n"), key->ToString(), value);
                }
            }
        }
    } // namespace Probe

    // ---------------------------------------------------------------- the mod
    class Mod final : public RC::CppUserModBase
    {
        bool m_unreal_ready = false;
        std::chrono::steady_clock::time_point m_last_tick{};

        // per-map-body state (SPEC 2.4): pool keyed by the live canvas
        std::wstring m_canvas_full_name;
        UObject* m_layer_canvas = nullptr;                 // our own CanvasPanel
        std::vector<UObject*> m_dots;                      // pooled UImage widgets
        std::optional<Project::Calibration> m_calibration;
        bool m_placed = false;
        bool m_collapsed = true;
        int m_log_budget = 20;

      public:
        Mod()
        {
            ModVersion = STR("2.0.0-p1");
            ModName = STR("CairnMap");
            ModAuthors = STR("Pixnop");
            ModDescription = STR("CairnMap: map collectables for Palworld 1.0+");
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

        // Find the visible map body's Canvas_MapBody + Canvas_ForIcon_Mask.
        auto find_map(UObject*& out_map_body_canvas, UObject*& out_mask_canvas) -> bool
        {
            std::vector<UObject*> bodies;
            UObjectGlobals::FindAllOf(STR("WBP_Map_Body_C"), bodies);
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
                }
            }
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
            // ⚠ SPEC 2.4: our icons live ONLY in our own canvas; the game's
            // Canvas_ForIcon_Mask children are rebuilt/iterated every open and
            // foreign widgets in there crash the second open (proven).
            Engine::ParamsAddChildToCanvas add{m_layer_canvas, nullptr};
            if (!Engine::call(map_body_canvas, L"AddChildToCanvas", add) || !add.ReturnValue)
            {
                log_once(L"AddChildToCanvas(map body) failed");
                m_layer_canvas = nullptr;
                return false;
            }
            // mirror the mask canvas geometry so our overlay aligns 1:1
            auto** mask_slot = mask->GetValuePtrByPropertyNameInChain<UObject*>(STR("Slot"));
            auto* mask_layout = (mask_slot && *mask_slot)
                                    ? (*mask_slot)->GetValuePtrByPropertyNameInChain<float>(STR("LayoutData"))
                                    : nullptr;
            auto* our_layout = add.ReturnValue->GetValuePtrByPropertyNameInChain<float>(STR("LayoutData"));
            if (mask_layout && our_layout)
            {
                for (int i = 0; i < 16; ++i)   // FMargin(4f)+FAnchors(4d)+FVector2D(2d) raw block
                {
                    our_layout[i] = mask_layout[i];
                }
            }
            Engine::ParamsSetZOrder z{100};
            Engine::call(add.ReturnValue, L"SetZOrder", z);
            Output::send<LogLevel::Default>(STR("[CairnMap] layer canvas created\n"));
            return true;
        }

        auto place_dots() -> void
        {
            auto* image_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            if (!image_class || !m_calibration)
            {
                return;
            }
            size_t dot_index = 0;
            size_t placed = 0;
            for (const auto& layer : Data::kLayers)
            {
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
                    UObject* dot = nullptr;
                    if (dot_index < m_dots.size())
                    {
                        dot = m_dots[dot_index];
                    }
                    else
                    {
                        FStaticConstructObjectParameters params{image_class, m_layer_canvas};
                        dot = UObjectGlobals::StaticConstructObject(params);
                        if (!dot)
                        {
                            continue;
                        }
                        m_dots.push_back(dot);
                    }
                    ++dot_index;

                    Engine::ParamsAddChildToCanvas add{dot, nullptr};
                    if (!Engine::call(m_layer_canvas, L"AddChildToCanvas", add) || !add.ReturnValue)
                    {
                        continue;
                    }
                    Engine::ParamsSetColorAndOpacity col{color};
                    Engine::call(dot, L"SetColorAndOpacity", col);
                    Engine::ParamsSetVisibility vis{Engine::Vis_HitTestInvisible};
                    Engine::call(dot, L"SetVisibility", vis);
                    Engine::ParamsSetAutoSize aut{false};
                    Engine::call(add.ReturnValue, L"SetAutoSize", aut);
                    Engine::ParamsSetAlignment align{{0.5, 0.5}};
                    Engine::call(add.ReturnValue, L"SetAlignment", align);
                    Engine::ParamsSetSize size{{10.0, 10.0}};
                    Engine::call(add.ReturnValue, L"SetSize", size);
                    Engine::ParamsSetPosition setpos{{pos.x, pos.y}};
                    Engine::call(add.ReturnValue, L"SetPosition", setpos);
                    ++placed;
                }
            }
            Output::send<LogLevel::Default>(STR("[CairnMap] {} dots placed (pool {})\n"), placed, m_dots.size());
            m_placed = true;
            m_collapsed = false;
        }

        auto tick() -> void
        {
            UObject* map_body_canvas = nullptr;
            UObject* mask = nullptr;
            if (!find_map(map_body_canvas, mask))
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
                m_dots.clear();
                m_calibration.reset();
                m_placed = false;
                m_collapsed = true;
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
                Probe::run();   // P1.5 temp: remove after grounding
            }

            if (!ensure_layer_canvas(map_body_canvas, mask))
            {
                return;
            }
            if (!m_placed)
            {
                place_dots();
            }
            else if (m_collapsed)
            {
                Engine::ParamsSetVisibility vis{Engine::Vis_SelfHitTestInvisible};
                Engine::call(m_layer_canvas, L"SetVisibility", vis);
                m_collapsed = false;
            }
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
