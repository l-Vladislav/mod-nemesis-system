-- Adds zone_id to the bounty + history tables so the 2h completion cap
-- can be counted independently per zone.
--
-- Idempotent: applied by hand on PTR/live before the DBUpdater picked it up, so
-- the columns and index can already exist. Guarded via information_schema — a
-- bare ADD COLUMN raises ERROR 1060 and aborts the whole update batch.

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'character_nemesis_bounty'
               AND COLUMN_NAME = 'zone_id');
SET @sql := IF(@col = 0,
    'ALTER TABLE `character_nemesis_bounty` ADD COLUMN `zone_id` int unsigned NOT NULL DEFAULT 0 COMMENT ''innkeeper zone where the contract was accepted'' AFTER `target_title`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'character_nemesis_bounty_history'
               AND COLUMN_NAME = 'zone_id');
SET @sql := IF(@col = 0,
    'ALTER TABLE `character_nemesis_bounty_history` ADD COLUMN `zone_id` int unsigned NOT NULL DEFAULT 0 COMMENT ''zone where the bounty was completed (copied from bounty row)'' AFTER `target_title`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @idx := (SELECT COUNT(*) FROM information_schema.STATISTICS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'character_nemesis_bounty_history'
               AND INDEX_NAME = 'idx_character_nemesis_bounty_history_zone');
SET @sql := IF(@idx = 0,
    'ALTER TABLE `character_nemesis_bounty_history` ADD INDEX `idx_character_nemesis_bounty_history_zone` (`guid`, `zone_id`, `completed_at`)',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
