-- NEM-001: Bounty Board
-- Active bounty: one row per player
CREATE TABLE IF NOT EXISTS `character_nemesis_bounty` (
    `guid`            int unsigned NOT NULL COMMENT 'player GUID',
    `target_spawn_id` int unsigned NOT NULL COMMENT 'nemesis spawnId',
    `target_title`    varchar(128) NOT NULL COMMENT 'cached title in case spawn cleared',
    `accepted_at`     int unsigned NOT NULL,
    `expires_at`      int unsigned NOT NULL,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Completion log: append-only history
CREATE TABLE IF NOT EXISTS `character_nemesis_bounty_history` (
    `id`              bigint unsigned NOT NULL AUTO_INCREMENT,
    `guid`            int unsigned NOT NULL,
    `target_spawn_id` int unsigned NOT NULL,
    `target_title`    varchar(128) NOT NULL,
    `target_rank`     tinyint unsigned NOT NULL,
    `completed_at`    int unsigned NOT NULL,
    `tokens_earned`   int unsigned NOT NULL,
    PRIMARY KEY (`id`),
    KEY `idx_character_nemesis_bounty_history_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
