-- CairnMap companion: preload the game's item icon textures so the C++ side
-- can StaticFindObject them (they only load lazily with the inventory UI).
local TEX = {
    "T_itemicon_Material_Coal", "T_itemicon_Material_CopperOre",
    "T_itemicon_Material_Quartz", "T_itemicon_Material_Sulfur",
    "T_itemicon_Material_CrudeOil", "T_itemicon_Material_SkyIslandOre",
    "T_itemicon_Material_WorldTreeOre", "T_itemicon_Material_Lava_Ancient",
    "T_itemicon_Material_NightStone", "T_itemicon_Material_DogCoin",
    "T_itemicon_Food_Lotus_hp_01", "T_itemicon_Relic",
}
local function preload()
    ExecuteInGameThread(function()
        local n = 0
        for _, t in ipairs(TEX) do
            local path = "/Game/Others/InventoryItemIcon/Texture/" .. t .. "." .. t
            local ok = pcall(function()
                local obj = StaticFindObject(path)
                if not (obj and obj:IsValid()) then
                    LoadAsset(path)
                end
            end)
            if ok then n = n + 1 end
        end
        print(string.format("[CairnMap.lua] %d icon textures preloaded\n", n))
    end)
end
ExecuteWithDelay(12000, preload)
ExecuteWithDelay(60000, preload)
print("[CairnMap.lua] icon preloader armed\n")
