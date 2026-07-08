# Wi-SUN HAT スマートメーター連携サンプル（M5Unified / Arduino版）

参考: [https://github.com/rin-ofumi/m5stickc_wisun_hat](https://github.com/rin-ofumi/m5stickc_wisun_hat)

（本サンプラムは上記MicroPython/UIFlow版プロジェクトの考え方を参考に、Arduino(M5Unified)向けとして書き起こしたものです）

<br>

## 概要

ROHM製Wi-SUN通信モジュール「`BP35A1`」または「`BP35C1-J11-T01`」を使って、
家庭用スマートメーターから電力使用情報を取得するプログラムです。

![Ambient_WISUN_0](https://kitto-yakudatsu.com/wp/wp-content/uploads/2026/07/Wi-SUN%E6%A7%8B%E6%88%90%E3%82%A4%E3%83%A1%E3%83%BC%E3%82%B83.jpg)

「BP35A1」または「BP35C1-J11-T01」をM5StickCシリーズまたはM5AtomS3シリーズへ半田付け無しで簡単に装着する為のモジュール接続用キットを販売しております。
M5StickCシリーズ用として「[Wi-SUN HAT](https://kitto-yakudatsu.com/archives/7206)」が、M5AtomS3シリーズ用として「[ATOMIC Wi-SUN](https://kitto-yakudatsu.com/page-9404)」が存在します。
<br>
組み合わせるWi-SUNモジュール（「BP35A1」あるいは「BP35C1-J11-T01」）の違いでそれぞれ別商品となりますので、ご注意ください。（詳しくは下記リストをご参照下さい）

| ボード | 対象Wi-SUN モジュール | 接続キット | 接続キットの販売サイト |
|----|----|----|----|
| M5AtomS3系 | BP35A1 | ATOMIC Wi-SUN-A1 | [BOOTH](https://kitto-yakudatsu.booth.pm/items/8591747) |
| M5AtomS3系 | BP35C1 | ATOMIC Wi-SUN-C1 | [BOOTH](https://kitto-yakudatsu.booth.pm/items/8591747) |
| M5StickC系 | BP35A1 | Wi-SUN HAT（無印） | [BOOTH](https://kitto-yakudatsu.booth.pm/items/1650727) / [スイッチサイエンス](https://www.switch-science.com/catalog/7612/) |
| M5StickC系 | BP35C1 | Wi-SUN HAT-C1 | [BOOTH](https://kitto-yakudatsu.booth.pm/items/1650727) / [スイッチサイエンス](https://www.switch-science.com/catalog/9655/) |

![ATOMIC WISUN](https://kitto-yakudatsu.com/wp/wp-content/uploads/2026/07/%E5%A3%81%E4%BB%98%E3%81%91%E9%81%8B%E7%94%A8%E4%BE%8B_2.jpg)
![WISUN_HAT](https://kitto-yakudatsu.com/wp/wp-content/uploads/2019/10/%E8%A6%AA%E6%A9%9F_M5StickCPlus2-scaled.jpg)

<br>

この様な電力データグラフを取得出来るようになります。（Ambientを利用しています）

![Ambient_WISUN_1](https://kitto-yakudatsu.com/wp/wp-content/uploads/2019/10/瞬間電力計測値.png)

![Ambient_WISUN_2](https://kitto-yakudatsu.com/wp/wp-content/uploads/2019/10/30分毎積算電力量.png)

<br>

洗面所のドライヤー近くや、キッチンなどの大電力家電を使う場所にモニター子機を設置することで、うっかりブレーカーを落とす危険を排除します。

![Ambient_ENV_3](https://kitto-yakudatsu.com/wp/wp-content/uploads/2019/10/%E6%B4%97%E9%9D%A2%E6%89%80_1-scaled.jpg)

![Ambient_ENV_4](https://kitto-yakudatsu.com/wp/wp-content/uploads/2019/10/%E5%AD%90%E6%A9%9F_M5StickC-scaled.jpg)

![Ambient_ENV_5](https://kitto-yakudatsu.com/wp/wp-content/uploads/2019/10/%E5%AD%90%E6%A9%9F_M5Stack-scaled.jpg)


## 主な機能

- M5Unified.h による画面・ボタン・ブザー・IMU(加速度センサー)の制御
- 加速度センサーによる画面表示の自動回転（重力方向を検知して0/90/180/270度に追従）
- ESPNOWによる取得データのブロードキャスト配信（子機側で受信して利用可能）
- [Ambient](https://ambidata.io/) へのデータ送信（任意、瞬間電力値/積算電力量を別チャンネルで送信）
- SoftAP + ブラウザによる設定ポータル（WiFi・Bルート認証情報・各種しきい値を、
  設定ファイルの編集なしでブラウザから入力可能。設定値は内蔵Flash(NVS)に保存）
- UNIT Buzzer（M5Stack社製、Groveポート接続の外付けブザー）への対応
- BP35A1（Wi-SUN HAT）・BP35C1-J11-T01（Wi-SUN HAT-C1）の両方に対応
  （コンパイル時に切替）

## 対象ボード・対象Wi-SUNモジュール
| 分類 | 名称 |
|----|----|
| ボード | M5StickC（未完成） <br> M5StickC Plus（未完成） <br> M5StickC Plus2（未完成） <br> M5StickS3（未完成） <br> M5AtomS3 <br> M5AtomS3R |
| Wi-SUNモジュール | BP35A1 <br> BP35C1-J11-T01 |
| モジュール接続用キット | Wi-SUN HAT（無印） <br> Wi-SUN HAT-C1 <br> ATOMIC Wi-SUN-A1 <br> ATOMIC Wi-SUN-C1 |

M5Unifiedが実行時にボード種別を自動判別しますが、UARTピンの割当てや
ブザーの有無はボードごとに異なるため、後述の通りコンパイル時に選択します。
**※M5StickC部分の機能実装はまだ未完成です。不具合が多数ありますのでご注意ください。**

## ファイル構成

```
wisun_Arduino/
├── wisun_Arduino.ino   … メインスケッチ（setup/loop、画面表示、ボタン処理など）
├── WisunUdp.h          … スマートメーターからの応答(ECHONET Lite)を解析するパーサー
├── WisunC1.h            … BP35C1-J11-T01用バイナリプロトコル送受信ドライバ
├── ConfigPortal.h       … 設定値の保存(NVS)＋Web設定ポータル
└── README.md
```

Arduino IDEで開く場合は、フォルダごと `wisun_Arduino` という名前にして、スケッチフォルダとして開いてください。
（`.ino`ファイルと同名のフォルダである必要があります）。

## 必要なライブラリ

- **M5Unified**（M5Stack公式、Arduino Library Managerから導入）
- **Ambient ESP32 ESP8266 lib**（Ambient公式、Library Managerで"Ambient"を検索。
  Ambient連携を使わない場合もincludeしているためインストールが必要です）
- ESP32 Arduino core 同梱: `WiFi.h`, `WebServer.h`, `DNSServer.h`,
  `Preferences.h`, `esp_now.h`

## 検証時バージョン（参考）

- Arduino IDE: 2.3.10
- M5Stackボードマネージャ: 3.3.7
- M5Unified: 0.2.17
- Ambient ESP32 ESP8266 lib: 1.0.5

## 対象ボード・対象チップの選択（コンパイル時設定）

`wisun_Arduino.ino` の冒頭に、ボードとWi-SUNチップそれぞれの選択スイッチが
あります。使用する組み合わせの行だけコメントを外し、他方はコメントアウト
してください。（ボード・チップ、それぞれ必ず1つだけ有効にすること）

```cpp
// #define TARGET_BOARD_STICKC     // M5StickC / M5StickC Plus / M5StickC Plus2 / M5StickS3
#define TARGET_BOARD_ATOMS3  // M5AtomS3（ブザー無し）/ M5AtomS3R（ブザー無し）

// #define TARGET_CHIP_A1           // BP35A1（Wi-SUN HAT 無印 / ATOMIC Wi-SUN-A1）
#define TARGET_CHIP_C1        // BP35C1-J11-T01（Wi-SUN HAT-C1 / ATOMIC Wi-SUN-C1）
```

BP35A1とBP35C1-J11-T01は通信プロトコルそのものが異なります
（BP35A1はテキストの`SKxxxx`+改行のATコマンド、BP35C1はユニークコード＋
チェックサム付きの独自バイナリフレーム）。そのため実行時の自動判別は行わず、
コンパイル時にどちらか一方を選ぶ方式にしています。

この選択に連動して、UART/RESETピンとBEEP(ブザー)機能の有無が自動的に切り替わります。

### ピン割当て一覧

| ボード | チップ | TX | RX | RESET |
|--------|--------|----|----|-------|
| M5StickC系 | BP35A1 | G0 | G26 | (不要) |
| M5StickC系 | BP35C1 | G0 | G36 | G26 |
| M5AtomS3 | BP35A1 | G6 | G7 | (不要) |
| M5AtomS3 | BP35C1 | G6 | G7 | G5 |

※`Wi-SUN HAT`または`ATOMIC Wi-SUN`を使用する前提のGPIOポート割り当てになっています。

### BP35C1側の接続シーケンスについて

BP35C1側の実装（`WisunC1.h` + `.ino`内のBP35C1関連関数）は、リセット→
起動通知待ち→初期化→Bルート認証情報設定→アクティブスキャン→
チャンネル確定→Bルート開始→UDPポートOPEN→PANA認証、という順序で
コマンドをやり取りします。受信したUDPデータ通知（0x6018）は、
`WisunUdp.h`のパーサーにそのまま渡せる疑似的な`ERXUDP ...`形式の
文字列に変換しており、BP35A1/BP35C1で受信データの解析ロジックを
共通化しています。

## 加速度センサーによる画面自動回転

M5.Imuで取得した重力方向(ax/ay)から、画面回転(0/90/180/270度)を自動的に
判定します。判定にはヒステリシスを設けており、机の上に平置きした際などの
細かな向きの揺れでチラチラ切り替わらないようにしています。

ボタンBのシングルクリックで、自動回転のON/OFFを切り替えられます
（OFFにするとその時点の向きに固定されます）。

IMUの軸とパネルの物理的な取付け方向の対応関係は機種によって異なることが
あります。実機で向きが逆や90度ズレる場合は、`updateAutoRotation()`内の
判定部分（`ax`/`ay`に対する不等号の向き）を入れ替えて調整してください。
Serial出力でax, ayの値を確認しながら調整すると分かりやすいです。

## 初期セットアップ手順

1. 書き込み後、初回起動時は自動的に設定ポータル（SoftAP）が起動します。
   （2回目以降、設定をやり直したい場合は **ボタンAを押しながら電源ON**）
2. スマホ/PCでWiFi「`WiSUN-HAT-Setup`」に接続します。
3. ブラウザで `http://192.168.4.1/` を開きます（自動的にポップアップされない
   場合は手動で開いてください）。
4. 以下を入力して保存します。
   - WiFi SSID / パスワード（ESPNOW用にSTAモードを使うため。インターネット
     接続自体は必須ではありません）
   - Bルート認証ID（32文字）/ パスワード（12文字）
   - 契約ブレーカー値、警告係数、無応答タイムアウト、瞬時電力要求サイクル
   - ESPNOWブロードキャストを使うかどうか
   - （任意）UNIT Buzzerを使うかどうか（主にM5AtomS3向け）
   - （任意）Ambient連携（瞬間電力用/積算電力用、それぞれのチャンネルID・ライトキー）
   - 電力値表示のフォント・表示倍率、バックライト輝度
5. 保存すると自動的に再起動し、通常動作（スマートメーターへの接続）を開始します。

Wi-SUNのチャンネルスキャン結果（チャンネル/PAN ID/MACアドレス/係数/単位）は
内蔵Flash(NVS)にキャッシュされ、次回起動時はスキャンを省略します。
スマートメーター側の使用チャンネルが変わるなどしてキャッシュの内容が古く
なった場合は、後述の「スキャンキャッシュの自動更新」の通り、自動的に
キャッシュを破棄して再スキャンします。

## ボタン操作

| ボタン | 操作 | 動作 |
|--------|------|------|
| ボタンA | シングルクリック | BEEP(ブザー警告)のON/OFF切替 |
| ボタンA | 起動時に押しながら電源ON | 設定ポータル（SoftAP）を起動 |
| ボタンB（M5Stick系のみ） | シングルクリック | 加速度センサーによる画面自動回転のON/OFF切替 |

BEEPのON/OFF切替は、内蔵ブザー（`TARGET_BOARD_STICKC`）またはUNIT Buzzer
（設定ポータルで有効化した場合）のいずれかが使える状態でのみ反応します。

バックライト輝度は本体操作ではなく、Web設定ポータルの「画面表示設定」で
固定値（10〜255）を指定します。契約アンペア超過の警告時は、設定値に
関わらず自動的に最大輝度になります。

## 画面表示について

- **電力値**: 4桁の数字が画面幅の約80%になるよう、フォント表示倍率を
  自動計算します（設定ポータルで表示倍率を0以外に指定すると手動固定も可能）。
  画面回転で幅が変わった場合も、その都度計算し直されます。
  単位の"W"は、選んだフォントに数字以外の字形が無いことがあるため常に
  標準フォントを使用し、数値の右下に改行した形で表示します。
- **BEEP表示**: 有効時、右上に緑文字で小さく「BEEP」と表示します。
  ブザー機能自体が使えない状態（内蔵ブザーもUNIT Buzzerも無効）の場合は
  表示されません。
- **Ambientステータス丸**: 白いリング＋中心の色で送信状況を表します。
  - 黒: まだ1回も送信していない（待機中）
  - 緑: 直前の送信に成功
  - 赤: 直前の送信に失敗
  - 瞬間電力用は画面左上、積算電力量用は画面左下に表示します
    （設定ポータルで該当チャンネルを有効にしている場合のみ表示）。

フォント・表示倍率・バックライト輝度は、いずれも設定ポータルの
「画面表示設定」から変更できます。

## Ambient連携

**瞬間電力値**と**30分毎積算電力量**を、別々のAmbientチャンネルとして
送信する構成です。設定ポータルの「Ambient連携（任意）」で、それぞれ
個別に有効化・チャンネルID・ライトキーを入力できます。

**瞬間電力値**
- チャンネルID／ライトキー（[Ambient](https://ambidata.io/) でチャンネル作成後に確認できます）
- 送信間隔[秒]（Ambient無料枠は5秒/回が下限のため、5秒未満は自動的に5秒に補正されます）
- スマートメーターから瞬間電力値(E7)を受信するたびに送信間隔を確認し、`d1`に電力値[W]をセットして送信します

**30分毎積算電力量**
- チャンネルID／ライトキー
- 送信間隔の設定はありません。スマートメーターから30分毎の積算電力量(EA72)
  を受信するたびに、間隔を空けず都度送信します
- `d1`に積算電力量[kWh]、`created`（Ambientライブラリ上はフィールド番号11）
  にスマートメーターの実際の計測時刻（`YYYY-MM-DD HH:MM:SS`）をセットして送信します

いずれもWiFiが接続されていない場合は送信されません。

## UNIT Buzzer対応について

[UNIT Buzzer](https://docs.m5stack.com/ja/unit/buzzer)はSIGNAL 1本にPWM方形波を
入力するだけの単純なパッシブブザーです。M5AtomS3のGroveポートに接続することで、
ESP32のLEDC(PWM)機能から直接鳴らせます（M5Stack社の専用ライブラリは不要です）。

- 設定ポータルの「運用設定」内、「UNIT Buzzer（Grove接続の外付けブザー）を使う」
  チェックボックスで有効化します。
- 有効化すると、内蔵ブザーと同じロジック（契約アンペア超過時に約2.1秒おきに
  220Hz・200msの警告音、ボタンAクリックでON/OFF切替、画面右上への「BEEP」表示）
  が使えるようになります。内部的には、内蔵ブザーとUNIT Buzzerのどちらでも
  同じ`soundTone()`関数から鳴らす共通実装にしています。
- 接続ピンは`wisun_Arduino.ino`内の以下の定義で変更できます（Groveポートの
  SIGNAL線をこのGPIOへ接続してください）。

```cpp
#define UNIT_BUZZER_PIN     2   // AtomS3のGroveポート(G2)。配線に合わせて変更可
#define UNIT_BUZZER_CHANNEL 4   // ledcのPWMチャンネル番号
```

- UNIT BuzzerはGPIO PWM駆動で、SIGNAL線はG2に接続する想定です。配線を変える場合は
  `UNIT_BUZZER_PIN`の値も合わせて変更してください。
- StickC系（内蔵ブザーあり）でも、配線すれば同時にUNIT Buzzerを使うこと自体は
  可能ですが、通常は内蔵ブザーのみで十分なため想定用途はM5AtomS3向けです。
- ESP32 Arduino coreのバージョン差（2.x系の`ledcSetup`/`ledcAttachPin`と、
  3.x系の`ledcAttach`）を自動判定するコードにしていますが、コンパイル
  エラーになった場合はお使いのcoreのLEDC API（`ledcWriteTone`等の引数が
  チャンネル番号かピン番号か）に合わせて調整してください。

## ESPNOW受信側（モニター子機）について

Arduino版の子機プログラムは未作成ですが、ESPNOWで送信されるデータは[元となったMicroPython/UIFlow版](https://github.com/rin-ofumi/m5stickc_wisun_hat)の子機プログラムと互換性があります。

`NPD=xxx`（瞬時電力値）、`TPD=xxx/日時`（積算電力量）という文字列形式で
ブロードキャスト配信しています。子機側で受信する場合は、
`esp_now_register_recv_cb()`で受信データの先頭が`"NPD="`か`"TPD="`かを
見て処理を分岐してください。

## スキャンキャッシュの自動更新

スマートメーター側の使用チャンネルが変わるなどして、保存済みのスキャン
キャッシュ（チャンネル/PAN ID/MACアドレス）が実態と合わなくなることが
あります。この場合、接続シーケンスの該当コマンドが失敗しますが、
本プログラムは以下の方針で自動的に復旧を試みます。

- BP35C1: チャンネル再初期化やPANA認証に失敗した場合、スキャンキャッシュを
  破棄した上で再起動し、次回起動時に自動的にアクティブスキャンからやり直します。
- BP35A1: チャンネル設定(`SKSREG S2`)・PAN ID設定(`SKSREG S3`)・MACアドレスの
  IPv6変換(`SKLL64`)・接続要求(`SKJOIN`)が失敗した場合も同様に、キャッシュを
  破棄して再起動します。これらはPANA接続の直前まで到達しており、Wi-SUN
  モジュール自体は生きていることが確認済みのため、失敗の原因は「キャッシュの
  内容が古い」可能性が高いと判断しているためです。
  なお、スキャンキャッシュに依存しないコマンド（`SKSREG SFE 0`など）が
  失敗した場合は、純粋な通信異常とみなしてエラー表示のまま停止します。

これにより、スマートメーター側のチャンネルが変わった場合などでも、
手動でのリセット操作なしに自動的に再スキャン・再接続を試みます。

## シリアルログについて

**検証時のデバッグ向けにUSBシリアルでコマンドログを出力しています。**

- 起動シーケンスの各コマンドで、Wi-SUNモジュールから一定時間内に応答が
  得られなかった場合は、画面・シリアル双方にエラーを表示して停止します
  （`SKLL64`・`SKJOIN`など一部のコマンドは、瞬断を考慮して既定で5回まで
  リトライしてから停止します）。
  ただし、スマートメーター宛のコマンド（積算電力量係数/単位の取得要求など）は、
  対応していないEPCへの問い合わせのように無応答自体が正常なケースもあるため、
  一定時間でタイムアウトしてデフォルト値（係数=1、単位=0.1）にフォールバック
  する仕様のままにしています。
- スマートメーターから瞬時電力値(E7)・30分毎積算電力量(EA72)のいずれかを
  受信するたびに、その時点で分かっている両方の値を1行にまとめてシリアル
  出力します。

  ```
  [POWER] instant=123W / total=456.78kWh @ 2024-01-01 12:30:00
  ```

  積算電力量をまだ受信していない場合は `total=N/A(not received yet)` と表示されます。
 
## 実機での確認ポイント

書き込み後、実機でシリアルモニタ（115200bps）を見ながら、特に以下を
確認することをおすすめします。

- チャンネルスキャン・PANA認証が正常に完了するか
- M5Unifiedのボタン関数名（`wasClicked()` / `wasDoubleClicked()`）が、
  お使いのM5Unifiedのバージョンと一致しているか（バージョンによっては
  `wasPressed()`等に読み替えが必要な場合があります）
- `TARGET_CHIP_C1`使用時: 各コマンドコード・データ位置はBP35C1-J11-T01の
  UART IFコマンド仕様書を参照していますが、ファームウェアバージョンに
  よって細部が異なる可能性があるため、Serial出力で`retcmd()`の値を
  確認しながら調整してください。
- Ambient連携使用時: `ambient.begin()`/`ambient.set()`/`ambient.send()`の
  引数・戻り値はAmbient公式ライブラリのバージョンによって異なる場合が
  あります。送信が失敗する場合はチャンネルID/ライトキーとWiFi接続状態を
  確認してください。なお`created`フィールドの番号(11)は、Ambient公式
  ライブラリ（AmbientDataInc/Ambient_ESP8266_lib）の実装から算出したもの
  で、`d1`〜`d8`=1〜8、`lat`=9、`lng`=10、`created`=11に対応します。
  ライブラリのバージョンが大きく異なる場合はソースを確認してください。

## 仕様上の簡略化

- 画面描画は座標を細かく指定する方式ではなく、M5GFXの`setTextDatum`/
  `drawString`による中央揃え表示にしています。表示位置を細かく調整したい
  場合は`drawPower()`を変更してください。
- NTPによる時刻取得は行っていません（画面表示にも使用していません）。
- BEEP音は`loop()`内でmillis()による非ブロッキングタイマー処理にしています。
- ボタン入力は割り込みではなく、`M5.update()`を毎ループ呼ぶポーリング方式
  です（M5Unified標準の作法です）。

## ライセンス

本プロジェクトは [MITライセンス](LICENSE) で公開しています。
<br>
プログラム中で使用している他のライブラリについては、各ライブラリのライセンスに従って下さい。

本Arduino版は、著作権者本人（rin_ofumi）による [rin-ofumi/m5stickc_wisun_hat](https://github.com/rin-ofumi/m5stickc_wisun_hat)
（MicroPython/UIFlow版）の考え方を踏まえて書き起こしたものです。

## 更新履歴

### Rev 0.0.1 【2026/7/8】
- ATOMIC Wi-SUN用（M5AtomS3系）として仮リリース。 ※Wi-SUN HAT用（M5Stick系）はまだ不備が多いです。