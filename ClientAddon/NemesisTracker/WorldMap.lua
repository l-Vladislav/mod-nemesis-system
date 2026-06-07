NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

NT.WorldMap = NT.WorldMap or {}
local WM = NT.WorldMap

local PIN_SIZE = 16
local MAX_WORLD_PINS = 60
local LIST_ROW_HEIGHT = 16
local LIST_MAX_ROWS = 40
local listZoneOnly = true
local listVisible = true
local pinsVisible = true

local worldPins = {}
local listRows = {}
local listPanel = nil
local listScrollOffset = 0
local hoveredPin = nil
local pinOverlay = nil
local findNemesisByUnit

local function threatColor(threat)
    if threat == "extreme" then
        return 1.0, 0.1, 0.1
    end
    if threat == "high" then
        return 1.0, 0.45, 0.1
    end
    if threat == "medium" then
        return 1.0, 0.82, 0.0
    end
    return 0.4, 1.0, 0.4
end

local function rankColor(rank)
    if rank >= 8 then
        return 1.0, 0.0, 0.0
    end
    if rank >= 5 then
        return 1.0, 0.4, 0.0
    end
    if rank >= 3 then
        return 1.0, 0.82, 0.0
    end
    return 0.6, 0.8, 1.0
end

local SELECTED_PIN_SIZE = PIN_SIZE * 2

----------------------------------------------------------------
-- Russian localization + RP flavor for nemesis tooltips
----------------------------------------------------------------

local RANK_TIER_RU = {
    Marked     = "Меченый",
    Hated      = "Ненавистный",
    Relentless = "Неумолимый",
    Legendary  = "Легендарный",
    Mythic     = "Мифический",
}

local AFFIX_RU = {
    Vampiric     = "Кровопийца",
    Swift        = "Стремительный",
    Juggernaut   = "Несокрушимый",
    Savage       = "Свирепый",
    Spellward    = "Защитник от чар",
    Enraged      = "Разъярённый",
    Regenerating = "Регенерирующий",
    None         = "Нет",
}

local THREAT_RU = {
    low     = "слабая",
    medium  = "средняя",
    high    = "высокая",
    extreme = "смертельная",
}

local REWARD_RU = {
    none    = "нет",
    revenge = "месть",
    shared  = "союзная охота",
    bounty  = "охота за головой",
}

local ROMAN = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X" }

local function toRoman(n)
    if not n or n < 1 then
        return "?"
    end
    return ROMAN[n] or tostring(n)
end

local function translateAffixes(text)
    if not text or text == "" or text == "None" then
        return nil
    end
    local parts = {}
    for word in string.gmatch(text, "[^,%s]+") do
        table.insert(parts, AFFIX_RU[word] or word)
    end
    if #parts == 0 then
        return nil
    end
    return table.concat(parts, ", ")
end

-- Appends RP-styled Russian nemesis info to an already-opened tooltip.
-- Caller is expected to have set the top line (creature title / name).
-- opts: { showLevel=bool, showZone=bool, showReward=bool }
-- showReward defaults to true; others default to false.
function WM.RenderTooltipBlock(tooltip, nemesis, opts)
    if not tooltip or not nemesis then
        return
    end
    opts = opts or {}

    local tier = nemesis.rankTier or "Marked"
    local tierRu = RANK_TIER_RU[tier] or tier
    local rank = nemesis.rank or 1

    if opts.showLevel then
        tooltip:AddLine(string.format("Уровень %d  |  Ранг %s — %s",
            nemesis.level or 0, toRoman(rank), tierRu), 1, 0.82, 0)
    else
        tooltip:AddLine(string.format("Ранг %s — %s",
            toRoman(rank), tierRu), 1, 0.82, 0)
    end

    if opts.showZone and nemesis.zoneName and nemesis.zoneName ~= "" then
        tooltip:AddLine("Зона: " .. nemesis.zoneName, 0.7, 0.7, 0.7)
    end

    local affixes = translateAffixes(nemesis.affixText)
    if affixes then
        tooltip:AddLine("Способности: " .. affixes, 1.0, 0.6, 0.2)
    end

    local tr, tg, tb = threatColor(nemesis.threatClass)
    tooltip:AddLine("Угроза: " .. (THREAT_RU[nemesis.threatClass]
        or nemesis.threatClass or ""), tr, tg, tb)

    -- Reward line is only surfaced for the player's active bounty target.
    -- The server-side rewardClass ("bounty"/"revenge"/"shared"/"none") is
    -- computed per-nemesis, but the tooltip shouldn't claim every nemesis is
    -- a bounty — that's reserved for the one the player has actually accepted.
    if opts.showReward ~= false and NT:IsActiveBounty(nemesis) then
        tooltip:AddLine("Награда: " .. REWARD_RU.bounty, 1.0, 0.82, 0.0)
    end
end

-- Get the internal map file name for the current zone.
-- GetMapInfo() returns a locale-independent file name like "Silverpine",
-- which works on any client language (EN, RU, etc.)
local function getCurrentMapFile()
    if GetMapInfo then
        local mapFile = GetMapInfo()
        if mapFile and mapFile ~= "" then
            return string.lower(mapFile)
        end
    end
    return nil
