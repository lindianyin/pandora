-- FR-SOC tables (safe to re-run)
SET NAMES utf8mb4;

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
