-- ============================================================================
-- Nemesis Bounty Vendor — Token item, ExtendedCost entries, vendor inventory
-- Adds a bounty token (100017) and a shared vendor entry (190000) that
-- innkeepers use via the NemesisBountyVendorScript gossip hook.
-- ============================================================================

-- ============================================================================
-- STEP 1: Create the Nemesis Bounty Token (item 100017)
-- class 12 = Quest, subclass 0 = Quest (shows as quest item, no "Use:")
-- ============================================================================

REPLACE INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `ItemLevel`, `RequiredLevel`, `MaxCount`, `stackable`, `bonding`, `description`, `Material`) VALUES
(100017, 12, 0, 'Nemesis Bounty Token', 35273, 3, 0, 0, 50000, 0, -1, 1, 0, 0, 200, 1, 'Reward for slaying a nemesis creature. Trade at any innkeeper.', 0);

-- Russian locale
DELETE FROM `item_template_locale` WHERE `ID` = 100017 AND `locale` = 'ruRU';
INSERT INTO `item_template_locale` (`ID`, `locale`, `Name`, `Description`) VALUES
(100017, 'ruRU', 'Жетон немезиды', 'Награда за убийство существа-немезиды. Обменивается у трактирщиков.');

-- ============================================================================
-- STEP 2: Server-side ItemExtendedCost entries
-- These define token costs for vendor items.
-- Format: ID, honorPts, arenaPts, arenaSlot, reqItem[5], reqItemCount[5], personalRating
-- We use IDs 100001-100005 to avoid conflicts with existing DBC entries.
-- ============================================================================

DELETE FROM `itemextendedcost_dbc` WHERE `ID` BETWEEN 100001 AND 100005;
INSERT INTO `itemextendedcost_dbc` (`ID`, `HonorPoints`, `ArenaPoints`, `ArenaBracket`, `ItemID_1`, `ItemID_2`, `ItemID_3`, `ItemID_4`, `ItemID_5`, `ItemCount_1`, `ItemCount_2`, `ItemCount_3`, `ItemCount_4`, `ItemCount_5`, `RequiredArenaRating`, `ItemPurchaseGroup`) VALUES
(100001, 0, 0, 0, 100017, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0),  -- 1 token
(100002, 0, 0, 0, 100017, 0, 0, 0, 0, 3,  0, 0, 0, 0, 0, 0),  -- 3 tokens
(100003, 0, 0, 0, 100017, 0, 0, 0, 0, 5,  0, 0, 0, 0, 0, 0),  -- 5 tokens
(100004, 0, 0, 0, 100017, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0),  -- 10 tokens
(100005, 0, 0, 0, 100017, 0, 0, 0, 0, 25, 0, 0, 0, 0, 0, 0);  -- 25 tokens

-- ============================================================================
-- STEP 3: Bounty vendor inventory (shared entry 190000)
-- All innkeepers use this vendor entry via gossip hook.
-- ExtendedCost references the token costs above.
-- Placeholder items — expand as desired.
-- ============================================================================

DELETE FROM `npc_vendor` WHERE `entry` = 190000;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`, `VerifiedBuild`) VALUES
-- StatBooster scrolls (1 token each)
(190000, 1,  100001, 0, 0, 100001, 0),  -- Runed Whetstone
(190000, 2,  100005, 0, 0, 100001, 0),  -- Runed Armor Patch
(190000, 3,  100009, 0, 0, 100001, 0),  -- Minor Arcane Vellum
(190000, 4,  100013, 0, 0, 100001, 0),  -- Minor Glyph of Fortune
-- T2 scrolls (3 tokens)
(190000, 5,  100002, 0, 0, 100002, 0),  -- Tempered Whetstone
(190000, 6,  100006, 0, 0, 100002, 0),  -- Tempered Armor Patch
(190000, 7,  100010, 0, 0, 100002, 0),  -- Arcane Vellum
(190000, 8,  100014, 0, 0, 100002, 0),  -- Glyph of Fortune
-- T3 scrolls (5 tokens)
(190000, 9,  100003, 0, 0, 100003, 0),  -- Honed Whetstone
(190000, 10, 100007, 0, 0, 100003, 0),  -- Hardened Armor Patch
(190000, 11, 100011, 0, 0, 100003, 0),  -- Greater Arcane Vellum
(190000, 12, 100015, 0, 0, 100003, 0),  -- Major Glyph of Fortune
-- T4 scrolls (10 tokens)
(190000, 13, 100004, 0, 0, 100004, 0),  -- Masterwork Whetstone
(190000, 14, 100008, 0, 0, 100004, 0),  -- Masterwork Armor Patch
(190000, 15, 100012, 0, 0, 100004, 0),  -- Superior Arcane Vellum
(190000, 16, 100016, 0, 0, 100004, 0),  -- Grand Glyph of Fortune
-- Attribute Recalibrator (5 tokens)
(190000, 17, 41605,  0, 0, 100003, 0);  -- Attribute Recalibrator