end

-- Resolve a nemesis entry to its internal map file name via MapData
local function getNemesisMapFile(nemesis)
    if not nemesis then
        return nil
    end

    if NT.MapData and type(NT.MapData.GetZone) == "function" then
        local info = NT.MapData:GetZone(nemesis.zoneId, nemesis.zoneName)
        if info and info.file then
            return string.lower(info.file)
        end
    end

    return nil
end

-- Build reverse lookup: map file name -> {continent, zoneIndex} for SetMapZoom
local mapFileToZoom = nil
local function buildMapFileToZoom()
    if mapFileToZoom then
        return
    end
    mapFileToZoom = {}
    if not GetMapZones or not SetMapZoom or not GetMapInfo then
        return
    end
    for continent = 0, 4 do
        local zones = { GetMapZones(continent) }
        for zoneIndex = 1, #zones do
            SetMapZoom(continent, zoneIndex)
            local mapFile = GetMapInfo()
            if mapFile and mapFile ~= "" then
                mapFileToZoom[string.lower(mapFile)] = { continent = continent, zone = zoneIndex }
            end
        end
    end
    -- Restore map to player zone
    if SetMapToCurrentZone then
        SetMapToCurrentZone()
    end
end

local function isNemesisInCurrentZone(nemesis)
    if not nemesis then
        return false
    end

    -- Dungeon branch: 3.3.5 instances have no world-map files and zone-name
    -- matching is unreliable (subzones, map-vs-zone name). The server pushes
    -- the current dungeon mapId (V2:DUNGEON); match the entry's mapId to it.
    if IsInInstance and IsInInstance() then
        local cur = NT.data and NT.data.currentDungeonMapId
        return cur ~= nil and cur ~= 0 and nemesis.mapId == cur
    end

    -- Primary: compare locale-independent map file names
    -- This works on any client language (RU, EN, DE, etc.)
    local currentFile = getCurrentMapFile()
    if currentFile then
        local nemesisFile = getNemesisMapFile(nemesis)
        if nemesisFile and nemesisFile == currentFile then
            return true
        end
    end

    -- Fallback: if the world map is open, SetMapToCurrentZone may have
    -- been called, so also try matching the zone ID directly against
    -- MapData's byZoneId table via the file name approach above.
    -- If we reach here, the nemesis zone is not in MapData — try
    -- a direct zoneId match with the nemesis data's own zoneId field
    -- by checking if any other nemesis in the same zoneId has a
    -- matching file (already covered above).

    return false
end

-- Public alias so other modules (e.g. BountyBoard) can reuse the same
-- locale-independent current-zone check.
WM.IsNemesisInCurrentZone = isNemesisInCurrentZone

local function navigateToNemesisZone(nemesis)
    if not nemesis then
        return
    end
    if isNemesisInCurrentZone(nemesis) then
        return
    end
    local nemesisFile = getNemesisMapFile(nemesis)
    if not nemesisFile then
        return
    end
    buildMapFileToZoom()
    local zoom = mapFileToZoom[nemesisFile]
    if zoom and SetMapZoom then
        SetMapZoom(zoom.continent, zoom.zone)
    end
end

-- Cached player zone file, updated on map open and zone change
local cachedPlayerZoneFile = nil

local function updatePlayerZoneCache()
    if not GetMapInfo or not SetMapToCurrentZone then
        return
    end
    local oldMap = GetMapInfo()
    SetMapToCurrentZone()
    local file = GetMapInfo()
    if file and file ~= "" then
        cachedPlayerZoneFile = string.lower(file)
    end
    -- Restore previous map view if it was different
    if oldMap and oldMap ~= "" and oldMap ~= file then
        -- Can't easily restore, will be set by caller
    end
end

function WM.updatePlayerZoneCache()
    updatePlayerZoneCache()
end

local function getPlayerZoneFile()
    return cachedPlayerZoneFile
end

local function isNemesisInPlayerZone(nemesis, playerFile)
    if not nemesis or not playerFile then
        return false
    end
    local nemesisFile = getNemesisMapFile(nemesis)
    return nemesisFile and nemesisFile == playerFile
end

local function getAllNemesesSorted()
    local results = {}
    for _, nemesis in pairs(NT.data.nemeses) do
        if not NT:ShouldHideNemesis(nemesis) then
            table.insert(results, nemesis)
        end
    end

    -- Use player's real zone, not the map being viewed
    local playerFile = getPlayerZoneFile()
    local inPlayerZone = {}
    for _, nemesis in ipairs(results) do
        if isNemesisInPlayerZone(nemesis, playerFile) then
            inPlayerZone[nemesis.spawnId] = true
        end
    end

    table.sort(results, function(a, b)
        -- Player's zone always first
        local aLocal = inPlayerZone[a.spawnId] or false
        local bLocal = inPlayerZone[b.spawnId] or false
        if aLocal ~= bLocal then
            return aLocal
        end
        local aZone = a.zoneName or ""
        local bZone = b.zoneName or ""
        if aZone ~= bZone then
            return aZone < bZone
        end
        if (a.rank or 1) ~= (b.rank or 1) then
            return (a.rank or 1) > (b.rank or 1)
        end
        return (a.name or "") < (b.name or "")
    end)

    return results
