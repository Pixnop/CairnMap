-- CairnMap companion: keep the game's item-icon textures resident so the C++
-- side can StaticFindObject them. Runs on a light permanent loop instead of a
-- fixed delay: whenever the map is opened, the textures are already loaded.
local TEX = {
    "T_itemicon_Material_Coal", "T_itemicon_Material_CopperOre",
    "T_itemicon_Material_Quartz", "T_itemicon_Material_Sulfur",
    "T_itemicon_Material_CrudeOil", "T_itemicon_Material_SkyIslandOre",
    "T_itemicon_Material_WorldTreeOre", "T_itemicon_Material_Lava_Ancient",
    "T_itemicon_Material_NightStone", "T_itemicon_Material_DogCoin",
    "T_itemicon_Food_Lotus_hp_01", "T_itemicon_Relic",
}
local loaded = {}
local function pass()
    ExecuteInGameThread(function()
        local n = 0
        for _, t in ipairs(TEX) do
            if not loaded[t] then
                local path = "/Game/Others/InventoryItemIcon/Texture/" .. t .. "." .. t
                pcall(function()
                    local o = StaticFindObject(path)
                    if not (o and o:IsValid()) then o = LoadAsset(path) end
                    if o and o:IsValid() then loaded[t] = true end
                end)
            end
            if loaded[t] then n = n + 1 end
        end
        if n < #TEX then
            ExecuteWithDelay(3000, pass)   -- keep trying until all resident
        else
            print("[CairnMap.lua] all icon textures resident\n")
        end
    end)
end
ExecuteWithDelay(3000, pass)
print("[CairnMap.lua] icon preloader armed (permanent)\n")
