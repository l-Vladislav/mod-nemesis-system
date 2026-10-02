-- Track where and when a nemesis was last seen.
--
-- Idempotent: this migration was originally applied by hand on PTR/live before
-- it was picked up by the DBUpdater, so the columns and index can already exist.
-- Guarded via information_schema so a re-run is a no-op instead of an error
-- (ERROR 1060 "Duplicate column name" aborts the whole update batch).

SET @col_zone := (SELECT COUNT(*) FROM information_schema.COLUMNS
                  WHERE TABLE_SCHEMA = DATABASE()
                    AND TABLE_NAME = 'character_nemesis'
                    AND COLUMN_NAME = 'zone_id');
SET @sql := IF(@col_zone = 0,
    'ALTER TABLE `character_nemesis` ADD COLUMN `zone_id` int unsigned NOT NULL DEFAULT 0 AFTER `map_id`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col_seen := (SELECT COUNT(*) FROM information_schema.COLUMNS
                  WHERE TABLE_SCHEMA = DATABASE()
                    AND TABLE_NAME = 'character_nemesis'
                    AND COLUMN_NAME = 'last_seen_at');
SET @sql := IF(@col_seen = 0,
    'ALTER TABLE `character_nemesis` ADD COLUMN `last_seen_at` int unsigned NOT NULL DEFAULT 0 AFTER `last_victim_guid`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @idx := (SELECT COUNT(*) FROM information_schema.STATISTICS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'character_nemesis'
               AND INDEX_NAME = 'idx_character_nemesis_last_seen');
SET @sql := IF(@idx = 0,
    'ALTER TABLE `character_nemesis` ADD KEY `idx_character_nemesis_last_seen` (`last_seen_at`)',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