end

local function createWorldPin(index)
    local parent = pinOverlay or WorldMapButton
    local pin = CreateFrame("Button", "NemesisWorldPin" .. index, parent)
    pin:SetWidth(PIN_SIZE)
    pin:SetHeight(PIN_SIZE)
    pin:SetFrameStrata("FULLSCREEN")
    pin:SetFrameLevel(110 + index)

    -- Larger hit area for easier hovering
    pin.hitArea = pin:CreateTexture(nil, "BACKGROUND")
    pin.hitArea:SetPoint("CENTER", pin, "CENTER", 0, 0)
    pin.hitArea:SetWidth(PIN_SIZE + 8)
    pin.hitArea:SetHeight(PIN_SIZE + 8)
    pin.hitArea:SetTexture(0, 0, 0, 0)
    pin:SetHitRectInsets(-4, -4, -4, -4)

    -- Selection glow ring (behind icon). ADD blend so the dark pixels of
    -- UI-Minimap-Background become transparent and only the tint shows —
    -- no more black halo around the pin.
    pin.glow = pin:CreateTexture(nil, "BORDER")
    pin.glow:SetPoint("CENTER", pin, "CENTER", 0, 0)
    pin.glow:SetWidth(PIN_SIZE + 10)
    pin.glow:SetHeight(PIN_SIZE + 10)
    pin.glow:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
    pin.glow:SetBlendMode("ADD")
    pin.glow:SetVertexColor(1.0, 1.0, 0.0, 0.7)
    pin.glow:Hide()

    pin.icon = pin:CreateTexture(nil, "ARTWORK")
    pin.icon:SetAllPoints(pin)
    pin.icon:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")

    -- "Wanted" quest-mark overlay: yellow exclamation, centered over
    -- the top-right corner of the skull (half inside, half outside —
    -- standard badge style), shown only for the player's active bounty.
    pin.questMark = pin:CreateTexture(nil, "OVERLAY")
    pin.questMark:SetPoint("CENTER", pin, "TOPRIGHT", 0, -2)
    pin.questMark:SetWidth(16)
    pin.questMark:SetHeight(16)
    pin.questMark:SetTexture("Interface\\GossipFrame\\AvailableQuestIcon")
    pin.questMark:Hide()

    pin:EnableMouse(true)
    pin:RegisterForClicks("LeftButtonUp")

    pin:SetScript("OnClick", function(self)
        if self.spawnId then
            NT:SelectNemesis(self.spawnId)
            WM:RefreshWorldMap()
            WM:RefreshListPanel()
        end
    end)

    pin:SetScript("OnEnter", function(self)
        hoveredPin = self
        if WorldMapPOIFrame then
            WorldMapPOIFrame.allowBlobTooltip = false
        end
        if not self.nemesis then
            return
        end
        local nemesis = self.nemesis
        local tooltip = WorldMapTooltip or GameTooltip
        tooltip:SetOwner(self, "ANCHOR_RIGHT")
        local title = nemesis.nemesisTitle or ""
        if title == "" then
            title = nemesis.name or "Unknown"
        end
        tooltip:SetText(string.format("|cffff4444%s", title))
        local pinBaseName = nemesis.localizedName or nemesis.name or ""
        if pinBaseName ~= "" and pinBaseName ~= title then
            tooltip:AddLine(pinBaseName, 0.75, 0.75, 0.75)
        end
        WM.RenderTooltipBlock(tooltip, nemesis, { showLevel = true })
        tooltip:Show()
    end)

    pin:SetScript("OnLeave", function()
        hoveredPin = nil
        if WorldMapPOIFrame then
            WorldMapPOIFrame.allowBlobTooltip = true
        end
        local tooltip = WorldMapTooltip or GameTooltip
        tooltip:Hide()
    end)

    pin:Hide()
    worldPins[index] = pin
    return pin
end


----------------------------------------------------------------
-- World map side panel: lists ALL nemeses grouped by zone
----------------------------------------------------------------

local function createListRow(parent, index)
    local row = CreateFrame("Button", nil, parent)
    row:SetHeight(LIST_ROW_HEIGHT)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 4, -((index - 1) * LIST_ROW_HEIGHT))
    row:SetPoint("RIGHT", parent, "RIGHT", -4, 0)

    row.bg = row:CreateTexture(nil, "BACKGROUND")
    row.bg:SetAllPoints(row)
    row.bg:SetTexture(0, 0, 0, 0)

    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightExtraSmall")
    row.text:SetPoint("LEFT", row, "LEFT", 2, 0)
    row.text:SetPoint("RIGHT", row, "RIGHT", -2, 0)
    row.text:SetJustifyH("LEFT")
    row.text:SetWordWrap(false)

    row:SetScript("OnEnter", function(self)
        if not self.nemesis then
            return
        end
        self.bg:SetTexture(0.3, 0.3, 0.4, 0.4)
        local nemesis = self.nemesis
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        local listTitle = nemesis.nemesisTitle or ""
        if listTitle == "" then
            listTitle = nemesis.name or "Unknown"
        end
        GameTooltip:SetText(string.format("|cffff4444%s", listTitle))
        WM.RenderTooltipBlock(GameTooltip, nemesis, { showLevel = true })
        GameTooltip:Show()
    end)

    row:SetScript("OnLeave", function(self)
        self.bg:SetTexture(0, 0, 0, 0)
        GameTooltip:Hide()
    end)

    row:SetScript("OnClick", function(self)
        if self.nemesis and self.nemesis.spawnId then
            NT:SelectNemesis(self.nemesis.spawnId)
            navigateToNemesisZone(self.nemesis)
            WM:RefreshWorldMap()
            WM:RefreshListPanel()
        end
    end)

    row:RegisterForClicks("LeftButtonUp")
    row:Hide()
    listRows[index] = row
    return row
