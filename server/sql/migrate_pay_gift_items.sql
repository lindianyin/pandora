-- FR-BAG: pay product gift items (MySQL 5.7+)
SET NAMES utf8mb4;

-- Add column if missing (idempotent via information_schema check is verbose; tolerate duplicate)
SET @col_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'pay_product' AND COLUMN_NAME = 'gift_items_json'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `pay_product` ADD COLUMN `gift_items_json` JSON NULL AFTER `gift_diamond`',
  'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

UPDATE `pay_product`
SET `gift_items_json` = CAST('[]' AS JSON)
WHERE `gift_items_json` IS NULL;
