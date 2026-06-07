-- BountyBoard.lua — Blizzard-styled two-tab journal for the Hunter's Covenant.
-- Opens from left-click on the minimap button. Tab «Охота» shows the active
-- bounty + current-zone nemeses + reputation progress. Tab «Хроника» shows
-- completed bounties fetched on-demand from the server (HIST opcode).

NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

NT.BountyBoard = NT.BountyBoard or {}
local BB = NT.BountyBoard

-- Rank metadata — mirrors server NemesisReputation tiers. Thresholds here
-- are for display only; the server is the source of truth for the rank.
local RANK_NAMES = {
    [1] = "Послушник",
    [2] = "Охотник",
    [3] = "Следопыт",
    [4] = "Ветеран Охоты",
    [5] = "Легенда Охоты",
}

local RANK_THRESHOLDS = { 0, 500, 2500, 8000, 20000 }

-- Tier colors mirror Blizz FACTION_BAR_COLORS so the rep bar reads as a
-- native rep bar (Neutral → Friendly → Honored → Revered → Exalted).
local function rankColorByTier(rank)
    if rank >= 5 then return 0.00, 0.60, 0.10 end -- exalted deep green
    if rank >= 4 then return 0.00, 0.39, 0.00 end -- revered
    if rank >= 3 then return 0.00, 0.25, 0.59 end -- honored-blue
    if rank >= 2 then return 0.20, 0.60, 0.20 end -- friendly
    return 0.60, 0.60, 0.60                       -- neutral grey
end

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

local RANK_TIER_RU = {
    Marked     = "Меченый",
    Hated      = "Ненавистный",
    Relentless = "Неумолимый",
    Legendary  = "Легендарный",
    Mythic     = "Мифический",
}

local THREAT_RU = {
    low     = "слабая",
    medium  = "средняя",
    high    = "высокая",
    extreme = "смертельная",
}

local function translateAffixes(text)
    if not text or text == "" or text == "None" then
        return "нет"
    end
    local parts = {}
    for word in string.gmatch(text, "[^,%s]+") do
        table.insert(parts, AFFIX_RU[word] or word)
    end
    return table.concat(parts, ", ")
end

local function formatExpiryCountdown(expiresAt, now)
    if not expiresAt or expiresAt <= 0 then
        return "—"
    end
    local remain = expiresAt - now
    if remain <= 0 then
        return "|cffff5050истёк|r"
    end
    local hours = math.floor(remain / 3600)
    local minutes = math.floor((remain % 3600) / 60)
    if hours > 0 then
        return string.format("%dч %dм", hours, minutes)
    end
    return string.format("%dм", minutes)
end

-- Strips the Blizz title template's "%s" placeholder + surrounding
-- whitespace so we show just "Охотник" rather than "Охотник %s". Defined
-- early because Create() references it before BuildTitleMenu's file slot.
local function cleanTitleName(name)
    if not name then return "" end
    name = name:gsub("%%s", "")
    name = name:gsub("^%s+", ""):gsub("%s+$", "")
    return name
end

local function formatTimestamp(ts)
    if not ts or ts <= 0 then
        return "—"
    end
    local diff = (NT:GetNow() or time()) - ts
    if diff < 60          then return "только что"      end
    if diff < 3600        then return string.format("%d мин. назад", math.floor(diff / 60))    end
    if diff < 86400       then return string.format("%d ч. назад",   math.floor(diff / 3600))  end
    if diff < 86400 * 30  then return string.format("%d дн. назад",  math.floor(diff / 86400)) end
    return date("%d.%m.%Y", ts)
end

----------------------------------------------------------------
-- Backdrop helper (matches NT.UI style)
----------------------------------------------------------------

-- Book body backdrop: Blizzard spellbook parchment. Rendered as 4 quadrant
-- child textures (UI-SpellbookPanel-*) — that's how the real spellbook
-- composes its torn-parchment page. Ornate gold border stays via SetBackdrop.
local SPELLBOOK_QUADS = {
    "TopLeft", "TopRight", "BotLeft", "BotRight",
}

local function layoutQuads(quads, frame, inset)
    inset = inset or 3
    quads[1]:SetPoint("TOPLEFT",     frame, "TOPLEFT",     inset, -inset)
    quads[1]:SetPoint("BOTTOMRIGHT", frame, "CENTER",      0, 0)
    quads[2]:SetPoint("TOPLEFT",     frame, "TOP",         0, -inset)
    quads[2]:SetPoint("BOTTOMRIGHT", frame, "RIGHT",      -inset, 0)
    quads[3]:SetPoint("TOPLEFT",     frame, "LEFT",        inset, 0)
    quads[3]:SetPoint("BOTTOMRIGHT", frame, "BOTTOM",      0, inset)
    quads[4]:SetPoint("TOPLEFT",     frame, "CENTER",      0, 0)
    quads[4]:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -inset, inset)
end

