-- NEM-002: Hunter's Covenant reputation
-- One row per character tracking cumulative reputation points.
-- Tier (1-5) is computed from points at runtime against config thresholds.
CREATE TABLE IF NOT EXISTS `character_nemesis_reputation` (
    `guid`          int unsigned NOT NULL COMMENT 'player GUID',
    `points`        int unsigned NOT NULL DEFAULT 0,
    `highest_rank`  tinyint unsigned NOT NULL DEFAULT 1
        COMMENT 'highest tier earned so far; never decreases',
    `updated_at`    int unsigned NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
