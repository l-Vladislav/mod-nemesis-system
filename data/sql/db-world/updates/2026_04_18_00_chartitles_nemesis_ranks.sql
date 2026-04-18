-- NEM-002: Hunter's Covenant rank titles
-- Mirrors CharTitles_custom.csv for sCharTitlesStore (server-side DBC overlay).
-- Client must have a matching CharTitles.dbc patch (via MPQ) to render them.
-- Russian text lives in the zhTW slot; matches the repack's locale mapping.
--
-- IDs 180-184 chosen because MAX_TITLE_INDEX = 192 in AzerothCore 3.3.5a
-- (KnownTitles is 3 uint64 = 192 bits). Stock WotLK uses up to Mask_ID 142.

DELETE FROM `chartitles_dbc` WHERE `ID` BETWEEN 180 AND 204;

INSERT INTO `chartitles_dbc`
    (`ID`, `Condition_ID`,
     `Name_Lang_zhTW`,  `Name_Lang_Mask`,
     `Name1_Lang_zhTW`, `Name1_Lang_Mask`,
     `Mask_ID`)
VALUES
    (180, 0, 'Послушник %s',       16712190, 'Послушница %s',   16712190, 180),
    (181, 0, 'Охотник %s',         16712190, 'Охотница %s',     16712190, 181),
    (182, 0, 'Следопыт %s',        16712190, 'Следопытка %s',   16712190, 182),
    (183, 0, 'Ветеран Охоты %s',   16712190, 'Ветеран Охоты %s', 16712190, 183),
    (184, 0, 'Легенда Охоты %s',   16712190, 'Легенда Охоты %s', 16712190, 184);
