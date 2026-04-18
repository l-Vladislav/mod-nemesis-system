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

local function rankColorByTier(rank)
    if rank >= 5 then return 1.0, 0.82, 0.0  end -- gold
    if rank >= 4 then return 1.0, 0.45, 0.1  end -- orange
    if rank >= 3 then return 0.7, 0.5, 1.0   end -- purple
    if rank >= 2 then return 0.2, 0.9, 0.4   end -- green
    return 0.7, 0.7, 0.7                         -- grey
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

local function setDialogBackdrop(frame, alpha)
    frame:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    frame:SetBackdropColor(0, 0, 0, alpha or 0.9)
end

local function setParchmentBackdrop(frame)
    frame:SetBackdrop({
        bgFile = "Interface\\QuestFrame\\QuestBG",  -- parchment look
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 64, edgeSize = 10,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    frame:SetBackdropColor(1, 1, 1, 0.35)
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
    if self.frame and self.frame:IsShown() and self.activeTab == 2 then
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

local ROW_HEIGHT = 18
local MAX_ZONE_ROWS = 10
local MAX_HISTORY_ROWS = 14
local FRAME_WIDTH = 400
local FRAME_HEIGHT = 560

function BB:Create()
    if self.frame then
        return self.frame
    end

    local frame = CreateFrame("Frame", "NemesisBountyBoardFrame", UIParent)
    frame:SetWidth(FRAME_WIDTH)
    frame:SetHeight(FRAME_HEIGHT)
    frame:SetPoint("CENTER", UIParent, "CENTER", -220, 0)
    frame:SetFrameStrata("DIALOG")
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) self:StartMoving() end)
    frame:SetScript("OnDragStop",  function(self) self:StopMovingOrSizing() end)
    setDialogBackdrop(frame, 0.92)
    frame:Hide()
    self.frame = frame

    -- Portrait (skull) in top-left
    frame.portrait = frame:CreateTexture(nil, "ARTWORK")
    frame.portrait:SetWidth(44)
    frame.portrait:SetHeight(44)
    frame.portrait:SetPoint("TOPLEFT", frame, "TOPLEFT", 8, -6)
    frame.portrait:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")
    frame.portrait:SetVertexColor(1.0, 0.3, 0.3)

    -- Title
    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOP", frame, "TOP", 0, -14)
    title:SetText("Журнал Охотника")
    title:SetTextColor(1.0, 0.82, 0.0)

    -- Subtitle (RP tagline)
    local subtitle = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    subtitle:SetPoint("TOP", title, "BOTTOM", 0, -2)
    subtitle:SetText("|cffa0a0a0«Имена, начертанные кровью.»|r")

    -- Close button
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -4)

    -- Refresh icon (force-sync)
    local refresh = CreateFrame("Button", nil, frame)
    refresh:SetWidth(22); refresh:SetHeight(22)
    refresh:SetPoint("TOPRIGHT", close, "TOPLEFT", -2, 0)
    refresh.icon = refresh:CreateTexture(nil, "ARTWORK")
    refresh.icon:SetAllPoints(refresh)
    refresh.icon:SetTexture("Interface\\Buttons\\UI-RefreshButton")
    refresh:SetScript("OnClick", function()
        if NT.RefreshFromSources then NT:RefreshFromSources() end
        if BB.activeTab == 2 then BB:RequestHistory() end
        BB:Refresh()
    end)
    refresh:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Обновить")
        GameTooltip:AddLine("Синхронизация с сервером", 0.8, 0.8, 0.8)
        GameTooltip:Show()
    end)
    refresh:SetScript("OnLeave", function() GameTooltip:Hide() end)

    -- Content frames (one per tab)
    frame.tabContent = {}
    for i = 1, 2 do
        local content = CreateFrame("Frame", nil, frame)
        content:SetPoint("TOPLEFT", frame, "TOPLEFT", 12, -54)
        content:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -12, 38)
        content:Hide()
        frame.tabContent[i] = content
    end

    -- Tab buttons at bottom (Blizz CharacterFrame pattern)
    local tabA = CreateFrame("Button", "NemesisBountyBoardTab1", frame,
        "CharacterFrameTabButtonTemplate")
    tabA:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 10, -30)
    tabA:SetText("Охота")
    tabA:SetID(1)
    tabA:SetScript("OnClick", function(self) BB:SelectTab(self:GetID()) end)

    local tabB = CreateFrame("Button", "NemesisBountyBoardTab2", frame,
        "CharacterFrameTabButtonTemplate")
    tabB:SetPoint("LEFT", tabA, "RIGHT", -14, 0)
    tabB:SetText("Хроника")
    tabB:SetID(2)
    tabB:SetScript("OnClick", function(self) BB:SelectTab(self:GetID()) end)

    frame.tabs = { tabA, tabB }

    -- Build per-tab content
    self:BuildHuntTab(frame.tabContent[1])
    self:BuildHistoryTab(frame.tabContent[2])

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
    if index == 2 and not self.historyPending and #self.history == 0 then
        self:RequestHistory()
    end
    self:Refresh()
