-- Migrate existing DB: drop NULL columns → NOT NULL + defaults/sentinels.
-- Safe to re-run on MySQL 5.7+/8.x.

SET NAMES utf8mb4;

UPDATE `user`
  SET `phone` = '' WHERE `phone` IS NULL;
ALTER TABLE `user`
  MODIFY COLUMN `phone` VARCHAR(32) NOT NULL DEFAULT '' COMMENT '空串表示未绑定';

UPDATE `game_round`
  SET `ended_at` = IFNULL(`ended_at`, `started_at`) WHERE `ended_at` IS NULL;
ALTER TABLE `game_round`
  MODIFY COLUMN `started_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  MODIFY COLUMN `ended_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3);

UPDATE `activity_define`
  SET `start_at` = '1970-01-01 00:00:00.000' WHERE `start_at` IS NULL;
UPDATE `activity_define`
  SET `end_at` = '9999-12-31 23:59:59.999' WHERE `end_at` IS NULL;
ALTER TABLE `activity_define`
  MODIFY COLUMN `start_at` DATETIME(3) NOT NULL DEFAULT '1970-01-01 00:00:00.000' COMMENT '哨兵=无开始限制',
  MODIFY COLUMN `end_at` DATETIME(3) NOT NULL DEFAULT '9999-12-31 23:59:59.999' COMMENT '哨兵=无结束限制';

UPDATE `pay_order`
  SET `paid_at` = '1970-01-01 00:00:00.000' WHERE `paid_at` IS NULL;
ALTER TABLE `pay_order`
  MODIFY COLUMN `paid_at` DATETIME(3) NOT NULL DEFAULT '1970-01-01 00:00:00.000' COMMENT '哨兵=未支付';

UPDATE `admin_audit`
  SET `before_json` = CAST('{}' AS JSON) WHERE `before_json` IS NULL;
UPDATE `admin_audit`
  SET `after_json` = CAST('{}' AS JSON) WHERE `after_json` IS NULL;
ALTER TABLE `admin_audit`
  MODIFY COLUMN `before_json` JSON NOT NULL,
  MODIFY COLUMN `after_json` JSON NOT NULL;