end

local function createListPanel()
    if listPanel then
        return listPanel
    end

    local panel = CreateFrame("Frame", "NemesisWorldMapList", WorldMapFrame)
    panel:SetWidth(240)
    panel:SetPoint("TOPRIGHT", WorldMapFrame, "TOPRIGHT", -8, -60)
    panel:SetPoint("BOTTOMRIGHT", WorldMapFrame, "BOTTOMRIGHT", -8, 12)
    panel:SetFrameStrata("FULLSCREEN")
    panel:SetFrameLevel(20)

    panel:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 16,
        edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    panel:SetBackdropColor(0, 0, 0, 0.85)

    local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOP", panel, "TOP", 0, -8)
    title:SetText("|cffff4444Немезиды|r")
    panel.title = title

    local countText = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightExtraSmall")
    countText:SetPoint("TOP", title, "BOTTOM", 0, -2)
    panel.countText = countText

    local zoneFilter = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
    zoneFilter:SetWidth(90)
    zoneFilter:SetHeight(18)
    zoneFilter:SetPoint("TOP", countText, "BOTTOM", 0, -2)
    zoneFilter:SetText("This Zone")
    zoneFilter:SetScript("OnClick", function()
        listZoneOnly = not listZoneOnly
        if listZoneOnly then
            zoneFilter:SetText("This Zone")
            if SetMapToCurrentZone then
                SetMapToCurrentZone()
            end
            WM:RefreshWorldMap()
        else
            zoneFilter:SetText("All Zones")
        end
        WM:RefreshListPanel()
    end)
    panel.zoneFilter = zoneFilter

    local scrollFrame = CreateFrame("ScrollFrame", "NemesisWorldMapListScroll", panel, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", panel, "TOPLEFT", 6, -52)
    scrollFrame:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -26, 6)
    panel.scrollFrame = scrollFrame

    local content = CreateFrame("Frame", nil, scrollFrame)
    content:SetWidth(scrollFrame:GetWidth())
    content:SetHeight(1)
    scrollFrame:SetScrollChild(content)
    panel.content = content

    for i = 1, LIST_MAX_ROWS do
        createListRow(content, i)
    end

    -- Toggle pins on/off (skull icon)
    local togglePins = CreateFrame("Button", "NemesisWorldMapTogglePins", WorldMapFrame)
    togglePins:SetWidth(26)
    togglePins:SetHeight(26)
    togglePins:SetPoint("TOPRIGHT", WorldMapFrame, "TOPRIGHT", -10, -36)
    togglePins:SetFrameStrata("FULLSCREEN")
    togglePins:SetFrameLevel(25)
    togglePins.icon = togglePins:CreateTexture(nil, "ARTWORK")
    togglePins.icon:SetAllPoints(togglePins)
    togglePins.icon:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")
    togglePins:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square")
    togglePins:SetScript("OnClick", function()
        pinsVisible = not pinsVisible
        if NT.db then NT.db.showPins = pinsVisible end
        if pinsVisible then
            togglePins.icon:SetDesaturated(false)
            togglePins.icon:SetAlpha(1.0)
            WM:RefreshWorldMap()
        else
            togglePins.icon:SetDesaturated(true)
            togglePins.icon:SetAlpha(0.4)
            for _, pin in ipairs(worldPins) do
                pin:Hide()
            end
        end
    end)
    togglePins:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Toggle map pins")
        GameTooltip:Show()
    end)
    togglePins:SetScript("OnLeave", function() GameTooltip:Hide() end)
    panel.togglePinsButton = togglePins

    -- Toggle list on/off (scroll/list icon)
    local toggleList = CreateFrame("Button", "NemesisWorldMapToggleList", WorldMapFrame)
    toggleList:SetWidth(26)
    toggleList:SetHeight(26)
    toggleList:SetPoint("RIGHT", togglePins, "LEFT", -4, 0)
    toggleList:SetFrameStrata("FULLSCREEN")
    toggleList:SetFrameLevel(25)
    toggleList.icon = toggleList:CreateTexture(nil, "ARTWORK")
    toggleList.icon:SetAllPoints(toggleList)
    toggleList.icon:SetTexture("Interface\\BUTTONS\\UI-GuildButton-PublicNote-Up")
    toggleList:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square")
    toggleList:SetScript("OnClick", function()
        if panel:IsShown() then
            panel:Hide()
            listVisible = false
            toggleList.icon:SetDesaturated(true)
            toggleList.icon:SetAlpha(0.4)
        else
            panel:Show()
            listVisible = true
            toggleList.icon:SetDesaturated(false)
            toggleList.icon:SetAlpha(1.0)
            WM:RefreshListPanel()
        end
        if NT.db then NT.db.showList = listVisible end
    end)
    toggleList:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Toggle nemesis list")
        GameTooltip:Show()
    end)
    toggleList:SetScript("OnLeave", function() GameTooltip:Hide() end)
    panel.toggleListButton = toggleList

    panel:Show()
    listPanel = panel
    return panel