end

----------------------------------------------------------------
-- Tab 1: Охота — active bounty + zone nemeses + rep bar
----------------------------------------------------------------

function BB:BuildHuntTab(parent)
    -- Reputation bar at the top
    local repBox = CreateFrame("Frame", nil, parent)
    repBox:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, 0)
    repBox:SetPoint("TOPRIGHT", parent, "TOPRIGHT", 0, 0)
    repBox:SetHeight(46)
    setParchmentBackdrop(repBox)
    parent.repBox = repBox

    repBox.rankText = repBox:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    repBox.rankText:SetPoint("TOPLEFT", repBox, "TOPLEFT", 10, -6)

    repBox.pointsText = repBox:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    repBox.pointsText:SetPoint("TOPRIGHT", repBox, "TOPRIGHT", -10, -8)

    repBox.bar = CreateFrame("StatusBar", nil, repBox)
    repBox.bar:SetPoint("BOTTOMLEFT", repBox, "BOTTOMLEFT", 10, 8)
    repBox.bar:SetPoint("BOTTOMRIGHT", repBox, "BOTTOMRIGHT", -10, 8)
    repBox.bar:SetHeight(12)
    repBox.bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    repBox.bar:SetStatusBarColor(0.9, 0.7, 0.2)
    repBox.bar:SetMinMaxValues(0, 1)
    repBox.bar:SetValue(0)

    local barBg = repBox.bar:CreateTexture(nil, "BACKGROUND")
    barBg:SetAllPoints(repBox.bar)
    barBg:SetTexture(0, 0, 0, 0.5)

    -- Active-title picker strip: a plain button that opens a context-style
    -- dropdown. Much more reliable than UIDropDownMenuTemplate's own button
    -- in 3.3.5a (whose click target is only the tiny arrow).
    local titleBar = CreateFrame("Frame", nil, parent)
    titleBar:SetPoint("TOPLEFT", repBox, "BOTTOMLEFT", 0, -4)
    titleBar:SetPoint("TOPRIGHT", repBox, "BOTTOMRIGHT", 0, -4)
    titleBar:SetHeight(26)
    parent.titleBar = titleBar

    local titleLabel = titleBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    titleLabel:SetPoint("LEFT", titleBar, "LEFT", 6, 0)
    titleLabel:SetText("Активный титул:")

    local titleButton = CreateFrame("Button", nil, titleBar, "UIPanelButtonTemplate")
    titleButton:SetPoint("LEFT", titleLabel, "RIGHT", 6, 0)
    titleButton:SetPoint("RIGHT", titleBar, "RIGHT", -6, 0)
    titleButton:SetHeight(22)
    titleButton:SetText("«нет»")
    titleButton:SetScript("OnClick", function(self)
        if not BB._titleMenu then
            BB._titleMenu = CreateFrame("Frame",
                "NemesisBountyBoardTitleMenu", UIParent, "UIDropDownMenuTemplate")
        end
        UIDropDownMenu_Initialize(BB._titleMenu, BB.BuildTitleMenu, "MENU")
        ToggleDropDownMenu(1, nil, BB._titleMenu, self, 0, 0)
    end)
    parent.titleButton = titleButton

    -- Active bounty parchment panel
    local bountyBox = CreateFrame("Frame", nil, parent)
    bountyBox:SetPoint("TOPLEFT", titleBar, "BOTTOMLEFT", 0, -4)
    bountyBox:SetPoint("TOPRIGHT", titleBar, "BOTTOMRIGHT", 0, -4)
    bountyBox:SetHeight(100)
    setParchmentBackdrop(bountyBox)
    parent.bountyBox = bountyBox

    bountyBox.header = bountyBox:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    bountyBox.header:SetPoint("TOPLEFT", bountyBox, "TOPLEFT", 10, -6)
    bountyBox.header:SetText("|cffffd100Активный контракт|r")

    bountyBox.title = bountyBox:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    bountyBox.title:SetPoint("TOPLEFT", bountyBox, "TOPLEFT", 10, -22)
    bountyBox.title:SetPoint("TOPRIGHT", bountyBox, "TOPRIGHT", -10, -22)
    bountyBox.title:SetJustifyH("LEFT")

    bountyBox.details = bountyBox:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    bountyBox.details:SetPoint("TOPLEFT", bountyBox, "TOPLEFT", 10, -44)
    bountyBox.details:SetPoint("RIGHT", bountyBox, "RIGHT", -10, 0)
    bountyBox.details:SetJustifyH("LEFT")
    bountyBox.details:SetJustifyV("TOP")
    bountyBox.details:SetHeight(50)
    bountyBox.details:SetWordWrap(true)

    -- Zone nemesis list
    local listHeader = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    listHeader:SetPoint("TOPLEFT", bountyBox, "BOTTOMLEFT", 0, -10)
    listHeader:SetText("|cffffd100Немезиды в этой зоне|r")
    parent.listHeader = listHeader

    local listBg = CreateFrame("Frame", nil, parent)
    listBg:SetPoint("TOPLEFT", listHeader, "BOTTOMLEFT", 0, -4)
    listBg:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", 0, 0)
    setDialogBackdrop(listBg, 0.4)
    parent.listBg = listBg

    parent.rows = {}
    for i = 1, MAX_ZONE_ROWS do
        local row = CreateFrame("Button", nil, listBg)
        row:SetHeight(ROW_HEIGHT)
        row:SetPoint("TOPLEFT",  listBg, "TOPLEFT",  6, -(4 + (i - 1) * ROW_HEIGHT))
        row:SetPoint("TOPRIGHT", listBg, "TOPRIGHT", -6, -(4 + (i - 1) * ROW_HEIGHT))
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
    parent.emptyText:SetPoint("CENTER", listBg, "CENTER", 0, 0)
    parent.emptyText:SetText("Тишина в зоне. Никого не помечено.")
    parent.emptyText:Hide()
