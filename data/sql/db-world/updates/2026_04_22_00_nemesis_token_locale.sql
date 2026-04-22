-- Re-apply ruRU locale for item 100017 (Nemesis Bounty Token).
-- The row in the base SQL only runs on initial module install, so any later
-- edits to the Russian name/description never reach the DB. This migration
-- reseeds the row so the current text is what ruRU clients see.

DELETE FROM `item_template_locale` WHERE `ID` = 100017 AND `locale` = 'ruRU';
INSERT INTO `item_template_locale` (`ID`, `locale`, `Name`, `Description`) VALUES
(100017, 'ruRU', 'Жетон немезиды', 'Награда за убийство существа-немезиды. Обменивается у трактирщиков.');
