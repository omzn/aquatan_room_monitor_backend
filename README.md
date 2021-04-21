あくあたん在室管理システム (backend)
============================

* apiserver: データベースとビーコン検出器との橋渡し
* ibeacon_scanner: ビーコン検出器
* ibeacon: ビーコン

```
[BLE Nano]  BLE     [M5Stick C]                    WiFi  [サーバ]  mysql
ibeacon    ----->  
ibeacon    ----->  ibeacon_scanner (各部屋に設置) -----> apiserver.js -----> room_monitor
ibeacon    ----->  ibeacon_scanner (各部屋に設置)  
ibeacon    ----->  
```
## 用意するもの

* BLE Nano または 市販のiBeacon: 検知対象の人数分
* M5Stick-C or M5Stack (ESP32なら多少の改変でOK) : 検知したい部屋の数以上
* MySQLが動作するサーバ: 1台 （大量に書き込みと読み出しが発生するので注意）

## How to use

1. ibeaconの準備  (RedBear Lab BLE Nano)
  * iBeacon規格に則ったビーコンを作成する．
  * 先頭の`GIT_TO_DETECT`をグループ共通の数字にする．(major number)
  * `MY_UID` を個別の識別番号にする．この識別番号は後々使う．(minor number)
  * major, minorが固定で，minorがすべて異なる市販iBeaconを使ってもよい．
2. ibeacon_scannerの準備 (M5Stick-C)
  * 先頭の`GID_TO_DETECT`をビーコンと同じに設定する．
    * ビーコンのmajorがバラバラな場合，`DISABLE_GID_CHECK`を有効にする．
  * `DEFAULT_*`に初期値を設定する．（この設定は後からWeb経由で変更可能）
  * Partition Schemeを `Minimal SPIFFS`に設定しないと書き込めない．(スケッチが巨大)  
3. データベースの準備
  * (推奨) このシステムが使うデータベースを作成する．
  * `apiserver/setup_db.sql`を実行し，mysqlにデータベースを作成する．
  * `ble_tag` テーブルの `beacon`に利用するビーコンの識別番号を記述，`name`は対応する人名等を入れる．（管理のため）
  * `ble_tab` テーブルの `active`を1にするとそのビーコンが検出可能になる．
    * DBの`room_log`テーブルには，ビーコンからの生の情報が逐次書き込まれる．（一定時間以上経過したものは削除）
    * `room_log`の情報を2つのビュー(`room_detectors`->`room_status`)で集約し，`ble_tag`の情報と併せて`room_monitor`ビューで表示．
4. apiserverの準備
  * `apiserver/config/default.json`にDBへの接続情報を記述する．
```json
  {
    "db":{
        "host":"localhost",
        "database":"hogedb",
        "user":"hoge",
        "password":"hogehoge",
	"multipleStatements": true
    },
    "port": 3001,
    "lost_detect_interval": 60000	
}
``` 
  * `node apiserver.js`にて実行．3001ポートを待ち受けにして待機．
5. 確認
  * `room_monitor`ビューを眺める．
  * `log`テーブルには，検出と失探のログが残る．

## モニター (room_monitor)

* Mysql DB内のビュー．
* DBは任意のものを作成し，その上で`setup_db.sql`を実行．

## サーバ (apiserver/apiserver.js)

* node.jsにて記述
* mysqlデータベースとesp32の橋渡しをするapiサーバ．
* 初期はポート3001に設定している．

### 初期設定

0. 依存するnpmパッケージをインストール．`npm install`
1. config/default.jsonにmysqlの接続情報を投入．
3. pm2などで起動管理しておくと便利．

### api

* `/beacon/add`
  `POST label=??&detector_id=??&place=??`
  * `label`: iBeaconのminor number
  * `detector_id`: 検出器の室内id
  * `place`: 検出器のある部屋の識別子
* `/beacon/alive`
  `POST room=??&detector_id=??&ipaddress=??`
  * `room`: 検出器のある部屋の識別子(上のplaceと同じ物)
  * `detector_id`: 検出器の室内id
  * `ipaddress`: 検出器のipアドレス


## クライアント (ibeacon_scanner)

M5Stack/M5StickC用に作った．切り替えは先頭のこれをどちらか生かす．
```c
#define USE_M5STICKC
#define USE_M5STACK
```
* M5ボタンを押すと，自動割り当てIPアドレス，部屋識別子と検出器IDを確認できる．
  * 部屋を区別するために部屋識別子(`place`)を利用し，同部屋内に複数設置する検出器を区別するために`detector_id`を使う．

### API
* `/status`
* `/config`
  * `hostname`: ホスト名を指定 (OTAで利用)
  * `url_endpoint`: apiserverのアドレス，ポートをuriで指定
  * `api_beaconadd`, `api_beaconalive`: apiserverのAPIを（変更した場合などは）指定．
  * `place`: 部屋識別子の指定
  * `detector_id`: 部屋内検出器のIDを指定 
    * `place`+`detector_id`が一意になるように設定
  
### 起動直後
1. WiFiの設定をする．ssid: BEACON_SCAN に接続して，ssid, passwordの設定
2. `/config`からはホスト名(hostname), APIサーバのエンドポイント(url_endpoint), APIのパス(api_beaconadd, api_beaconalive), 部屋識別子(place), 部屋内検出器ID(detector_id)が設定できる．
3. 必ず，url_endpointの設定をする．
```
$ curl "http://scanner.local/config?url_endpoint=http://10.0.0.1:3001"
```
4. また，部屋識別子(place), 部屋内検出器ID(detector_id) はペアで一意になる必要がある．
```
$ curl "http://scanner.local/config?place=MyRoom&detector_id=0"
```
   3と4は同時に設定した方が早い．
```
$ curl "http://scanner.local/config?url_endpoint=http://10.0.0.1:3001&place=MyRoom&detector_id=0"
```

### 動作
1. 1秒ずつBLEをスキャンしてiBeaconを検出．
2. 検出したiBeaconについて，apiserver:/beacon/addにアクセスして登録
3. 一定時間(60秒)ごとに検出器の生存確認をapiserver:/beacon/aliveにアクセスして登録

## ibeacon

* RedBear Lab製 BLE Nano (V1.5, V2.0)用のビーコン発信器
* major, minorをプログラムに直書きして埋め込む．
* CR2032で6ヶ月〜1年運用可能．

## DB

* ble_tag テーブルに，最低限 label, name, activeを登録する．

-------

(c) omzn 2021