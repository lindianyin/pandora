-- Pandora M1 schema (MySQL 8+, utf8mb4)
-- Based on SPEC §6.1 — 所有业务列均为 NOT NULL（无值用空串/哨兵时间）

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS `user` (
  `uid` BIGINT NOT NULL AUTO_INCREMENT,
  `account_type` TINYINT NOT NULL DEFAULT 1 COMMENT '1=guest 2=phone',
  `open_id` VARCHAR(128) NOT NULL,
  `phone` VARCHAR(32) NOT NULL DEFAULT '' COMMENT '空串表示未绑定',
  `status` TINYINT NOT NULL DEFAULT 0 COMMENT '0=ok 1=banned',
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`uid`),
  UNIQUE KEY `uk_account_open` (`account_type`, `open_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `player_profile` (
  `uid` BIGINT NOT NULL,
  `nickname` VARCHAR(64) NOT NULL DEFAULT '',
  `avatar` VARCHAR(256) NOT NULL DEFAULT '',
  `gold` BIGINT NOT NULL DEFAULT 10000,
  `diamond` BIGINT NOT NULL DEFAULT 0,
  `level` INT NOT NULL DEFAULT 1,
  PRIMARY KEY (`uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ledger` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `uid` BIGINT NOT NULL,
  `currency` TINYINT NOT NULL COMMENT '1=gold 2=diamond',
  `delta` BIGINT NOT NULL,
  `balance_after` BIGINT NOT NULL,
  `biz_type` VARCHAR(32) NOT NULL,
  `idempotent_key` VARCHAR(64) NOT NULL,
  `ref_id` VARCHAR(64) NOT NULL DEFAULT '',
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_idempotent` (`idempotent_key`),
  KEY `idx_uid_time` (`uid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `room_template` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `game_id` INT NOT NULL DEFAULT 1,
  `name` VARCHAR(64) NOT NULL,
  `base_score` INT NOT NULL DEFAULT 100,
  `rake_bp` INT NOT NULL DEFAULT 500,
  `min_gold` BIGINT NOT NULL DEFAULT 0,
  `max_gold` BIGINT NOT NULL DEFAULT 0,
  `enabled` TINYINT NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `game_round` (
  `round_id` BIGINT NOT NULL,
  `room_id` BIGINT NOT NULL,
  `game_id` INT NOT NULL,
  `template_id` INT NOT NULL,
  `players_json` JSON NOT NULL,
  `base_score` INT NOT NULL,
  `multiplier` INT NOT NULL DEFAULT 1,
  `started_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `ended_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`round_id`),
  KEY `idx_room` (`room_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `activity_define` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `type` VARCHAR(16) NOT NULL COMMENT 'sign/task/gift',
  `title` VARCHAR(128) NOT NULL,
  `rules_json` JSON NOT NULL,
  `start_at` DATETIME(3) NOT NULL DEFAULT '1970-01-01 00:00:00.000' COMMENT '哨兵=无开始限制',
  `end_at` DATETIME(3) NOT NULL DEFAULT '9999-12-31 23:59:59.999' COMMENT '哨兵=无结束限制',
  `enabled` TINYINT NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `activity_progress` (
  `activity_id` INT NOT NULL,
  `uid` BIGINT NOT NULL,
  `progress_json` JSON NOT NULL,
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`activity_id`, `uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `activity_claim` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `activity_id` INT NOT NULL,
  `uid` BIGINT NOT NULL,
  `reward_key` VARCHAR(64) NOT NULL,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_claim` (`activity_id`, `uid`, `reward_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `activity_stock` (
  `activity_id` INT NOT NULL,
  `remain` INT NOT NULL,
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`activity_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pay_product` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `amount_fen` INT NOT NULL,
  `diamond` INT NOT NULL,
  `gift_diamond` INT NOT NULL DEFAULT 0,
  `sort` INT NOT NULL DEFAULT 0,
  `enabled` TINYINT NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pay_order` (
  `order_id` VARCHAR(32) NOT NULL,
  `uid` BIGINT NOT NULL,
  `product_id` INT NOT NULL,
  `amount_fen` INT NOT NULL,
  `status` TINYINT NOT NULL DEFAULT 0 COMMENT '0=pending 1=ok 2=fail 3=closed',
  `alipay_trade_no` VARCHAR(64) NOT NULL DEFAULT '',
  `idempotent_paid` TINYINT NOT NULL DEFAULT 0,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `paid_at` DATETIME(3) NOT NULL DEFAULT '1970-01-01 00:00:00.000' COMMENT '哨兵=未支付',
  PRIMARY KEY (`order_id`),
  KEY `idx_uid` (`uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `admin_user` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `username` VARCHAR(64) NOT NULL,
  `password_hash` VARCHAR(128) NOT NULL,
  `role` VARCHAR(16) NOT NULL DEFAULT 'cs',
  `enabled` TINYINT NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `admin_audit` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `admin_id` INT NOT NULL,
  `action` VARCHAR(64) NOT NULL,
  `target` VARCHAR(128) NOT NULL DEFAULT '',
  `before_json` JSON NOT NULL,
  `after_json` JSON NOT NULL,
  `ip` VARCHAR(64) NOT NULL DEFAULT '',
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  KEY `idx_admin_time` (`admin_id`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- FR-SOC-01
CREATE TABLE IF NOT EXISTS `player_stats` (
  `uid` BIGINT NOT NULL,
  `total_rounds` INT NOT NULL DEFAULT 0,
  `win_rounds` INT NOT NULL DEFAULT 0,
  `lose_rounds` INT NOT NULL DEFAULT 0,
  `landlord_rounds` INT NOT NULL DEFAULT 0,
  `gold_win_sum` BIGINT NOT NULL DEFAULT 0,
  `gold_lose_sum` BIGINT NOT NULL DEFAULT 0,
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- FR-SOC-02
CREATE TABLE IF NOT EXISTS `friend_request` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `from_uid` BIGINT NOT NULL,
  `to_uid` BIGINT NOT NULL,
  `status` TINYINT NOT NULL DEFAULT 0 COMMENT '0=pending 1=accepted 2=rejected 3=expired',
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_from_to` (`from_uid`, `to_uid`),
  KEY `idx_to_status` (`to_uid`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `friendship` (
  `uid_low` BIGINT NOT NULL,
  `uid_high` BIGINT NOT NULL,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`uid_low`, `uid_high`),
  KEY `idx_high` (`uid_high`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- FR-SOC-03
CREATE TABLE IF NOT EXISTS `mail` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `to_uid` BIGINT NOT NULL,
  `title` VARCHAR(128) NOT NULL,
  `body` VARCHAR(1024) NOT NULL DEFAULT '',
  `attach_json` JSON NOT NULL,
  `status` TINYINT NOT NULL DEFAULT 0 COMMENT '0=unread 1=read 2=claimed 3=deleted',
  `expire_at` DATETIME(3) NOT NULL,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  KEY `idx_uid_status` (`to_uid`, `status`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mail_send_log` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `admin_id` INT NOT NULL,
  `scope` VARCHAR(16) NOT NULL COMMENT 'uids/all',
  `target_json` JSON NOT NULL,
  `title` VARCHAR(128) NOT NULL,
  `body` VARCHAR(1024) NOT NULL DEFAULT '',
  `attach_json` JSON NOT NULL,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  KEY `idx_admin_time` (`admin_id`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- FR-SOC-04
CREATE TABLE IF NOT EXISTS `rank_snapshot` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `period` VARCHAR(16) NOT NULL COMMENT 'daily/weekly',
  `period_key` VARCHAR(16) NOT NULL,
  `uid` BIGINT NOT NULL,
  `score` BIGINT NOT NULL,
  `rank_no` INT NOT NULL,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_period_uid` (`period`, `period_key`, `uid`),
  KEY `idx_period_rank` (`period`, `period_key`, `rank_no`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `room_template` (`id`, `game_id`, `name`, `base_score`, `rake_bp`, `min_gold`, `max_gold`, `enabled`)
VALUES (1, 1, '初级场', 100, 500, 1000, 0, 1)
ON DUPLICATE KEY UPDATE `name`=VALUES(`name`);

INSERT INTO `pay_product` (`id`, `amount_fen`, `diamond`, `gift_diamond`, `sort`, `enabled`)
VALUES
  (1, 600, 60, 0, 1, 1),
  (2, 3000, 300, 30, 2, 1),
  (3, 9800, 980, 100, 3, 1)
ON DUPLICATE KEY UPDATE
  `amount_fen`=VALUES(`amount_fen`),
  `diamond`=VALUES(`diamond`),
  `gift_diamond`=VALUES(`gift_diamond`),
  `enabled`=VALUES(`enabled`);

SET FOREIGN_KEY_CHECKS = 1;
