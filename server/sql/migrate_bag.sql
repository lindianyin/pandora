-- FR-BAG migrate (idempotent-ish for MySQL 5.7+)
SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `item_define` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `name` VARCHAR(64) NOT NULL,
  `icon` VARCHAR(256) NOT NULL DEFAULT '',
  `kind` VARCHAR(16) NOT NULL COMMENT 'qty/qty_ttl/ttl',
  `stackable` TINYINT NOT NULL DEFAULT 1,
  `default_expire_sec` INT NOT NULL DEFAULT 0,
  `tag` VARCHAR(32) NOT NULL DEFAULT '',
  `enabled` TINYINT NOT NULL DEFAULT 1,
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `bag_item` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `uid` BIGINT NOT NULL,
  `item_id` INT NOT NULL,
  `quantity` BIGINT NOT NULL,
  `expire_at` DATETIME(3) NOT NULL,
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_uid_item_expire` (`uid`, `item_id`, `expire_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `item_ledger` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `uid` BIGINT NOT NULL,
  `item_id` INT NOT NULL,
  `delta` BIGINT NOT NULL,
  `quantity_after` BIGINT NOT NULL,
  `expire_at` DATETIME(3) NOT NULL,
  `biz_type` VARCHAR(32) NOT NULL,
  `idempotent_key` VARCHAR(64) NOT NULL,
  `ref_id` VARCHAR(64) NOT NULL DEFAULT '',
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_idempotent` (`idempotent_key`),
  KEY `idx_uid_time` (`uid`, `created_at`),
  KEY `idx_uid_id` (`uid`, `id`),
  KEY `idx_item_id` (`item_id`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `item_define` (`id`, `name`, `icon`, `kind`, `stackable`, `default_expire_sec`, `tag`, `enabled`)
VALUES
  (1001, '改名卡', '', 'qty', 1, 0, 'rename', 1),
  (1002, '限时加倍券', '', 'qty_ttl', 1, 86400, 'ticket', 1),
  (2001, '周卡体验', '', 'ttl', 1, 604800, 'pass', 1)
ON DUPLICATE KEY UPDATE
  `name`=VALUES(`name`),
  `kind`=VALUES(`kind`),
  `default_expire_sec`=VALUES(`default_expire_sec`),
  `tag`=VALUES(`tag`),
  `enabled`=VALUES(`enabled`);
