-- Adds zone_id to the bounty + history tables so the 2h completion cap
-- can be counted independently per zone.

ALTER TABLE `character_nemesis_bounty`
    ADD COLUMN `zone_id` int unsigned NOT NULL DEFAULT 0
    COMMENT 'innkeeper zone where the contract was accepted'
    AFTER `target_title`;

ALTER TABLE `character_nemesis_bounty_history`
    ADD COLUMN `zone_id` int unsigned NOT NULL DEFAULT 0
    COMMENT 'zone where the bounty was completed (copied from bounty row)'
    AFTER `target_title`,
    ADD INDEX `idx_character_nemesis_bounty_history_zone`
    (`guid`, `zone_id`, `completed_at`);
