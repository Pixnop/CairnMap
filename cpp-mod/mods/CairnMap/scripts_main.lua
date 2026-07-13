-- CairnMap companion: load the game's item-icon textures AND hold strong refs
-- so UE's GC keeps them resident (LoadAsset alone lets them be collected).
_CairnTextures = _CairnTextures or {}
local TEX = {
    "T_itemicon_Material_Coal", "T_itemicon_Material_CopperOre",
    "T_itemicon_Material_Quartz", "T_itemicon_Material_Sulfur",
    "T_itemicon_Material_CrudeOil", "T_itemicon_Material_SkyIslandOre",
    "T_itemicon_Material_WorldTreeOre", "T_itemicon_Material_Lava_Ancient",
    "T_itemicon_Material_NightStone", "T_itemicon_Material_DogCoin",
    "T_itemicon_Food_Lotus_hp_01", "T_itemicon_Relic",
    "T_itemicon_Material_Sapphire", "T_itemicon_Consume_TechnologyBook_G1",
    "T_itemicon_Consume_TreasureBoxKey01", "T_itemicon_Food_BaconEggs",
    "T_itemicon_Consume_AffectionFruit_01",
}
local function pass()
    ExecuteInGameThread(function()
        local held = 0
        for _, t in ipairs(TEX) do
            local ref = _CairnTextures[t]
            if not (ref and ref:IsValid()) then
                local path = "/Game/Others/InventoryItemIcon/Texture/" .. t .. "." .. t
                pcall(function()
                    local o = LoadAsset(path)
                    if o and o:IsValid() then
                        pcall(function() o:AddRef() end)         -- UE4SS strong ref
                        _CairnTextures[t] = o                    -- Lua strong ref
                    end
                end)
            end
            if _CairnTextures[t] and _CairnTextures[t]:IsValid() then held = held + 1 end
        end
        print(string.format("[CairnMap.lua] icon textures resident: %d/%d\n", held, #TEX))
        if held < #TEX then
            ExecuteWithDelay(3000, pass)   -- keep trying until all resident
        end
    end)
end
ExecuteWithDelay(2000, pass)
print("[CairnMap.lua] icon holder armed\n")
