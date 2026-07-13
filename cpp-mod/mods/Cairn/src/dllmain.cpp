// Cairn — UE4SS C++ mod (map collectables for Palworld 1.0+) (see docs/SPEC-2.0.md)
// P0 skeleton: loads, logs, schedules a game-thread watcher tick.
// ABI target: UE4SS v3.0.1 (Okaetsu experimental-palworld, c2ac246, MSVC).

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>

namespace Cairn
{
    using namespace RC;
    using namespace Unreal;

    class Mod final : public RC::CppUserModBase
    {
      public:
        Mod()
        {
            ModVersion = STR("2.0.0-p0");
            ModName = STR("Cairn");
            ModAuthors = STR("Pixnop");
            ModDescription = STR("Cairn: map collectables for Palworld 1.0+");
            Output::send<LogLevel::Default>(STR("[Cairn] loaded (P0 skeleton)\n"));
        }

        ~Mod() override = default;

        auto on_unreal_init() -> void override
        {
            // ⚠ SPEC §2.6: all widget/actor work must happen on the game
            // thread. UE4SS C++ mods get on_update() on the game thread —
            // the watcher lives there (no LoopAsync-style off-thread races,
            // which crashed the Lua prototype until wrapped).
            Output::send<LogLevel::Default>(STR("[Cairn] Unreal initialized\n"));
        }

        auto on_update() -> void override
        {
            // Game-thread tick. P1 will run the change-signature watcher here
            // (SPEC §2.6): debounced map detection -> repair pipeline.
        }
    };
} // namespace Cairn

#define MOD_EXPORT __declspec(dllexport)
extern "C"
{
    MOD_EXPORT RC::CppUserModBase* start_mod()
    {
        return new Cairn::Mod();
    }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