end

function WM:RefreshListPanel()
    if not listPanel or not listPanel:IsShown() then
        return
    end

    local allNemeses = getAllNemesesSorted()

    -- Filter to player zone if toggled
    local playerFile = getPlayerZoneFile()
    local filtered = {}
    if listZoneOnly then
        for _, nemesis in ipairs(allNemeses) do
            if isNemesisInPlayerZone(nemesis, playerFile) then
                table.insert(filtered, nemesis)
            end
        end
    else
        filtered = allNemeses
    end

    local totalCount = #filtered
    if listZoneOnly then
        listPanel.countText:SetText(string.format("%d in this zone", totalCount))
    else
        listPanel.countText:SetText(string.format("%d tracked across all zones", totalCount))
    end

    -- Build display list with zone headers
    local displayList = {}
    local lastZone = nil

    for _, nemesis in ipairs(filtered) do
        local zone = nemesis.zoneName or "Unknown"
        if zone ~= lastZone then
            table.insert(displayList, { isHeader = true, zoneName = zone })
            lastZone = zone
        end
        table.insert(displayList, { isHeader = false, nemesis = nemesis })
    end

    -- Ensure enough rows
    while #listRows < #displayList and #listRows < 200 do
        createListRow(listPanel.content, #listRows + 1)
    end

    local inCurrentZone = {}
    for _, nemesis in ipairs(allNemeses) do
        if isNemesisInPlayerZone(nemesis, playerFile) then
            inCurrentZone[nemesis.spawnId] = true
        end
    end

    for index, entry in ipairs(displayList) do
        local row = listRows[index]
        if not row then
            break
        end

        row:ClearAllPoints()
        row:SetHeight(LIST_ROW_HEIGHT)
        row:SetPoint("TOPLEFT", listPanel.content, "TOPLEFT", 4, -((index - 1) * LIST_ROW_HEIGHT))
        row:SetPoint("RIGHT", listPanel.content, "RIGHT", -4, 0)

        if entry.isHeader then
            row.nemesis = nil
            row.text:SetText("|cffffd200" .. entry.zoneName .. "|r")
            row.bg:SetTexture(0.15, 0.15, 0.2, 0.5)
            row:Show()
        else
            local nemesis = entry.nemesis
            row.nemesis = nemesis
            local r, g, b = rankColor(nemesis.rank or 1)
            local currentMark = ""
            if inCurrentZone[nemesis.spawnId] then
                currentMark = "|cff00ff00*|r "
            end

            -- Active bounty target: gold exclamation prefix + bold name.
            local isActiveBounty = NT:IsActiveBounty(nemesis)
            local bountyMark = isActiveBounty and "|cffffd100!|r " or ""

            -- Format coordinates from mapX/mapY (0-1) to percentage display
            local coordText = ""
            local mx = nemesis.mapX or 0
            local my = nemesis.mapY or 0
            if mx > 0 and my > 0 then
                coordText = string.format(" |cffaaaaaa(%.1f, %.1f)|r", mx * 100, my * 100)
            end

            local displayName = nemesis.nemesisTitle or ""
            if displayName == "" then
                displayName = nemesis.name or "Unknown"
            end
            if isActiveBounty then
                displayName = "|cffffd100" .. displayName .. "|r"
            end

            row.text:SetText(string.format("%s%s|cff%02x%02x%02xR%d|r %s%s",
                bountyMark,
                currentMark,
                math.floor(r * 255), math.floor(g * 255), math.floor(b * 255),
                nemesis.rank or 1,
                displayName,
                coordText))

            -- Active bounty stays fully opaque; staleness alpha ignored.
            local alpha = isActiveBounty and 1.0 or NT:GetVisibilityAlpha(nemesis)
            row:SetAlpha(alpha)

            -- Row background: active bounty wins, then selection, then default.
            if isActiveBounty then
                row.bg:SetTexture(0.35, 0.30, 0.05, 0.6)  -- muted gold
            else
                local selectedId = NT.data.selectedSpawnId
                if selectedId and nemesis.spawnId == selectedId then
                    row.bg:SetTexture(0.25, 0.25, 0.35, 0.7)
                else
                    row.bg:SetTexture(0, 0, 0, 0)
                end
            end

            row:Show()
        end
    end

    -- Hide unused rows
    for i = #displayList + 1, #listRows do
        listRows[i]:Hide()
    end

    listPanel.content:SetHeight(math.max(1, #displayList * LIST_ROW_HEIGHT))
end

----------------------------------------------------------------
-- World map pins: current zone only (accurate positioning)
----------------------------------------------------------------

function WM:GetNemesesForCurrentZone()
    local results = {}
    for _, nemesis in pairs(NT.data.nemeses) do
        if not NT:ShouldHideNemesis(nemesis) and isNemesisInCurrentZone(nemesis) then
            table.insert(results, nemesis)
        end
    end
    return results
end

function WM:RefreshWorldMap()
    if not WorldMapButton then
        return
    end

    if not WorldMapFrame or not WorldMapFrame:IsShown() or not pinsVisible then
        for _, pin in ipairs(worldPins) do
            pin:Hide()
        end
        return
    end

    local nemeses = self:GetNemesesForCurrentZone()
    local width = WorldMapButton:GetWidth()
    local height = WorldMapButton:GetHeight()
    local selectedId = NT.data.selectedSpawnId
    local usedCount = 0

    for index, nemesis in ipairs(nemeses) do
        if index > MAX_WORLD_PINS then
            break
        end

        local pin = worldPins[index] or createWorldPin(index)
        pin.spawnId = nemesis.spawnId
        pin.nemesis = nemesis

        local isSelected = nemesis.spawnId == selectedId
        local isActiveBounty = NT:IsActiveBounty(nemesis)

        -- Size: active bounty stays normal; only user-selected pin grows.
        local size = isSelected and SELECTED_PIN_SIZE or PIN_SIZE
        pin:SetWidth(size)
        pin:SetHeight(size)

        local mapX = nemesis.mapX or 0.5
        local mapY = nemesis.mapY or 0.5

        if mapX > 0 and mapX < 1 and mapY > 0 and mapY < 1 then
            pin:ClearAllPoints()
            pin:SetPoint("CENTER", WorldMapButton, "TOPLEFT",
                width * mapX, -(height * mapY))

            local r, g, b = rankColor(nemesis.rank or 1)
            pin.icon:SetVertexColor(r, g, b)

            -- Active bounty overrides staleness — always fully opaque.
            local alpha = isActiveBounty and 1.0 or NT:GetVisibilityAlpha(nemesis)
            pin:SetAlpha(alpha)

            -- Glow: gold for active bounty (wins over selection), yellow for selection.
            if isActiveBounty then
                pin.glow:SetVertexColor(1.0, 0.78, 0.0, 1.0)  -- gold
                pin.glow:SetWidth(size + 12)
                pin.glow:SetHeight(size + 12)
                pin.glow:Show()
            elseif isSelected then
                pin.glow:SetVertexColor(1.0, 1.0, 0.0, 0.7)   -- yellow
                pin.glow:SetWidth(size + 8)
                pin.glow:SetHeight(size + 8)
                pin.glow:Show()
            else
                pin.glow:Hide()
            end

            -- Quest-mark "!" overlay only on the active bounty target.
            if isActiveBounty then
                pin.questMark:Show()
            else
                pin.questMark:Hide()
            end

            pin:Show()
            usedCount = index
        end
    end

    -- Hide only unused pins beyond the current set
    for i = usedCount + 1, #worldPins do
        worldPins[i]:Hide()
    end

    -- Also refresh the side panel
    self:RefreshListPanel()
end


----------------------------------------------------------------
-- Tooltip hook: show nemesis info on unit tooltips
----------------------------------------------------------------

function WM:HookTooltips()
    if self.tooltipHooked then
        return
    end
    self.tooltipHooked = true

    GameTooltip:HookScript("OnTooltipSetUnit", function(self)
        local _, unit = self:GetUnit()
        if not unit or not UnitExists(unit) then
            return
        end
        local nemesis = findNemesisByUnit(unit)
        if nemesis then
            local title = nemesis.nemesisTitle or ""
            if title ~= "" then
                GameTooltipTextLeft1:SetText("|cffff4444" .. title .. "|r")
            end
            local baseName = nemesis.localizedName or nemesis.name or ""
            if baseName ~= "" and baseName ~= title then
                GameTooltip:AddLine(baseName, 0.75, 0.75, 0.75)
            end
            GameTooltip:AddLine(" ")
            WM.RenderTooltipBlock(GameTooltip, nemesis)
            GameTooltip:Show()
        end
    end)
end

----------------------------------------------------------------
-- Portrait nemesis icon
----------------------------------------------------------------

local function parseSpawnId(guid)
    if not guid then
        return nil
    end
    local low = string.match(guid, "(%x+)$")
    if not low then
        return nil
    end
    if string.len(low) > 8 then
        low = string.sub(low, -8)
    end
    return tonumber(low, 16)
end

local function createPortraitIcon(parentFrame, anchorFrame)
    local size = 22
    local frame = CreateFrame("Frame", nil, parentFrame)
    frame:SetWidth(size)
    frame:SetHeight(size)
    frame:SetFrameStrata("HIGH")
    frame:SetFrameLevel(10)
    frame:SetPoint("CENTER", anchorFrame, "TOP", 0, 0)

    -- Colored glow behind the skull. ADD blend so the dark texture becomes
    -- transparent — only the tinted highlight shows, no black halo.
    frame.glow = frame:CreateTexture(nil, "BACKGROUND")
    frame.glow:SetPoint("CENTER", frame, "CENTER", 0, 0)
    frame.glow:SetWidth(size + 8)
    frame.glow:SetHeight(size + 8)
    frame.glow:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
    frame.glow:SetBlendMode("ADD")
    frame.glow:SetVertexColor(1.0, 0.0, 0.0, 0.6)

    -- Skull icon
    frame.icon = frame:CreateTexture(nil, "ARTWORK")
    frame.icon:SetAllPoints(frame)
    frame.icon:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")

    -- "Wanted" quest-marker overlay — shown only when this unit is the
    -- player's active bounty target. Fully outside the skull, floating to
    -- the right with a small gap.
    frame.questMark = frame:CreateTexture(nil, "OVERLAY")
    frame.questMark:SetPoint("LEFT", frame, "RIGHT", -8, 6)
    frame.questMark:SetWidth(16)
    frame.questMark:SetHeight(16)
    frame.questMark:SetTexture("Interface\\GossipFrame\\AvailableQuestIcon")
    frame.questMark:Hide()

    frame:EnableMouse(true)
    frame:SetScript("OnEnter", function(self)
        if not self.nemesis then
            return
        end
        local nemesis = self.nemesis
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        local portTitle = nemesis.nemesisTitle or ""
        if portTitle == "" then
            portTitle = nemesis.name or "Немезида"
        end
        GameTooltip:SetText(string.format("|cffff4444%s", portTitle))
        WM.RenderTooltipBlock(GameTooltip, nemesis)
        GameTooltip:Show()
    end)
    frame:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)

    frame:Hide()
    return frame
end

local targetIcon = nil
local focusIcon = nil

findNemesisByUnit = function(unit)
    if not unit or not UnitExists(unit) then
        return nil
    end

    local guid = UnitGUID(unit)

    -- 1. Exact match by runtime GUID from server
    if guid then
        for _, nemesis in pairs(NT.data.nemeses) do
            if nemesis.runtimeGuid and nemesis.runtimeGuid ~= "" and nemesis.runtimeGuid == guid then
                return nemesis
            end
        end
    end

    -- 2. Match by nemesis title (creature renamed server-side)
    local unitName = UnitName(unit)
    if unitName and unitName ~= "" then
        for _, nemesis in pairs(NT.data.nemeses) do
            if nemesis.nemesisTitle and nemesis.nemesisTitle ~= "" and nemesis.nemesisTitle == unitName then
                return nemesis
            end
        end
    end

    return nil
end

local function updatePortraitIcon(icon, unit)
    if not icon then
        return
    end

    if not unit or not UnitExists(unit) then
        icon.nemesis = nil
        icon:Hide()
        return
    end

    local nemesis = findNemesisByUnit(unit)
    if not nemesis then
        icon.nemesis = nil
        icon:Hide()
        return
    end
    icon.nemesis = nemesis

    local r, g, b = rankColor(nemesis.rank or 1)
    icon.icon:SetVertexColor(r, g, b)

    local isActiveBounty = NT:IsActiveBounty(nemesis)
    if isActiveBounty then
        -- Gold glow for the player's active bounty target.
        icon.glow:SetVertexColor(1.0, 0.78, 0.0, 0.9)
        icon.questMark:Show()
    else
        icon.glow:SetVertexColor(r * 0.5, g * 0.3, b * 0.3, 0.6)
        icon.questMark:Hide()
    end

    local size = 22 + (nemesis.rank or 1)
    icon:SetWidth(size)
    icon:SetHeight(size)

    -- Override target frame name with nemesis title
    if unit == "target" and TargetFrameTextureFrameName then
        local title = nemesis.nemesisTitle or ""
        if title ~= "" then
            TargetFrameTextureFrameName:SetText(title)
        end
    end

    icon:Show()
end

function WM:RefreshPortraitIcons()
    updatePortraitIcon(targetIcon, "target")
    updatePortraitIcon(focusIcon, "focus")
end

function WM:CreatePortraitIcons()
    if not targetIcon and TargetFrame then
        targetIcon = createPortraitIcon(TargetFrame, TargetFramePortrait)
    end
    if not focusIcon and FocusFrame then
        focusIcon = createPortraitIcon(FocusFrame, FocusFramePortrait)
    end

    -- Hook TargetFrame_Update so our name override runs AFTER the game sets the name
    if TargetFrame_Update and not self.targetFrameHooked then
        self.targetFrameHooked = true
        hooksecurefunc("TargetFrame_Update", function()
            if not targetIcon or not UnitExists("target") then
                return
            end
            local nemesis = findNemesisByUnit("target")
            if nemesis then
                local title = nemesis.nemesisTitle or ""
                if title ~= "" and TargetFrameTextureFrameName then
                    TargetFrameTextureFrameName:SetText(title)
                end
            end
        end)
    end
end

----------------------------------------------------------------
-- Minimap button
----------------------------------------------------------------

function WM:CreateMinimapButton()
    if self.minimapButton then
        return
    end

    local button = CreateFrame("Button", "NemesisMinimapButton", Minimap)
    button:SetWidth(32)
    button:SetHeight(32)
    button:SetFrameStrata("MEDIUM")
    button:SetFrameLevel(8)

    button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

    local overlay = button:CreateTexture(nil, "OVERLAY")
    overlay:SetWidth(54)
    overlay:SetHeight(54)
    overlay:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    overlay:SetPoint("TOPLEFT", button, "TOPLEFT", 0, 0)

    local icon = button:CreateTexture(nil, "BACKGROUND")
    icon:SetWidth(20)
    icon:SetHeight(20)
    icon:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")
    icon:SetPoint("CENTER", button, "CENTER", 0, 0)
    button.icon = icon

    -- Position around minimap edge
    local angle = NT.db.minimapButtonAngle or 220
    local rad = math.rad(angle)
    local radius = 80
    button:SetPoint("CENTER", Minimap, "CENTER",
        math.cos(rad) * radius, math.sin(rad) * radius)

    -- Drag to reposition around minimap
    button:EnableMouse(true)
    button:RegisterForDrag("LeftButton")
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button.dragging = false

    button:SetScript("OnDragStart", function(self)
        self.dragging = true
    end)

    button:SetScript("OnDragStop", function(self)
        self.dragging = false
    end)

    button:SetScript("OnUpdate", function(self)
        if not self.dragging then
            return
        end
        local mx, my = Minimap:GetCenter()
        local cx, cy = GetCursorPosition()
        local scale = Minimap:GetEffectiveScale()
        cx = cx / scale
        cy = cy / scale
        local dx = cx - mx
        local dy = cy - my
        local newAngle = math.deg(math.atan2(dy, dx))
        NT.db.minimapButtonAngle = newAngle
        local newRad = math.rad(newAngle)
        self:ClearAllPoints()
        self:SetPoint("CENTER", Minimap, "CENTER",
            math.cos(newRad) * radius, math.sin(newRad) * radius)
    end)

    button:SetScript("OnClick", function(self, clickType)
        if clickType == "RightButton" then
            -- Right-click: toggle world map (moved from left-click).
            if WorldMapFrame and WorldMapFrame:IsShown() then
                HideUIPanel(WorldMapFrame)
            else
                if SetMapToCurrentZone then
                    SetMapToCurrentZone()
                end
                ShowUIPanel(WorldMapFrame)
            end
        else
            -- Left-click: open the BountyBoard journal.
            if NT.BountyBoard and NT.BountyBoard.Toggle then
                NT.BountyBoard:Toggle()
            end
        end
    end)

    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("|cffff4444Немезида|r")
        GameTooltip:AddLine("ЛКМ: открыть журнал охотника", 1, 1, 1)
        GameTooltip:AddLine("ПКМ: открыть карту мира", 1, 1, 1)
        GameTooltip:AddLine("Перетащите для перемещения", 0.7, 0.7, 0.7)
        GameTooltip:Show()
    end)

    button:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)

    self.minimapButton = button
