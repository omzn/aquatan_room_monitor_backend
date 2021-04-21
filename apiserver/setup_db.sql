CREATE DATABASE ibeacon;
use ibeacon;

CREATE TABLE `log` (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `timestamp` tinytext,
  `label` tinytext,
  `value` double DEFAULT NULL,
  `note` tinytext,
  PRIMARY KEY (`id`),
  KEY `idlabel` (`label`(20)),
  KEY `idlabelval` (`label`(20),`value`),
  KEY `lbl_ts` (`label`(20),`timestamp`(20))
) ENGINE=InnoDB AUTO_INCREMENT=0 DEFAULT CHARSET=utf8mb4;

CREATE TABLE `ble_tag` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `label` int DEFAULT NULL,
  `twitter` tinytext,
  `name` tinytext,
  `nickname` tinytext,
  `slack` tinytext,
  `active` int DEFAULT '1',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `alive2` (
  `label` tinytext NOT NULL,
  `d_id` int NOT NULL,
  `ipaddress` tinytext,
  `timestamp` tinytext,
  PRIMARY KEY (`label`(20),`d_id`),
  KEY `label` (`label`(20))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `room_log` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `timestamp` bigint NOT NULL,
  `label` int DEFAULT NULL,
  `place` tinytext,
  `proxi` float DEFAULT NULL,
  `d_id` int DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `timestamp` (`timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE SQL SECURITY INVOKER VIEW `room_detectors`
AS SELECT
   `t1`.`label` AS `label`,
   `t1`.`place` AS `place`,
   `t1`.`d_id` AS `d_id`,(avg(`t1`.`proxi`) + (30 / count(`t1`.`proxi`))) AS `avgproxi`,avg(`t1`.`proxi`) AS `rawavgproxi`,count(`t1`.`proxi`) AS `rawcount`
FROM `room_log` `t1` where `t1`.`label` in (select distinct `ble_tag`.`label` from `ble_tag`) group by `t1`.`label`,`t1`.`place`,`t1`.`d_id` order by `t1`.`label`,(select min(`t3`.`proxi`) from `room_log` `t3` where (`t3`.`label` in (select distinct `ble_tag`.`label` from `ble_tag`) and (`t1`.`label` = `t3`.`label`) and (`t1`.`place` = `t3`.`place`) and (`t1`.`d_id` = `t3`.`d_id`))),`t1`.`place`;

CREATE SQL SECURITY INVOKER VIEW `room_status`
AS SELECT
   `room_detectors`.`label` AS `label`,
   `room_detectors`.`place` AS `place`,round((avg(`room_detectors`.`avgproxi`) + (20 / sum(`room_detectors`.`rawcount`))),4) AS `avgproxi`,sum(`room_detectors`.`rawcount`) AS `sumcount`
FROM `room_detectors` group by `room_detectors`.`label` order by `room_detectors`.`label`,`avgproxi`;

CREATE SQL SECURITY INVOKER VIEW `room_monitor`
AS SELECT
   `room_status`.`label` AS `label`,
   `ble_tag`.`name` AS `name`,
   `ble_tag`.`twitter` AS `twitter`,
   `ble_tag`.`slack` AS `slack`,
   `room_status`.`place` AS `place`,min(`room_status`.`avgproxi`) AS `minavgproxi`
FROM (`room_status` join `ble_tag`) where (`room_status`.`label` = `ble_tag`.`label`) group by `room_status`.`label`;