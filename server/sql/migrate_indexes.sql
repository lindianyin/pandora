-- Index / query-path fixes (MySQL 8+)
-- Idempotent where possible; ADD INDEX may error if already applied — safe to skip duplicates.

SET NAMES utf8mb4;

-- P0: gold rank fallback / snapshot
ALTER TABLE `player_profile`
  ADD KEY `idx_gold_uid` (`gold`, `uid`);

-- P0: game_round by end time (admin all-rounds still uses PK / round_id)
ALTER TABLE `game_round`
  ADD KEY `idx_ended_at` (`ended_at`);

-- P0: player ↔ round side table (replaces players_json LIKE for recent/history)
CREATE TABLE IF NOT EXISTS `game_round_player` (
  `round_id` BIGINT NOT NULL,
  `uid` BIGINT NOT NULL,
  `ended_at` DATETIME(3) NOT NULL,
  PRIMARY KEY (`uid`, `round_id`),
  KEY `idx_uid_ended` (`uid`, `ended_at`),
  KEY `idx_round` (`round_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Backfill from existing rounds (MySQL 5.7+/8: expand up to 4 seats)
INSERT IGNORE INTO `game_round_player` (`round_id`, `uid`, `ended_at`)
SELECT g.`round_id`,
       CAST(JSON_UNQUOTE(JSON_EXTRACT(g.`players_json`, CONCAT('$[', s.idx, '].uid'))) AS UNSIGNED),
       g.`ended_at`
FROM `game_round` g
JOIN (
  SELECT 0 AS idx UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3
) s
WHERE JSON_EXTRACT(g.`players_json`, CONCAT('$[', s.idx, '].uid')) IS NOT NULL
  AND CAST(JSON_UNQUOTE(JSON_EXTRACT(g.`players_json`, CONCAT('$[', s.idx, '].uid'))) AS UNSIGNED) > 0;

-- P1: ledger / item_ledger ORDER BY id aligned with filter
ALTER TABLE `ledger`
  ADD KEY `idx_uid_id` (`uid`, `id`);

ALTER TABLE `item_ledger`
  ADD KEY `idx_uid_id` (`uid`, `id`),
  ADD KEY `idx_item_id` (`item_id`, `id`);

-- P1: mail list by to_uid + id (status filtered in SQL with IN)
ALTER TABLE `mail`
  ADD KEY `idx_to_uid_id` (`to_uid`, `id`);

-- P1: friend request outbound pending list
ALTER TABLE `friend_request`
  ADD KEY `idx_from_status` (`from_uid`, `status`);

-- P1: latest rank snapshot period_key
ALTER TABLE `rank_snapshot`
  ADD KEY `idx_period_created` (`period`, `created_at`);

-- P1: admin order list by time
ALTER TABLE `pay_order`
  ADD KEY `idx_created` (`created_at`);

-- Optional: drop redundant bag index (UK already covers uid,item_id prefix)
ALTER TABLE `bag_item`
  DROP KEY `idx_uid_item`;