end

----------------------------------------------------------------
-- Initialize
----------------------------------------------------------------

function WM:Initialize()
    if self.initialized then
        return
    end
    self.initialized = true

    if NT.db then
        if NT.db.showPins ~= nil then pinsVisible = NT.db.showPins end
        if NT.db.showList ~= nil then listVisible = NT.db.showList end
    end

    self:HookTooltips()
    self:CreatePortraitIcons()

    self:CreateMinimapButton()

    if WorldMapFrame then
        -- ScrollFrame isolates pin children from WorldMapButton's frame management
        local scrollFrame = CreateFrame("ScrollFrame", "NemesisPinScrollFrame", WorldMapButton)
        scrollFrame:SetAllPoints(WorldMapButton)
        scrollFrame:SetFrameStrata("FULLSCREEN")
        scrollFrame:SetFrameLevel(WorldMapButton:GetFrameLevel() + 1)

        pinOverlay = CreateFrame("Frame", "NemesisPinOverlay", scrollFrame)
        pinOverlay:SetWidth(WorldMapButton:GetWidth())
        pinOverlay:SetHeight(WorldMapButton:GetHeight())
        scrollFrame:SetScrollChild(pinOverlay)

        createListPanel()

        WorldMapFrame:HookScript("OnShow", function()
            updatePlayerZoneCache()
            if listPanel then
                if listVisible then
                    listPanel:Show()
                    listPanel.toggleListButton.icon:SetDesaturated(false)
                    listPanel.toggleListButton.icon:SetAlpha(1.0)
                else
                    listPanel:Hide()
                    listPanel.toggleListButton.icon:SetDesaturated(true)
                    listPanel.toggleListButton.icon:SetAlpha(0.4)
                end
                if pinsVisible then
                    listPanel.togglePinsButton.icon:SetDesaturated(false)
                    listPanel.togglePinsButton.icon:SetAlpha(1.0)
                else
                    listPanel.togglePinsButton.icon:SetDesaturated(true)
                    listPanel.togglePinsButton.icon:SetAlpha(0.4)
                end
            end
            WM:RefreshWorldMap()
        end)

        hooksecurefunc("WorldMapFrame_Update", function()
            if not hoveredPin then
                WM:RefreshWorldMap()
            end
        end)
    end
end
