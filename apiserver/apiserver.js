var conf = require('config');
const express = require('express');  // expressモジュールを読み込む
const mysql = require('mysql');
var strftime = require('strftime');
const bodyParser = require('body-parser');
const app = express();  // expressアプリを生成する
//var webclient = require('request');
//var sprintf = require('sprintf').sprintf

var strftimetz = strftime.timezone(9 * 60);

// app.use(multer().none()); // multerでブラウザから送信されたデータを解釈する
app.use(bodyParser.urlencoded({ extended: false }));
const client = mysql.createConnection(conf.get('db'));

app.post('/beacon/alive', (req, res) => {
  // クライアントからの送信データを取得する
  const data = req.body;
  const ipaddress = data.ipaddress;
  const room = data.room;
  const id = data.id;

  var log = log_alive(ipaddress, room, id);
  // 追加した項目をクライアントに返す
  res.json(log);
});

app.post('/beacon/add', (req, res) => {
  // クライアントからの送信データを取得する
  const data = req.body;
  const label = data.label;
  const place = data.place;
  const proxi = data.proxi;
  const id = data.id;

  var log = log_room(label, place, proxi, id);
  // 追加した項目をクライアントに返す
  res.json(log);
});

app.get('/beacon/status', (req, res) => {
  client.query('select timestamp,substring(label,8,5) as label,note from log_lost_found where id in (select max(id) from log_lost_found group by label)',
    [], function (err, qres) {
      var target = {};
      for (var i = 0; i < qres.length; ++i) {
        var blest = {};
        blest.status = qres[i].note;
        blest.time = qres[i].timestamp;
        target["bt" + qres[i].label] = blest;
      }
      var json_ble = {};
      json_ble['target'] = target;
      res.json(json_ble);
    }
  );
});

var interval = conf.get("lost_detect_interval");

// 部屋特定と過去ログ削除
setInterval(determine_place, interval);
// 指定ポート(3001)でサーバを立てる
app.listen(conf.get("port"), () => console.log('Listening on port ' + String(conf.get("port"))));

//================================================================
// Functions
//================================================================

function determine_place() {
  var now = new Date() / 1000;
  //console.log("=== begin determine ===");
  //console.log(now); 
  // determine Lost
  client.query(
    'SELECT DISTINCT(label) FROM ble_tag WHERE active = 1 AND label NOT IN (SELECT label FROM room_log WHERE timestamp <= ? AND timestamp > ? GROUP BY label)',
    [now, now - (interval / 1000)], function (err, result) {
      if (err) throw err;
      //console.log("=== begin Lost detection ===");
      //console.log(result);
      //console.log(result.length);
      if (result.length > 0) {
        for (var j = 0; j < result.length; ++j) {
          if (result[j].label != null) {
            //console.log(sprintf("[beacon_%s] away",result[j].label));
            insert_lost_log(result[j].label);
          }
        }
      }
      //console.log("=== end Lost detection ===");
    });

  // determine Found
  client.query('SELECT distinct(label) FROM room_log WHERE timestamp <= ? AND timestamp > ?', [now, now - (interval / 1000)], function (err, beacons) {
    if (err) throw err;
    //console.log("=== begin Found detection ===");
    for (var i = 0; i < beacons.length; ++i) {
      client.query(
        'SELECT label,place,MIN(avgproxi) AS minavgproxi FROM room_status WHERE label=?',
        [beacons[i].label], function (err, result) {
          if (err) throw err;
          //console.log(result);
          //console.log(result.length);
          if (result.length > 0) {
            if (result[0].label != null || result[0].place != null) {
              //console.log(sprintf("[beacon_%s](%s = %5.2f)",result[0].label,result[0].place,result[0].minavgproxi));
              insert_found_log(result[0].label, result[0].place);
            }
          }
        });
    }
    //console.log("=== end Found detection ===");
  });

  // delete past data
  client.query(
    'delete from room_log where timestamp <= ?', [now - (interval / 1000)],
    function (err, result) {
      if (err) throw err;
    });
}

function insert_found_log(label, place) {
  client.query(
    'SELECT note FROM log WHERE label = ? ORDER BY id DESC LIMIT 1',
    ['target_' + label.toString() + '_status'], function (err, result) {
      if (err) throw err;
      if (result.length != 0) {
        if (result[0].note != 'Found_' + place.toString()) {
          client.query(
            'INSERT INTO log VALUES(null,?,?,null,?)',
            [
              strftimetz('%Y-%m-%d %H:%M:%S'),
              'target_' + label.toString() + '_status',
              'Found_' + place.toString(),
            ],
            function (err, info) {
              if (err) throw err;
            });
          // console.log(sprintf("[%s][LOG] Found %d",strftimetz('%Y-%m-%d
          // %H:%M:%S'),label));
        }
      } else {
        // for the first time
        client.query(
          'INSERT INTO log VALUES(null,?,?,null,?)',
          [
            strftimetz('%Y-%m-%d %H:%M:%S'),
            'target_' + label.toString() + '_status',
            'Found_' + place.toString(),
          ],
          function (err, info) {
            if (err) throw err;
          });
        // console.log(sprintf("[%s][NEW] Found %d",strftimetz('%Y-%m-%d
        // %H:%M:%S'),label));
      }
    });
}

function insert_lost_log(label) {
  if (label) {
    client.query(
      'SELECT note FROM log WHERE label = ? ORDER BY id DESC LIMIT 1',
      ['target_' + label.toString() + '_status'], function (err, result) {
        if (err) throw err;
        if (result.length != 0) {
          if (result[0].note != 'Lost') {
            client.query(
              'INSERT INTO log VALUES(null,?,?,null,?)',
              [
                strftimetz('%Y-%m-%d %H:%M:%S'),
                'target_' + label.toString() + '_status',
                'Lost',
              ],
              function (err, info) {
                if (err) throw err;
              });
            // console.log(sprintf("[%s][LOG] Lost %s",strftimetz('%Y-%m-%d
            // %H:%M:%S'),label));
          }
        }
      });
  }
}

function log_room(label, place, proxi, id) {
  const now = strftime('%s');
  client.query(
    'INSERT INTO room_log VALUES(null,?,?,?,?,?)',
    [now, label, place.toString(), proxi, id],
    function (err, info) {
      if (err) throw err;
    });

  var json =
    { timestamp: now, minor: label, place: place, accuracy: proxi, id: id };

  return json;
}

// send alive signal to db
function log_alive(ipaddress, room, id) {
  const now = strftimetz('%Y-%m-%d %H:%M:%S');
  client.query(
    'INSERT INTO alive2 VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE timestamp=?, ipaddress=?',
    //      'UPDATE alive SET timestamp=?, ipaddress=? WHERE label=? AND d_id=?',
    [
      room, id, ipaddress, now, now, ipaddress
    ],
    function (err, info) {
      if (err) throw err;
    });
  //    console.log("updated ibeacon db");
  var json =
    { timestamp: now, ipaddress: ipaddress, room: room, id: id };
  return json;
}

// send alive signal to db
function log_aqua_alive(ipaddress, label) {
  const now = strftimetz('%Y-%m-%d %H:%M:%S');
  client.query(
    'INSERT INTO alive VALUES(?,?,?) ON DUPLICATE KEY UPDATE timestamp=?, ipaddress=?',
    //      'UPDATE alive SET timestamp=?, ipaddress=? WHERE label=? AND d_id=?',
    [
      label, ipaddress, now, now, ipaddress
    ],
    function (err, info) {
      if (err) throw err;
    });
  //    console.log("updated ibeacon db");
  var json =
    { timestamp: now, ipaddress: ipaddress, label: label };
  return json;
}
