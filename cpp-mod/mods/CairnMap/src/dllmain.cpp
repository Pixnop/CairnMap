// CairnMap — map collectables for Palworld 1.0+ (see docs/SPEC-2.0.md)
// P1: static layers rendered as tinted dots in our own canvas, positioned by
// the boss-tower-anchored projection (cairn_project.hpp, unit-tested natively).
// ABI target: UE4SS v3.0.1 (Okaetsu experimental-palworld, c2ac246, MSVC).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
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


    // ----------------------------------------------------------- dot styling
    // Round dots without any texture: FSlateBrush.DrawAs = RoundedBox with a
    // fixed corner radius, written via reflection-resolved offsets.
    namespace Style
    {
        struct Offsets
        {
            int32_t draw_as = -1, outline = -1, radii = -1, rounding = -1;
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
            }
            off.resolved = off.draw_as >= 0 && off.outline >= 0 && off.radii >= 0 && off.rounding >= 0;
            return off.resolved;
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
        UObject* m_layer_slot = nullptr;                   // its canvas slot
        uint8_t m_mask_geom[64] = {};                      // last-seen mask LayoutData
        struct Dot
        {
            UObject* widget;
            UObject* slot;
            const wchar_t* icon;   // nullptr = plain dot
            bool icon_applied;
            double base_size;
        };
        std::vector<Dot> m_dots;                           // pooled dot widgets
        struct GuidDot
        {
            size_t dot_index;
            const Data::GuidPoint* pt;
        };
        std::vector<GuidDot> m_guid_dots;                  // effigy/note dots for refresh
        double m_applied_zoom = 1.0;
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

        // one dot: pooled construction, canvas attach, styling. Returns index or SIZE_MAX.
        auto emit_dot(UClass* image_class, double px, double py, const Engine::FLinearColor_& color,
                      const wchar_t* icon, double base_size, bool visible) -> size_t
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
                m_dots.push_back({dot, nullptr, nullptr, false, base_size});
            }
            Dot& entry = m_dots[m_emit_cursor];
            entry.icon = g_icons_enabled ? icon : nullptr;
            entry.base_size = base_size;

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
            const double sz = std::clamp(entry.base_size / std::sqrt(m_applied_zoom), 2.5, 26.0);
            Engine::ParamsSetSize size{{sz, sz}};
            Engine::call(entry.slot, L"SetSize", size);
            Engine::ParamsSetPosition setpos{{px, py}};
            Engine::call(entry.slot, L"SetPosition", setpos);
            return m_emit_cursor++;
        }

        size_t m_emit_cursor = 0;
        static constexpr bool g_icons_enabled = true;    // icons painted in background batches
        static inline UObject* const kAnyPackage = reinterpret_cast<UObject*>(static_cast<intptr_t>(-1));

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
            if (it != m_tex_index.end())
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
            m_emit_cursor = 0;
            size_t placed = 0;
            const auto t0 = std::chrono::steady_clock::now();
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
                    if (emit_dot(image_class, pos.x, pos.y, color, layer.icon, 14.0, true) != SIZE_MAX)
                    {
                        ++placed;
                    }
                }
            }
            // effigies & notes, filtered by the live collected set
            std::unordered_set<std::wstring> collected;
            const bool have_flags = Collected::gather(collected);
            size_t hidden = 0;
            auto place_guid_layer = [&](const Data::GuidPoint* pts, size_t count,
                                        const Engine::FLinearColor_& color, const wchar_t* icon,
                                        double base_size) {
                for (size_t i = 0; i < count; ++i)
                {
                    const bool is_collected =
                        have_flags && collected.contains(Collected::guid_key(pts[i].guid));
                    if (is_collected)
                    {
                        ++hidden;
                    }
                    const auto pos = m_calibration->transform.apply(pts[i].x, pts[i].y);
                    if (pos.x < -2000 || pos.x > 6000 || pos.y < -2000 || pos.y > 6000)
                    {
                        continue;
                    }
                    const size_t idx =
                        emit_dot(image_class, pos.x, pos.y, color, icon, base_size, !is_collected);
                    if (idx != SIZE_MAX)
                    {
                        m_guid_dots.push_back({idx, &pts[i]});
                        ++placed;
                    }
                }
            };
            place_guid_layer(Data::kEffigies, std::size(Data::kEffigies), {0.35f, 1.0f, 0.20f, 1.0f},
                             Data::kEffigyIcon, 20.0);
            place_guid_layer(Data::kNotes, std::size(Data::kNotes), {0.20f, 0.88f, 1.0f, 1.0f},
                             nullptr, 14.0);
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
                const bool is_collected = collected.contains(Collected::guid_key(gd.pt->guid));
                hidden += is_collected ? 1 : 0;
                Engine::ParamsSetVisibility vis{
                    is_collected ? Engine::Vis_Collapsed : Engine::Vis_HitTestInvisible};
                Engine::call(m_dots[gd.dot_index].widget, L"SetVisibility", vis);
            }
            Output::send<LogLevel::Default>(STR("[CairnMap] collected refresh: {} hidden\n"), hidden);
        }

        // retry lazy icon textures (game loads them as the player encounters items)
        // Paint at most `budget` pending icon textures per call (non-blocking).
        // Runs across ticks so placement stays instant and icons pop in smoothly.
        size_t m_icon_scan = 0;
        bool m_icon_diag_done = false;
        auto icon_diagnostic() -> void
        {
            if (m_icon_diag_done)
            {
                return;
            }
            m_icon_diag_done = true;
            rebuild_texture_index();
            Output::send<LogLevel::Default>(STR("[CairnDiag] {} Texture2D resident\n"), m_tex_index_size);
            for (const auto& layer : Data::kLayers)
            {
                if (!layer.icon)
                {
                    continue;
                }
                auto* tex = layer_texture(layer.icon);
                Output::send<LogLevel::Default>(STR("[CairnDiag] {}: tex={}\n"), layer.key,
                                                tex ? STR("FOUND") : STR("MISSING"));
            }
            // test SetBrushFromTexture wiring on the first icon dot
            for (auto& d : m_dots)
            {
                if (d.icon && d.widget)
                {
                    auto* tex = layer_texture(d.icon);
                    if (tex)
                    {
                        Engine::ParamsSetBrushFromTexture brush{tex, false};
                        const bool ok = Engine::call(d.widget, L"SetBrushFromTexture", brush);
                        Output::send<LogLevel::Default>(STR("[CairnDiag] SetBrushFromTexture call ok={}\n"),
                                                        ok);
                    }
                    break;
                }
            }
        }
        auto paint_icons_batch(size_t budget) -> void
        {
            if (!g_icons_enabled || m_dots.empty())
            {
                return;
            }
            icon_diagnostic();
            if (m_tex_index.empty())
            {
                rebuild_texture_index();
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
            if (zoom <= 0.0 || std::abs(zoom - m_applied_zoom) / m_applied_zoom < 0.15)
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
                const double sz = std::clamp(d.base_size / std::sqrt(zoom), 2.5, 26.0);
                Engine::ParamsSetSize size{{sz, sz}};
                Engine::call(d.slot, L"SetSize", size);
            }
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
                m_layer_slot = nullptr;
                m_dots.clear();
                m_guid_dots.clear();
                m_emit_cursor = 0;
                m_applied_zoom = 1.0;
                m_icon_scan = 0;
                m_icon_diag_done = false;
                m_tex_index.clear();
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
            }
            else if (m_collapsed)
            {
                Engine::ParamsSetVisibility vis{Engine::Vis_SelfHitTestInvisible};
                Engine::call(m_layer_canvas, L"SetVisibility", vis);
                m_collapsed = false;
                refresh_collected();   // visibility-only diff, no re-parenting
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
