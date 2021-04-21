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
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `beacon` int(11) DEFAULT NULL,
  `twitter` tinytext,
  `name` tinytext,
  `nickname` tinytext,
  `slack` tinytext,
  `active` int(11) DEFAULT '1',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=0 DEFAULT CHARSET=utf8mb4;

CREATE TABLE `alive2` (
  `room` tinytext NOT NULL,
  `d_id` int(11) NOT NULL,
  `ipaddress` tinytext,
  `timestamp` tinytext,
  PRIMARY KEY (`room`(20),`d_id`),
  KEY `room` (`room`(20))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE `room_log` (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `timestamp` bigint(20) NOT NULL,
  `room` tinytext,
  `d_id` int(11) DEFAULT NULL,
  `beacon` int(11) DEFAULT NULL,
  `proxi` float DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `timestamp` (`timestamp`)
) ENGINE=InnoDB AUTO_INCREMENT=0 DEFAULT CHARSET=utf8mb4;

CREATE SQL SECURITY INVOKER VIEW `room_status`
AS SELECT
   `t1`.`beacon` AS `beacon`,
   `t1`.`room` AS `room`,avg(`t1`.`proxi`) AS `avgproxi`
FROM `room_log` `t1` where (`t1`.`beacon` in (select distinct `ble_tag`.`beacon` from `ble_tag`) and ((select count(`t2`.`id`) from `room_log` `t2` where (`t2`.`beacon` in (select distinct `ble_tag`.`beacon` from `ble_tag`) and (`t1`.`beacon` = `t2`.`beacon`) and (`t1`.`room` = `t2`.`room`) and (`t1`.`proxi` > `t2`.`proxi`))) < 10)) group by `t1`.`beacon`,`t1`.`room` order by `t1`.`beacon`,(select min(`t3`.`proxi`) from `room_log` `t3` where (`t3`.`beacon` in (select distinct `ble_tag`.`beacon` from `ble_tag`) and (`t1`.`beacon` = `t3`.`beacon`) and (`t1`.`room` = `t3`.`room`))),`t1`.`room`;

CREATE SQL SECURITY INVOKER VIEW `room_detectors`
AS SELECT
   `t1`.`beacon` AS `beacon`,
   `t1`.`room` AS `room`,
   `t1`.`d_id` AS `d_id`,(avg(`t1`.`proxi`) + (30 / count(`t1`.`proxi`))) AS `avgproxi`,avg(`t1`.`proxi`) AS `rawavgproxi`,count(`t1`.`proxi`) AS `rawcount`
FROM `room_log` `t1` where `t1`.`beacon` in (select distinct `ble_tag`.`beacon` from `ble_tag`) group by `t1`.`beacon`,`t1`.`room`,`t1`.`d_id` order by `t1`.`beacon`,(select min(`t3`.`proxi`) from `room_log` `t3` where (`t3`.`beacon` in (select distinct `ble_tag`.`beacon` from `ble_tag`) and (`t1`.`beacon` = `t3`.`beacon`) and (`t1`.`room` = `t3`.`room`) and (`t1`.`d_id` = `t3`.`d_id`))),`t1`.`room`;

CREATE SQL SECURITY INVOKER VIEW `room_monitor`
AS SELECT
   `room_status`.`beacon` AS `beacon`,
   `ble_tag`.`name` AS `name`,
   `ble_tag`.`twitter` AS `twitter`,
   `ble_tag`.`slack` AS `slack`,
   `room_status`.`room` AS `room`,min(`room_status`.`avgproxi`) AS `minavgproxi`
FROM (`room_status` join `ble_tag`) where (`room_status`.`beacon` = `ble_tag`.`beacon`) group by `room_status`.`beacon`;