end

----------------------------------------------------------------
-- Title dropdown: pick which earned title to display on the nameplate
----------------------------------------------------------------

local TITLE_IDS = { 180, 181, 182, 183, 184 }

-- Strip the "%s" placeholder and surrounding whitespace so the dropdown
-- shows just the title word (e.g. "Охотник", not "Охотник %s").
local function cleanTitleName(name)
    if not name then return "" end
    name = name:gsub("%%s", "")
    name = name:gsub("^%s+", ""):gsub("%s+$", "")
    return name
end

-- Menu builder — invoked by UIDropDownMenu_Initialize each time the popup
-- opens. Called as a method (self = the menu frame), so the signature
-- is (self, level, menuList). We ignore all args and rebuild from scratch.
function BB.BuildTitleMenu(_, level)
    local current = GetCurrentTitle and GetCurrentTitle() or -1

    -- «снять» — clears active title.
    local info = UIDropDownMenu_CreateInfo()
    info.text = "«снять»"
    info.notCheckable = false
    info.checked = (current == -1)
    info.func = function()
        SetCurrentTitle(-1)
        BB:RefreshTitleButton()
        CloseDropDownMenus()
    end
    UIDropDownMenu_AddButton(info, level)

    for _, titleId in ipairs(TITLE_IDS) do
        if IsTitleKnown and IsTitleKnown(titleId) then
            local name = cleanTitleName(GetTitleName and GetTitleName(titleId))
            if name ~= "" then
                local id = titleId  -- fresh upvalue captured by the closure
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
    if not self.frame then return end
    local btn = self.frame.tabContent[1].titleButton
    if not btn then return end

    local current = GetCurrentTitle and GetCurrentTitle() or -1
    local text = "«нет»"
    if current and current > 0 and GetTitleName then
        local name = cleanTitleName(GetTitleName(current))
        if name ~= "" then text = name end
    end
    btn:SetText(text)
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

    -- Reputation
    local points, rank = self:ReadRepSnapshot()
    local rankName = RANK_NAMES[rank] or "—"
    local r, g, b = rankColorByTier(rank)
    content.repBox.rankText:SetText(string.format("|cff%02x%02x%02xРанг %d — %s|r",
        math.floor(r * 255), math.floor(g * 255), math.floor(b * 255), rank, rankName))

    if rank >= 5 then
        content.repBox.pointsText:SetText(string.format("%d очков (макс)", points))
        content.repBox.bar:SetValue(1)
    else
        local nextThreshold = RANK_THRESHOLDS[rank + 1] or (points + 1)
        local curThreshold  = RANK_THRESHOLDS[rank] or 0
        local span = math.max(1, nextThreshold - curThreshold)
        local progress = math.min(1, math.max(0, (points - curThreshold) / span))
        content.repBox.pointsText:SetText(string.format("%d / %d",
            points, nextThreshold))
        content.repBox.bar:SetValue(progress)
    end
    content.repBox.bar:SetStatusBarColor(r, g, b)

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

    -- Zone nemeses list — matches by localized zone name (server sends zone
    -- names already localized via GetAreaTableEntry, so both sides agree).
    local zoneList = {}
    local currentZoneName = GetRealZoneText and GetRealZoneText() or ""
    for _, nemesis in pairs(NT.data.nemeses or {}) do
        if nemesis.zoneName and nemesis.zoneName == currentZoneName then
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
    header:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, 0)
    header:SetText("|cffffd100Завершённые контракты|r")
    parent.header = header

    local listBg = CreateFrame("Frame", nil, parent)
    listBg:SetPoint("TOPLEFT", header, "BOTTOMLEFT", 0, -6)
    listBg:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", 0, 0)
    setDialogBackdrop(listBg, 0.4)
    parent.listBg = listBg

    parent.rows = {}
    for i = 1, MAX_HISTORY_ROWS do
        local row = CreateFrame("Frame", nil, listBg)
        row:SetHeight(ROW_HEIGHT + 4)
        row:SetPoint("TOPLEFT",  listBg, "TOPLEFT",  6, -(4 + (i - 1) * (ROW_HEIGHT + 4)))
        row:SetPoint("TOPRIGHT", listBg, "TOPRIGHT", -6, -(4 + (i - 1) * (ROW_HEIGHT + 4)))
        row.left = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.left:SetPoint("LEFT", row, "LEFT", 4, 0)
        row.left:SetJustifyH("LEFT")
        row.right = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.right:SetPoint("RIGHT", row, "RIGHT", -4, 0)
        row.right:SetJustifyH("RIGHT")
        row:Hide()
        parent.rows[i] = row
    end

    parent.emptyText = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    parent.emptyText:SetPoint("CENTER", listBg, "CENTER", 0, 0)
    parent.emptyText:SetText("Ни одного завершённого контракта.")
    parent.emptyText:Hide()

    parent.loadingText = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    parent.loadingText:SetPoint("CENTER", listBg, "CENTER", 0, 0)
    parent.loadingText:SetText("|cffa0a0a0Загрузка...|r")
    parent.loadingText:Hide()
end

function BB:RenderHistoryTab()
    local content = self.frame.tabContent[2]
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
    if self.activeTab == 2 then
        self:RenderHistoryTab()
    else
        self:RenderHuntTab()
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