local function addSpellbookParchment(frame, inset)
    local quads = {}
    for i, pos in ipairs(SPELLBOOK_QUADS) do
        local tex = frame:CreateTexture(nil, "BACKGROUND")
        tex:SetTexture("Interface\\Spellbook\\UI-SpellbookPanel-" .. pos)
        quads[i] = tex
    end
    layoutQuads(quads, frame, inset or 4)
    frame._parchment = quads
    return quads
end

-- Outer frame still uses the spellbook parchment fill for now, but all
-- the sub-panels are borderless — they're just invisible layout regions
-- sitting directly on the FriendsFrame chrome body.
local function setBookBackdrop(frame)
    frame:SetBackdrop(nil)
end

local function setPageBackdrop(frame)
    frame:SetBackdrop(nil)
end

local function setListBackdrop(frame)
    frame:SetBackdrop(nil)
end

----------------------------------------------------------------
-- Classic loading-screen artwork lookup
-- Stock 3.3.5a textures at `Interface\Glues\LoadingScreens\`.
-- Zones route to their continent's cinematic panel; the few cities with
-- dedicated loading screens in 3.3.5a get their own override.
----------------------------------------------------------------

local LOADSCREEN_DIR = "Interface\\LoadingScreens\\"

-- Per-zone dedicated loading screens. Keys are GetMapInfo() file names.
-- Only zones confirmed to ship with a dedicated LoadScreen_* BLP in 3.3.5a.
local CITY_LOADSCREEN = {
    StormwindCity    = "LoadScreenStormwindCity",
    OrgrimmarCity    = "LoadScreenOrgrimmarCity",
    IronforgeCity    = "LoadScreenIronforgeCity",
    UndercityCity    = "LoadScreenUndercityCity",
    Darnassus        = "LoadScreenDarnassusCity",
    ThunderbluffCity = "LoadScreenThunderBluffCity",
    ShattrathCity    = "LoadScreenShattrathCity",
    DalaranCity      = "LoadScreenDalaran",
}

-- Northrend zones (continent LoadScreenNorthrend).
local NORTHREND_ZONES = {
    BoreanTundra=1, HowlingFjord=1, Dragonblight=1, GrizzlyHills=1,
    ZulDrak=1, SholazarBasin=1, StormPeaks=1, TheStormPeaks=1,
    Icecrown=1, IceCrown=1, CrystalsongForest=1, DalaranCity=1,
    Wintergrasp=1,
}

-- Outland zones (continent LoadScreenOutland).
local OUTLAND_ZONES = {
    HellfirePeninsula=1, Zangarmarsh=1, Nagrand=1,
    TerokkarForest=1, Terokkar=1, ShadowmoonValley=1,
    BladesEdgeMountains=1, BladesEdge=1, Netherstorm=1,
    ShattrathCity=1, IsleofQuelDanas=1, QuelDanas=1,
}

local function resolveZoneLoadScreen(mapFile)
    if not mapFile or mapFile == "" then return nil end

    local override = CITY_LOADSCREEN[mapFile]
    if override then
        return LOADSCREEN_DIR .. override
    end

    if NORTHREND_ZONES[mapFile] then
        return LOADSCREEN_DIR .. "LoadScreenNorthrend"
    end
    if OUTLAND_ZONES[mapFile] then
        return LOADSCREEN_DIR .. "LoadScreenOutland"
    end

    -- Default: Eastern Kingdoms / Kalimdor pre-Cata panel.
    return LOADSCREEN_DIR .. "LoadScreenAzeroth"
end

----------------------------------------------------------------
-- Cached history (populated by HIST stream from server)
----------------------------------------------------------------

BB.history = BB.history or {}
BB.historyPending = false

function BB:BeginHistory()
    wipe(self.history)
    self.historyPending = true
end

function BB:AddHistoryEntry(fields)
    -- fields[1]=V2, fields[2]=HIST_ENTRY, then: spawnId, title, rank, completedAt, tokens
    table.insert(self.history, {
        spawnId     = tonumber(fields[3]) or 0,
        title       = fields[4] or "",
        rank        = tonumber(fields[5]) or 1,
        completedAt = tonumber(fields[6]) or 0,
        tokens      = tonumber(fields[7]) or 0,
    })
end

function BB:FinalizeHistory()
    self.historyPending = false
    if self.frame and self.frame:IsShown() and self.activeTab == 3 then
        self:RenderHistoryTab()
    end
end

function BB:RequestHistory()
    self:BeginHistory()
    if NT.SendServerCommand then
        NT:SendServerCommand(".nemesis addon history")
    end
end

----------------------------------------------------------------
-- Frame construction
----------------------------------------------------------------

local ROW_HEIGHT = 15
local MAX_ZONE_ROWS = 7
local MAX_HISTORY_ROWS = 40
-- FriendsFrame chrome is 4×(256x256). At width 384 the top/bottom pairs
-- overlap 128px horizontally (as designed). We shrink the height so the
-- top and bottom halves overlap vertically — removes the empty vertical
-- dead space below the content.
local FRAME_WIDTH = 384
local FRAME_HEIGHT = 512

-- Layout constants — lifted from FriendsFrame.xml so content aligns to the
-- chrome art's inset area. Frame is 384x512.
-- Tabs: (30, -64) from TOPLEFT.
-- Content: stock FriendsFrame uses (21, -100)→(-61, 105) = 302 wide.
-- We shrink the inner content width by ~20% and center it so panels
-- don't stretch edge-to-edge of the chrome.
local TAB_X         = 30
local TAB_Y         = -64
local CONTENT_LEFT  = 25
local CONTENT_TOP   = -100
local CONTENT_RIGHT = -91
local CONTENT_BOTTOM = 40

function BB:Create()
    if self.frame then
        return self.frame
    end

    local frame = CreateFrame("Frame", "NemesisBountyBoardFrame", UIParent)
    frame:SetWidth(FRAME_WIDTH)
    frame:SetHeight(FRAME_HEIGHT)
    frame:SetPoint("CENTER", UIParent, "CENTER", -220, 0)
    frame:SetFrameStrata("DIALOG")
    frame:SetToplevel(true)           -- matches FriendsFrame's toplevel="true"
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) self:StartMoving() end)
    frame:SetScript("OnDragStop",  function(self) self:StopMovingOrSizing() end)
    frame:Hide()
    self.frame = frame

    -- FriendsFrame chrome: 4 quadrant textures compose the dark-metal frame.
    -- Sizes come straight from FriendsFrame.xml — LEFT quads are 256 wide
    -- (full half), RIGHT quads are only 128 wide. 256 + 128 = 384 = frame
    -- width with zero overlap.
    local function addFriendsChrome(f)
        local base = "Interface\\FriendsFrame\\UI-FriendsFrame-"
        local tl = f:CreateTexture(nil, "ARTWORK")
        tl:SetTexture(base .. "TopLeft-bnet")
        tl:SetWidth(256); tl:SetHeight(256)
        tl:SetPoint("TOPLEFT", f, "TOPLEFT", 0, 0)

        local tr = f:CreateTexture(nil, "ARTWORK")
        tr:SetTexture(base .. "TopRight-bnet")
        tr:SetWidth(128); tr:SetHeight(256)  -- stock XML: 128x256
        tr:SetPoint("TOPRIGHT", f, "TOPRIGHT", 0, 0)

        local bl = f:CreateTexture(nil, "ARTWORK")
        bl:SetTexture(base .. "BotLeft-bnet")
        bl:SetWidth(256); bl:SetHeight(256)
        bl:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 0, 0)

        local br = f:CreateTexture(nil, "ARTWORK")
        br:SetTexture(base .. "BotRight-bnet")
        br:SetWidth(128); br:SetHeight(256)  -- stock XML: 128x256
        br:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", 0, 0)
    end
    addFriendsChrome(frame)

    -- Close button — stock FriendsFrame offset (-30, -8) so it sits inside
    -- the top-right recess baked into the chrome art.
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -30, -8)
    frame.close = close

    -- Refresh (force-sync) — sits to the left of the close button.
    local refresh = CreateFrame("Button", nil, frame)
    refresh:SetWidth(22); refresh:SetHeight(22)
    refresh:SetPoint("TOPRIGHT", close, "TOPLEFT", -6, 0)
    refresh.icon = refresh:CreateTexture(nil, "ARTWORK")
    refresh.icon:SetAllPoints(refresh)
    refresh.icon:SetTexture("Interface\\Buttons\\UI-RefreshButton")
    refresh:SetScript("OnClick", function()
        if NT.RefreshFromSources then NT:RefreshFromSources() end
        if BB.activeTab == 3 then BB:RequestHistory() end
        BB:Refresh()
    end)
    refresh:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Обновить")
        GameTooltip:AddLine("Синхронизация с сервером", 0.8, 0.8, 0.8)
        GameTooltip:Show()
    end)
    refresh:SetScript("OnLeave", function() GameTooltip:Hide() end)

    -- Brown spellbook icon on top of the FriendsFrame scroll icon (baked
    -- into the TopLeft chrome). This stock texture already ships with its
    -- own dark/leather surround, so no extra backdrop is needed.
    local portrait = frame:CreateTexture(nil, "ARTWORK")
    portrait:SetWidth(59); portrait:SetHeight(59)
    portrait:SetPoint("CENTER", frame, "TOPLEFT", 38, -35)
    portrait:SetTexture("Interface\\FriendsFrame\\FriendsFrameScrollIcon")
    portrait:SetVertexColor(1, 1, 1)
    frame.portrait = portrait

    -- Title centered in the FriendsFrame header strip (matches stock y=-14).
    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    title:SetPoint("TOP", frame, "TOP", 0, -18)
    title:SetText("Журнал Охотника")
    title:SetTextColor(1.0, 0.82, 0.0)

    -- Active-title picker — compact dropdown with a left-side label.
    local titleLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    -- Title picker right-anchored with a touch of top padding so it sits
    -- opposite the portrait and below the close-button row.
    titleLabel:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -160, -52)
    titleLabel:SetText("|cffffd100Титул:|r")
    frame.titleLabel = titleLabel

    local titleDD = CreateFrame("Frame", "NemesisBountyBoardTitleDropdown",
        frame, "UIDropDownMenuTemplate")
    titleDD:SetPoint("LEFT", titleLabel, "RIGHT", -8, 2)
    UIDropDownMenu_SetWidth(titleDD, 96)
    UIDropDownMenu_JustifyText(titleDD, "CENTER")
    frame.titleDD = titleDD

    UIDropDownMenu_Initialize(titleDD, BB.BuildTitleMenu)
    UIDropDownMenu_SetText(titleDD, "«нет»")

    -- Content area — matches FriendsFrame's scroll-frame inset exactly.
    frame.tabContent = {}
    for i = 1, 3 do
        local content = CreateFrame("Frame", nil, frame)
        content:SetPoint("TOPLEFT",     frame, "TOPLEFT",
            CONTENT_LEFT, CONTENT_TOP)
        content:SetPoint("TOPRIGHT",    frame, "TOPRIGHT",
            CONTENT_RIGHT, CONTENT_TOP)
        content:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT",
            CONTENT_RIGHT, CONTENT_BOTTOM)
        content:SetPoint("BOTTOMLEFT",  frame, "BOTTOMLEFT",
            CONTENT_LEFT, CONTENT_BOTTOM)
        content:Hide()
        frame.tabContent[i] = content
    end

    -- ── Rep footer — persistent rank + progress bar across the frame bottom.
    -- Rank text sits at the bottom-LEFT.
    local repRankText = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    repRankText:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 30, 86)
    frame.repRankText = repRankText

    -- Bar container + fill at the bottom-RIGHT (center-right of the footer).
    local barFrame = CreateFrame("Frame", nil, frame)
    barFrame:SetSize(170, 16)
    barFrame:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -50, 82)
    barFrame:SetBackdrop({
        bgFile   = "Interface\\Buttons\\WHITE8X8",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        edgeSize = 10,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    barFrame:SetBackdropColor(0.06, 0.04, 0.02, 0.85)
    barFrame:SetBackdropBorderColor(0.55, 0.40, 0.22)

    local repBar = CreateFrame("StatusBar", nil, barFrame)
    repBar:SetPoint("TOPLEFT",     barFrame, "TOPLEFT",      3, -3)
    repBar:SetPoint("BOTTOMRIGHT", barFrame, "BOTTOMRIGHT", -3,  3)
    repBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    repBar:SetStatusBarColor(0.2, 0.6, 0.2)
    repBar:SetMinMaxValues(0, 1)
    repBar:SetValue(0)
    frame.repBar = repBar

    local repBarLabel = repBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    repBarLabel:SetPoint("CENTER", repBar, "CENTER", 0, 0)
    repBarLabel:SetTextColor(1, 1, 1)
    frame.repBarLabel = repBarLabel

    -- Sub-tabs — exact FriendsFrame offsets (30, -64 from TOPLEFT).
    local tabA = CreateFrame("Button", "NemesisBountyBoardTab1", frame,
        "TabButtonTemplate")
    tabA:SetPoint("TOPLEFT", frame, "TOPLEFT", TAB_X, TAB_Y)
    tabA:SetText("Охота")
    tabA:SetID(1)
    tabA:SetScript("OnClick", function(self) BB:SelectTab(self:GetID()) end)
    if PanelTemplates_TabResize then PanelTemplates_TabResize(tabA, 0) end

    local tabB = CreateFrame("Button", "NemesisBountyBoardTab2", frame,
        "TabButtonTemplate")
    tabB:SetPoint("LEFT", tabA, "RIGHT", 4, 0)
    tabB:SetText("Немезиды")
    tabB:SetID(2)
    tabB:SetScript("OnClick", function(self) BB:SelectTab(self:GetID()) end)
    if PanelTemplates_TabResize then PanelTemplates_TabResize(tabB, 0) end

    local tabC = CreateFrame("Button", "NemesisBountyBoardTab3", frame,
        "TabButtonTemplate")
    tabC:SetPoint("LEFT", tabB, "RIGHT", 4, 0)
    tabC:SetText("Хроника")
    tabC:SetID(3)
    tabC:SetScript("OnClick", function(self) BB:SelectTab(self:GetID()) end)
    if PanelTemplates_TabResize then PanelTemplates_TabResize(tabC, 0) end

    frame.tabs = { tabA, tabB, tabC }

    -- Build per-tab content (Hunt=1, Zone=2, History=3).
    self:BuildHuntTab(frame.tabContent[1])
    self:BuildZoneTab(frame.tabContent[2])
    self:BuildHistoryTab(frame.tabContent[3])

    self:SelectTab(1)
    self:StartRefreshTimer()
    return frame
end

-- Manual tab visual switching — avoids PanelTemplates_SetTab's requirement
-- that tabs be named "<FrameName>Tab<N>" (stock 3.3.5a quirk).
local function updateTabVisual(tab, selected)
    if not tab then return end
    if selected then
        PanelTemplates_SelectTab(tab)
    else
        PanelTemplates_DeselectTab(tab)
    end
end

function BB:SelectTab(index)
    if not self.frame then return end
    self.activeTab = index
    for i, tab in ipairs(self.frame.tabs or {}) do
        updateTabVisual(tab, i == index)
    end
    for i, content in ipairs(self.frame.tabContent) do
        if i == index then content:Show() else content:Hide() end
    end
    -- History tab (3) fetches lazily on first open.
    if index == 3 and not self.historyPending and #self.history == 0 then
        self:RequestHistory()
    end
    self:Refresh()
end

----------------------------------------------------------------
-- Tab 1: Охота — active bounty + zone nemeses + rep bar
----------------------------------------------------------------

function BB:BuildHuntTab(parent)
    -- Reputation display has moved to the frame footer (see BB:BuildRepFooter);
    -- this tab's content starts directly with the Active Contract panel.

    -- Active bounty parchment panel — anchored directly below the rep box
    -- now that the active-title picker has moved to the frame header.
    local bountyBox = CreateFrame("Frame", nil, parent)
    bountyBox:SetPoint("TOPLEFT",  parent, "TOPLEFT",  0, 0)
    bountyBox:SetPoint("TOPRIGHT", parent, "TOPRIGHT", 0, 0)
    bountyBox:SetHeight(72)
    setPageBackdrop(bountyBox)
    parent.bountyBox = bountyBox

    bountyBox.header = bountyBox:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    bountyBox.header:SetPoint("TOPLEFT", bountyBox, "TOPLEFT", 0, -5)
    bountyBox.header:SetText("|cffffd100Активный контракт|r")

    bountyBox.title = bountyBox:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    bountyBox.title:SetPoint("TOPLEFT", bountyBox, "TOPLEFT", 0, -18)
    bountyBox.title:SetPoint("TOPRIGHT", bountyBox, "TOPRIGHT", 0, -18)
    bountyBox.title:SetJustifyH("LEFT")

    bountyBox.details = bountyBox:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    bountyBox.details:SetPoint("TOPLEFT", bountyBox, "TOPLEFT", 0, -34)
    bountyBox.details:SetPoint("BOTTOMRIGHT", bountyBox, "BOTTOMRIGHT", 0, 4)
    bountyBox.details:SetJustifyH("LEFT")
    bountyBox.details:SetJustifyV("TOP")
    bountyBox.details:SetWordWrap(true)

    -- Особое поручение — epic-quest styled panel below the active contract.
    local taskBox = CreateFrame("Frame", nil, parent)
    taskBox:SetPoint("TOPLEFT",  bountyBox, "BOTTOMLEFT",  0, -6)
    taskBox:SetPoint("TOPRIGHT", bountyBox, "BOTTOMRIGHT", 0, -6)
    taskBox:SetHeight(58)
    setPageBackdrop(taskBox)
    parent.taskBox = taskBox

    taskBox.header = taskBox:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    taskBox.header:SetPoint("TOPLEFT", taskBox, "TOPLEFT", 0, -5)
    taskBox.header:SetText("|cffa335eeОсобое поручение|r")

    taskBox.title = taskBox:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    taskBox.title:SetPoint("TOPLEFT", taskBox, "TOPLEFT", 0, -18)
    taskBox.title:SetPoint("TOPRIGHT", taskBox, "TOPRIGHT", 0, -18)
    taskBox.title:SetJustifyH("LEFT")

    taskBox.details = taskBox:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    taskBox.details:SetPoint("TOPLEFT", taskBox, "TOPLEFT", 0, -32)
    taskBox.details:SetPoint("BOTTOMRIGHT", taskBox, "BOTTOMRIGHT", 0, 4)
    taskBox.details:SetJustifyH("LEFT")
    taskBox.details:SetJustifyV("TOP")
    taskBox.details:SetWordWrap(true)

    -- Live countdown for the timed task: re-render once a second while the
    -- hunt tab is visible (taskBox is hidden together with the tab content).
    taskBox._acc = 0
    taskBox:SetScript("OnUpdate", function(frame, elapsed)
        frame._acc = frame._acc + elapsed
        if frame._acc < 1 then return end
        frame._acc = 0
        local task = NT.GetActiveSpecialTask and NT:GetActiveSpecialTask()
        if task and task.durationMin then
            BB:RenderSpecialTaskBox(parent)
        end
    end)
end

-- Renders only the special-task panel (called from RenderHuntTab and the
-- 1-second countdown ticker).
function BB:RenderSpecialTaskBox(content)
    local taskBox = content and content.taskBox
    if not taskBox then return end

    local task = NT.GetActiveSpecialTask and NT:GetActiveSpecialTask()
    if task then
        taskBox.title:SetText("|cffa335ee" .. (task.name or "Поручение") .. "|r")
        local detail = task.condition or ""
        if task.durationMin and task.acceptedAt then
            local left = task.acceptedAt + task.durationMin * 60 - time()
            if left < 0 then left = 0 end
            detail = detail .. string.format("\n|cffffd100Осталось: %d:%02d|r",
                math.floor(left / 60), math.floor(left % 60))
        end
        taskBox.details:SetText(detail)
    else
        taskBox.title:SetText("|cff808080Нет активного поручения|r")
        taskBox.details:SetText("|cffa0a0a0Спросите трактирщика об особом поручении.|r")
    end
end

----------------------------------------------------------------
-- Tab 2: Немезиды — current-zone nemesis list (scrollable)
----------------------------------------------------------------

local MAX_ZONE_ROWS_TAB = 40      -- plenty of slots; scroll handles overflow

function BB:BuildZoneTab(parent)
    -- Header inside the tab.
    local header = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    header:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, -5)
    header:SetText("|cffffd100Немезиды в этой зоне|r")
    parent.header = header

    -- Scroll frame filling the rest of the tab content area.
    local scroll = CreateFrame("ScrollFrame",
        "NemesisBountyBoardZoneScroll", parent, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT",     header, "BOTTOMLEFT",  0, -4)
    scroll:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", 26, 64)
    parent.scroll = scroll

    local content = CreateFrame("Frame", nil, scroll)
    content:SetSize(1, 1)                          -- size recalculated on render
    scroll:SetScrollChild(content)
    parent.scrollContent = content

    parent.rows = {}
    for i = 1, MAX_ZONE_ROWS_TAB do
        local row = CreateFrame("Button", nil, content)
        row:SetHeight(ROW_HEIGHT)
        row:SetPoint("TOPLEFT",  content, "TOPLEFT",  0, -((i - 1) * ROW_HEIGHT))
        row:SetPoint("RIGHT",    content, "RIGHT",    0, 0)
        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.text:SetPoint("LEFT", row, "LEFT", 2, 0)
        row.text:SetPoint("RIGHT", row, "RIGHT", -2, 0)
        row.text:SetJustifyH("LEFT")
        row.text:SetWordWrap(false)
        row:SetScript("OnClick", function(self)
            if self.spawnId and NT.SelectNemesis then
                NT:SelectNemesis(self.spawnId)
            end
        end)
        row:Hide()
        parent.rows[i] = row
    end

    parent.emptyText = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    parent.emptyText:SetPoint("CENTER", parent, "CENTER", 0, 0)
    parent.emptyText:SetText("Тишина в зоне. Никого не помечено.")
    parent.emptyText:Hide()
end

----------------------------------------------------------------
-- Title dropdown: pick which earned title to display on the nameplate
----------------------------------------------------------------

local TITLE_IDS = { 180, 181, 182, 183, 184 }

-- Menu builder — invoked by UIDropDownMenu_Initialize each time the popup
-- opens. Called as a method (self = the menu frame), so the signature
-- is (self, level, menuList). We ignore all args and rebuild from scratch.
function BB.BuildTitleMenu(_, level)
    local current = GetCurrentTitle and GetCurrentTitle() or -1

    local info = UIDropDownMenu_CreateInfo()
    info.text = "«снять»"
    info.checked = (current == -1)
    info.func = function()
        SetCurrentTitle(-1)
        BB:RefreshTitleButton()
        CloseDropDownMenus()
    end
    UIDropDownMenu_AddButton(info, level)

    for _, titleId in ipairs(TITLE_IDS) do
        if IsTitleKnown and IsTitleKnown(titleId) == 1 then
            local name = cleanTitleName(GetTitleName and GetTitleName(titleId))
            if name ~= "" then
                local id = titleId
                info = UIDropDownMenu_CreateInfo()
                info.text = name
                info.checked = (current == id)
                info.func = function()
                    SetCurrentTitle(id)
                    BB:RefreshTitleButton()
                    CloseDropDownMenus()
                end
                UIDropDownMenu_AddButton(info, level)
            end
        end
    end
end

function BB:RefreshTitleButton()
    if not self.frame or not self.frame.titleDD then return end

    local current = GetCurrentTitle and GetCurrentTitle() or -1
    local text = "«нет»"
    if current and current > 0 and GetTitleName then
        local name = cleanTitleName(GetTitleName(current))
        if name ~= "" then text = name end
    end
    UIDropDownMenu_SetText(self.frame.titleDD, text)
end

function BB:FindActiveBountyNemesis()
    local activeTitle = NT.GetActiveBountyTitle and NT:GetActiveBountyTitle()
    if not activeTitle or activeTitle == "" then
        return nil
    end
    for _, nemesis in pairs(NT.data.nemeses or {}) do
        if nemesis.nemesisTitle == activeTitle then
            return nemesis
        end
    end
    return nil
end

-- Pulls repPoints/repRank from whichever nemesis entry has them populated.
-- Server writes them onto every outgoing addon payload per player, so any
-- stored entry's rep fields are current.
function BB:ReadRepSnapshot()
    local points, rank = 0, 1
    for _, nemesis in pairs(NT.data.nemeses or {}) do
        if nemesis.repPoints and nemesis.repPoints > points then
            points = nemesis.repPoints
        end
        if nemesis.repRank and nemesis.repRank > rank then
            rank = nemesis.repRank
        end
    end
    return points, rank
end

function BB:RenderHuntTab()
    local content = self.frame.tabContent[1]
    if not content then return end

    -- Title picker
    self:RefreshTitleButton()

    -- Reputation footer (frame-level widgets, shared across tabs).
    local frame = self.frame
    local points, rank = self:ReadRepSnapshot()
    local rankName = RANK_NAMES[rank] or "—"
    local r, g, b = rankColorByTier(rank)
    if frame.repRankText then
        frame.repRankText:SetText(string.format("|cff%02x%02x%02xРанг %d — %s|r",
            math.floor(r * 255), math.floor(g * 255), math.floor(b * 255),
            rank, rankName))
    end

    if frame.repBar then
        if rank >= 5 then
            frame.repBar:SetValue(1)
            if frame.repBarLabel then
                frame.repBarLabel:SetText(string.format("%d (макс)", points))
            end
        else
            local nextThreshold = RANK_THRESHOLDS[rank + 1] or (points + 1)
            local curThreshold  = RANK_THRESHOLDS[rank] or 0
            local span = math.max(1, nextThreshold - curThreshold)
            local progress = math.min(1, math.max(0, (points - curThreshold) / span))
            local curInTier = points - curThreshold
            frame.repBar:SetValue(progress)
            if frame.repBarLabel then
                frame.repBarLabel:SetText(string.format("%d / %d", curInTier, span))
            end
        end
        frame.repBar:SetStatusBarColor(r, g, b)
    end

    -- Active bounty
    local bounty = self:FindActiveBountyNemesis()
    if bounty then
        content.bountyBox.title:SetText("|cffffd100" ..
            (bounty.nemesisTitle or bounty.name or "Немезида") .. "|r")
        local tierRu = RANK_TIER_RU[bounty.rankTier] or bounty.rankTier or "—"
        local threatRu = THREAT_RU[bounty.threatClass] or bounty.threatClass or "—"
        local expires = formatExpiryCountdown(bounty.expiresAt or 0, NT:GetNow() or time())
        content.bountyBox.details:SetText(string.format(
            "Ранг %d — %s\nСпособности: %s\nУгроза: %s\nИстекает через: %s",
            bounty.rank or 1, tierRu,
            translateAffixes(bounty.affixText),
            threatRu,
            expires))
    else
        content.bountyBox.title:SetText("|cff808080Нет активного контракта|r")
        content.bountyBox.details:SetText(
            "|cffa0a0a0Посетите трактирщика, чтобы принять охоту на голову.|r")
    end

    -- Особое поручение
    self:RenderSpecialTaskBox(content)
end

function BB:RenderZoneTab()
    local content = self.frame.tabContent[2]
    if not content or not content.rows then return end

    -- Collect zone nemeses via WorldMap's locale-independent matcher.
    local zoneList = {}
    local inZone = NT.WorldMap and NT.WorldMap.IsNemesisInCurrentZone
    for _, nemesis in pairs(NT.data.nemeses or {}) do
        if inZone and inZone(nemesis) then
            table.insert(zoneList, nemesis)
        end
    end
    table.sort(zoneList, function(a, b)
        if (a.rank or 1) ~= (b.rank or 1) then
            return (a.rank or 1) > (b.rank or 1)
        end
        return (a.nemesisTitle or a.name or "") < (b.nemesisTitle or b.name or "")
    end)

    for i, row in ipairs(content.rows) do
        local nemesis = zoneList[i]
        if nemesis then
            local rr, gg, bb = rankColorByTier(math.min(5, math.ceil((nemesis.rank or 1) / 2 + 1)))
            local name = nemesis.nemesisTitle or nemesis.name or "—"
            row.spawnId = nemesis.spawnId
            row.text:SetText(string.format("|cff%02x%02x%02xR%d|r  %s",
                math.floor(rr * 255), math.floor(gg * 255), math.floor(bb * 255),
                nemesis.rank or 1, name))
            row:Show()
        else
            row.spawnId = nil
            row:Hide()
        end
    end

    -- Resize scroll content to match filled row count so the scroll bar
    -- reflects real overflow.
    if content.scrollContent then
        local rows = math.max(1, #zoneList)
        content.scrollContent:SetHeight(rows * ROW_HEIGHT)
        if content.scroll then
            content.scrollContent:SetWidth(content.scroll:GetWidth())
        end
    end

    if #zoneList == 0 then
        content.emptyText:Show()
    else
        content.emptyText:Hide()
    end
end

----------------------------------------------------------------
-- Tab 2: Хроника — bounty history
----------------------------------------------------------------

function BB:BuildHistoryTab(parent)
    local header = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    header:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, -5)
    header:SetText("|cffffd100Завершённые контракты|r")
    parent.header = header

    -- Scroll frame filling the rest of the tab content area (mirrors Zone tab).
    local scroll = CreateFrame("ScrollFrame",
        "NemesisBountyBoardHistoryScroll", parent, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT",     header, "BOTTOMLEFT",  0, -4)
    scroll:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", 26, 64)
    parent.scroll = scroll

    local content = CreateFrame("Frame", nil, scroll)
    content:SetSize(1, 1)                          -- size recalculated on render
    scroll:SetScrollChild(content)
    parent.scrollContent = content

    parent.rows = {}
    for i = 1, MAX_HISTORY_ROWS do
        local row = CreateFrame("Frame", nil, content)
        row:SetHeight(ROW_HEIGHT)
        row:SetPoint("TOPLEFT", content, "TOPLEFT", 0, -((i - 1) * ROW_HEIGHT))
        row:SetPoint("RIGHT",   content, "RIGHT",   0, 0)
        row.left = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.left:SetPoint("LEFT", row, "LEFT", 2, 0)
        row.left:SetJustifyH("LEFT")
        row.right = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.right:SetPoint("RIGHT", row, "RIGHT", -2, 0)
        row.right:SetJustifyH("RIGHT")
        row:Hide()
        parent.rows[i] = row
    end

    parent.emptyText = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    parent.emptyText:SetPoint("CENTER", parent, "CENTER", 0, 0)
    parent.emptyText:SetText("Ни одного завершённого контракта.")
    parent.emptyText:Hide()

    parent.loadingText = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    parent.loadingText:SetPoint("CENTER", parent, "CENTER", 0, 0)
    parent.loadingText:SetText("|cffa0a0a0Загрузка...|r")
    parent.loadingText:Hide()
end

function BB:RenderHistoryTab()
    local content = self.frame.tabContent[3]
    if not content then return end

    if self.historyPending then
        content.loadingText:Show()
        content.emptyText:Hide()
        for _, row in ipairs(content.rows) do row:Hide() end
        return
    end
    content.loadingText:Hide()

    for i, row in ipairs(content.rows) do
        local entry = self.history[i]
        if entry then
            local rr, gg, bb = rankColorByTier(math.min(5, math.ceil((entry.rank or 1) / 2 + 1)))
            row.left:SetText(string.format(
                "|cff%02x%02x%02xR%d|r  %s",
                math.floor(rr * 255), math.floor(gg * 255), math.floor(bb * 255),
                entry.rank or 1, entry.title))
            row.right:SetText(string.format(
                "|cffffd100%d жет.|r  |cffa0a0a0%s|r",
                entry.tokens or 0, formatTimestamp(entry.completedAt)))
            row:Show()
        else
            row:Hide()
        end
    end

    -- Resize scroll content so the scroll bar reflects real overflow.
    if content.scrollContent then
        local rows = math.max(1, #self.history)
        content.scrollContent:SetHeight(rows * ROW_HEIGHT)
        if content.scroll then
            content.scrollContent:SetWidth(content.scroll:GetWidth())
        end
    end

    if #self.history == 0 then
        content.emptyText:Show()
    else
        content.emptyText:Hide()
    end
end

----------------------------------------------------------------
-- Public API
----------------------------------------------------------------

function BB:Refresh()
    if not self.frame or not self.frame:IsShown() then return end
    -- Rep footer renders on every refresh (always visible).
    self:RenderHuntTab()
    if self.activeTab == 2 then
        self:RenderZoneTab()
    elseif self.activeTab == 3 then
        self:RenderHistoryTab()
    end
end

function BB:Toggle()
    self:Create()
    if self.frame:IsShown() then
        self.frame:Hide()
    else
        self.frame:Show()
        self:Refresh()
    end
end

function BB:Show()
    self:Create()
    self.frame:Show()
    self:Refresh()
end

-- Periodic refresh to update expiry countdowns while visible (cheap: ~1/sec
-- worth, throttled via a timer).
function BB:StartRefreshTimer()
    if self.refreshTimer then return end
    if not NT.ScheduleRepeatingTimer then return end
    self.refreshTimer = NT:ScheduleRepeatingTimer(function()
        if BB.frame and BB.frame:IsShown() and BB.activeTab == 1 then
            BB:RenderHuntTab()
        end
    end, 1)
end
