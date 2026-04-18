-- ============================================================================
-- Nemesis Bounty Vendor — Token item, ExtendedCost entries, vendor inventory
-- Adds a bounty token (100017) and a shared vendor entry (190000) that
-- innkeepers use via the NemesisBountyVendorScript gossip hook.
-- Also adds a virtual creature_template entry (190001) used only as the
-- "from" name on bounty completion mails (never spawned in the world).
-- ============================================================================

-- ============================================================================
-- STEP 0: Virtual mail-sender creature_template (entry 190002)
-- Never spawned. Used only as the sender entry for bounty completion mails
-- so the mailbox shows an NPC name instead of "Неизвестно".
--
-- NOTE: Name is kept in ASCII only. The Russian 3.3.5a client's mail window
-- renders creature names with a cp1251 glyph path, so UTF-8 Russian in this
-- field appears as mojibake. English "Innkeeper" is ASCII → identical in
-- both encodings → always renders correctly. No ruRU locale override needed
-- (the client falls back to the base name for locales without a row).
-- ============================================================================

REPLACE INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `unit_class`, `type`) VALUES
(190002, 'Innkeeper', 'Bounty Board', 1, 1, 35, 1, 7);

-- Clean up any prior test entry so future migrations are idempotent.
DELETE FROM `creature_template_locale` WHERE `entry` IN (190001, 190002);
DELETE FROM `creature_template` WHERE `entry` = 190001;

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
(100001, 0, 0, 0, 100017, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0),  -- T1 scrolls
(100002, 0, 0, 0, 100017, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0),  -- T2 scrolls
(100003, 0, 0, 0, 100017, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0),  -- T3 scrolls + Reroll (Attribute Recalibrator)
(100004, 0, 0, 0, 100017, 0, 0, 0, 0, 25, 0, 0, 0, 0, 0, 0),  -- T4 scrolls
(100005, 0, 0, 0, 100017, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 0);  -- reserved for future premium items

-- ============================================================================
-- STEP 3: Bounty vendor inventory (shared entry 190000)
-- All innkeepers use this vendor entry via gossip hook.
-- ExtendedCost references the token costs above.
-- Placeholder items — expand as desired.
-- ============================================================================

DELETE FROM `npc_vendor` WHERE `entry` = 190000;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`, `VerifiedBuild`) VALUES
-- T1 scrolls (10 tokens)
(190000, 1,  100001, 0, 0, 100001, 0),  -- Runed Whetstone
(190000, 2,  100005, 0, 0, 100001, 0),  -- Runed Armor Patch
(190000, 3,  100009, 0, 0, 100001, 0),  -- Minor Arcane Vellum
(190000, 4,  100013, 0, 0, 100001, 0),  -- Minor Glyph of Fortune
-- T2 scrolls (15 tokens)
(190000, 5,  100002, 0, 0, 100002, 0),  -- Tempered Whetstone
(190000, 6,  100006, 0, 0, 100002, 0),  -- Tempered Armor Patch
(190000, 7,  100010, 0, 0, 100002, 0),  -- Arcane Vellum
(190000, 8,  100014, 0, 0, 100002, 0),  -- Glyph of Fortune
-- T3 scrolls (20 tokens)
(190000, 9,  100003, 0, 0, 100003, 0),  -- Honed Whetstone
(190000, 10, 100007, 0, 0, 100003, 0),  -- Hardened Armor Patch
(190000, 11, 100011, 0, 0, 100003, 0),  -- Greater Arcane Vellum
(190000, 12, 100015, 0, 0, 100003, 0),  -- Major Glyph of Fortune
-- T4 scrolls (25 tokens)
(190000, 13, 100004, 0, 0, 100004, 0),  -- Masterwork Whetstone
(190000, 14, 100008, 0, 0, 100004, 0),  -- Masterwork Armor Patch
(190000, 15, 100012, 0, 0, 100004, 0),  -- Superior Arcane Vellum
(190000, 16, 100016, 0, 0, 100004, 0),  -- Grand Glyph of Fortune
-- Attribute Recalibrator (20 tokens, same price as T3)
(190000, 17, 41605,  0, 0, 100003, 0);  -- Attribute Recalibrator
