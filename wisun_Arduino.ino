// =====================================================================
//  wisun_Arduino.ino
//
//  M5StickC/Plus/Plus2 または M5AtomS3 / M5AtomS3R と、
//  Wi-SUN HAT(BP35A1) / Wi-SUN HAT-C1(BP35C1-J11-T01) 
//  または ATOMIC WiSUN-A1 / ATOMIC WiSUN-C1
//  を組み合わせて、電力会社のスマートメーターからBルート経由で瞬時電力値・積算電力量を取得し、
//  本体画面に表示するサンプルプログラムです。
//
//  主な機能:
//   ・M5Unified.h による画面/ボタン/ブザー/IMU制御
//   ・加速度センサーによる画面表示の自動回転
//   ・ESPNOWによる取得データのブロードキャスト配信（子機側で受信可能）
//   ・Ambient(https://ambidata.io/)へのデータ送信（任意）
//   ・SoftAP + ブラウザによる設定ポータル
//     （WiFi/Bルート認証情報/各種しきい値を、設定ファイルの編集なしで
//     ブラウザから入力可能。設定値は内蔵Flash(NVS/Preferences)に保存）
//   ・UNIT Buzzer（M5Stack社製、Groveポート接続の外付けブザー）に対応
//
//  必要ライブラリ (Arduino IDE / PlatformIO):
//   - M5Unified                  (M5Stack公式)
//   - Ambient ESP32 ESP8266 lib  (Ambient公式、Library Managerで"Ambient"検索)
//   - WiFi / WebServer / DNSServer / Preferences / esp_now
//     （いずれも ESP32 Arduino core 同梱）
//
//  同じフォルダに WisunUdp.h・WisunC1.h・ConfigPortal.h が必要です。
// =====================================================================

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Ambient.h>   // Ambient連携用（Arduino Library Manager で "Ambient" を検索してインストール）
#include <math.h>      // floorf() 使用のため

#include "WisunUdp.h"
#include "WisunC1.h"
#include "ConfigPortal.h"

// ------------------------------------------------------------------
// 対象ボード ＆ 対象Wi-SUNチップ の選択
//  それぞれ、使用する方の行だけコメントを外し、もう片方はコメント
//  アウトしてください（ボード・チップ、各1つだけ有効にすること）。
//  UARTピン・RESETピン・BEEP(ブザー)機能の有無がこの選択に連動します。
// ------------------------------------------------------------------
// #define TARGET_BOARD_STICKC     // M5StickC / M5StickC Plus / M5StickC Plus2 / M5StickS3
#define TARGET_BOARD_ATOMS3  // M5AtomS3（ブザー無し）/ M5AtomS3R（ブザー無し）

// #define TARGET_CHIP_A1           // BP35A1（Wi-SUN HAT 無印 / ATOMIC Wi-SUN-A1）
#define TARGET_CHIP_C1        // BP35C1-J11-T01（Wi-SUN HAT-C1 / ATOMIC Wi-SUN-C1）

// ---- UART / RESET ピン割当て ----
#if defined(TARGET_BOARD_STICKC) && defined(TARGET_CHIP_A1)
  #define WISUN_UART_TX_PIN  0
  #define WISUN_UART_RX_PIN  26
  // BP35A1にはRESETピン制御は不要

#elif defined(TARGET_BOARD_STICKC) && defined(TARGET_CHIP_C1)
  #define WISUN_UART_TX_PIN  0
  #define WISUN_UART_RX_PIN  36
  #define WISUN_RESET_PIN    26

#elif defined(TARGET_BOARD_ATOMS3) && defined(TARGET_CHIP_A1)
  #define WISUN_UART_TX_PIN  6
  #define WISUN_UART_RX_PIN  7
  // BP35A1にはRESETピン制御は不要

#elif defined(TARGET_BOARD_ATOMS3) && defined(TARGET_CHIP_C1)
  #define WISUN_UART_TX_PIN  6
  #define WISUN_UART_RX_PIN  7
  #define WISUN_RESET_PIN    5

#else
  #error "TARGET_BOARD_* / TARGET_CHIP_* は、それぞれ1つずつ定義してください"
#endif

// ---- ブザー(BEEP)機能の有無（ボード依存） ----
#if defined(TARGET_BOARD_STICKC)
  #define HAS_BUZZER 1
#elif defined(TARGET_BOARD_ATOMS3)
  #define HAS_BUZZER 0            // M5AtomS3はブザー非搭載のためBEEP機能を無効化
#endif

HardwareSerial WisunSerial(1); // UART1を使用

// ------------------------------------------------------------------
// 固定コマンド（スマートメーターへ送るECHONET Liteフレーム）
//
// ECHONET Liteフレームは、家電などの機器をネットワーク経由で制御・監視する
// ための共通プロトコルです。ここで送るのは「Get（値を教えて）」という
// 要求で、各バイトの意味は次の通りです（本プログラムで使う4種類は
// すべて同じ形で、最後のEPC(知りたい項目のコード)だけが異なります）。
//
//   0x10,0x81         : EHD (ECHONET Liteヘッダ。固定値)
//   0x00,0x01         : TID (トランザクションID。応答と要求を対応付ける番号)
//   0x05,0xFF,0x01    : SEOJ (送信元=コントローラ)
//   0x02,0x88,0x01    : DEOJ (宛先=低圧スマート電力量メータ)
//   0x62              : ESV (サービスコード。0x62=Get、値を要求する)
//   0x01              : OPC (プロパティ数。ここでは1個だけ問い合わせる)
//   0xD3/E1/E7/EA     : EPC (知りたいプロパティのコード。下記コメント参照)
//   0x00              : PDC (要求時のデータ長。Getでは0)
// ------------------------------------------------------------------
static const uint8_t GET_COEFFICIENT[] = {   // EPC=0xD3: 積算電力量係数
  0x10,0x81,0x00,0x01,0x05,0xFF,0x01,0x02,0x88,0x01,0x62,0x01,0xD3,0x00
};
static const uint8_t GET_TOTAL_POWER_UNIT[] = { // EPC=0xE1: 積算電力量単位
  0x10,0x81,0x00,0x01,0x05,0xFF,0x01,0x02,0x88,0x01,0x62,0x01,0xE1,0x00
};
static const uint8_t GET_NOW_P[] = {         // EPC=0xE7: 瞬時電力計測値
  0x10,0x81,0x00,0x01,0x05,0xFF,0x01,0x02,0x88,0x01,0x62,0x01,0xE7,0x00
};
static const uint8_t GET_TOTAL_POWER_30[] = { // EPC=0xEA: 定時積算電力量(30分毎)
  0x10,0x81,0x00,0x01,0x05,0xFF,0x01,0x02,0x88,0x01,0x62,0x01,0xEA,0x00
};

static const int SCAN_COUNT = 6;    // アクティブスキャン試行回数
static const int RES_TOUT   = 10;   // コマンド応答待ちタイムアウト[秒]

// ------------------------------------------------------------------
// グローバル状態
// ------------------------------------------------------------------
WisunUdp        u;
WisunHatConfig  cfg;
WisunScanCache  scanCache;
ConfigPortal    portal;

String ipv6Addr = "";  // BP35A1用: SKLL64で得たIPv6アドレス

#if defined(TARGET_CHIP_C1)
WisunC1 wisunC1(WisunSerial);
std::vector<uint8_t> c1_iph;   // 送信先IPv6アドレス前半(固定: fe80::)
std::vector<uint8_t> c1_ipl;   // 送信先IPv6アドレス後半(スマートメーターMACより生成)
std::vector<uint8_t> c1_port;  // 送受信元ポート(固定: 0e1a/0e1a)
#endif

// 加速度センサー(IMU)による画面自動回転の状態
bool imuAvailable    = false;
bool autoRotateEnabled = true;   // false時はcurrentRotationに固定（ボタンBでトグル）
int  currentRotation   = 0;      // 現在の画面回転(0/1/2/3)
int  rotationCandidate = 0;      // ヒステリシス判定用の暫定値
unsigned long rotationCandidateSince = 0;

bool dataMute    = false;  // 電力値の表示ミュート（タイムアウト時）
bool beepEnabled = true;   // BEEP有効/無効（ボタンAクリックでトグル）

// 内蔵ブザー(HAS_BUZZER)か、UNIT Buzzer(外付け, cfg.unitBuzzerEnable)のいずれかが
// 使える状態かどうか。BEEP表示・トグル操作の有効/無効判定に使う。
bool buzzerAvailable() {
  return HAS_BUZZER || cfg.unitBuzzerEnable;
}

// Ambientステータス丸の状態: 0=非表示 / 1=待機中 / 2=送信成功 / 3=送信失敗
int ambientNowStatus   = 0;
int ambientTotalStatus = 0;

uint8_t broadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

unsigned long np_tc  = 0;   // 瞬時電力要求タイマ
unsigned long tp_tc  = 0;   // 積算電力要求タイマ
unsigned long cmd_tc = 0;   // コマンド応答待ちタイマ
unsigned long ambientNow_tc = 0; // Ambient(瞬間電力)送信タイマ
bool tp_f  = false;         // 積算電力量、直近受信済みか
bool cmd_w = false;         // コマンド送信中(排他)フラグ
int  cmd_rc = 0;            // コマンド再送回数

// Ambient連携用。瞬間電力値と積算電力量を、それぞれ別チャンネルへ送信する構成
WiFiClient ambientNowClient;
Ambient    ambientNow;
WiFiClient ambientTotalClient;
Ambient    ambientTotal;

String rxLineBuf = "";      // メインループ用UART受信バッファ

// ====================================================================
//  ユーティリティ
// ====================================================================

// UARTへ1行送信（\r\n付加）
void uartWriteLine(const String &s) {
  WisunSerial.print(s);
  WisunSerial.print("\r\n");
}

// タイムアウト付きで1行読む（空なら空文字列）。setup()中のブロッキング用。
String readLineBlocking(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WisunSerial.available()) {
      String line = WisunSerial.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) return line;
    }
    delay(1);
  }
  return "";
}

// "OK" で始まる行が来るまで待つ（全体タイムアウトで諦める）
bool waitForOK(unsigned long totalTimeoutMs) {
  unsigned long start = millis();
  while (millis() - start < totalTimeoutMs) {
    String line = readLineBlocking(200);
    if (line.length() == 0) continue;
    if (line.startsWith("OK")) return true;
  }
  return false;
}

void progressDot() {
  // 起動シーケンスの進捗状況を、画面左上に "*" を1文字ずつ追加しながら表示する
  static String dots = "";
  dots += "*";
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.print(dots);
}

void fatalError(const String &msg) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.println(msg);
  Serial.println(msg);
  while (true) { delay(1000); }
}

// ====================================================================
//  表示関連
// ====================================================================

int screenW() { return M5.Display.width(); }
int screenH() { return M5.Display.height(); }

// 加速度センサー(重力方向)から画面回転(0/1/2/3)を自動判定する。
// ヒステリシス(ROT_HOLD_MS)を設けており、机の上に平置きした際などの
// 細かな向き変化でチラチラ切り替わらないようにしている。
//
// ※ IMUの軸とパネルの物理的な取付け方向の対応関係は機種によって異なる
//   ことがあります。実機で向きが逆/90度ズレる場合は、下の判定部分の
//   axis(ax/ay)や不等号の向きを入れ替えて調整してください
//   （Serial出力でax,ayの値を確認しながら調整すると分かりやすいです）。
static const float ROT_TH_ENTER = 0.55f;  // この値を超えたら回転候補にする
static const unsigned long ROT_HOLD_MS = 350; // 同じ向きがこの時間続いたら確定

void updateAutoRotation() {
  if (!imuAvailable || !autoRotateEnabled) return;

  if (!M5.Imu.update()) return; // 新しいサンプルが無ければ何もしない

  auto data = M5.Imu.getImuData();
  float ax = data.accel.x;
  float ay = data.accel.y;

  int candidate = currentRotation;
  if (ay >= ROT_TH_ENTER)       candidate = 0; // 上向き（標準の持ち方）
  else if (ay <= -ROT_TH_ENTER) candidate = 2; // 180度（上下逆さ）
  else if (ax >= ROT_TH_ENTER)  candidate = 1; // 時計回り90度
  else if (ax <= -ROT_TH_ENTER) candidate = 3; // 反時計回り90度
  else return; // 重力成分が弱い(ほぼ水平に置かれている等)場合は判定しない

  if (candidate != rotationCandidate) {
    rotationCandidate = candidate;
    rotationCandidateSince = millis();
  }
  if (candidate != currentRotation && millis() - rotationCandidateSince >= ROT_HOLD_MS) {
    currentRotation = candidate;
    M5.Display.setRotation(currentRotation);
    redraw();
  }
}

void drawBeepBadge() {
  if (!buzzerAvailable() || !beepEnabled) return;
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_right);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.drawString("BEEP", screenW() - 2, 2);
}

// Ambientステータス丸: 白リング＋中心の色で状態を示す
//  status: 0=非表示 / 1=待機中(黒) / 2=直前の送信成功(緑) / 3=直前の送信失敗(赤)
void drawAmbientDot(int cx, int cy, int status) {
  if (status == 0) return;
  uint16_t center = TFT_BLACK;
  if (status == 2) center = TFT_GREEN;
  else if (status == 3) center = TFT_RED;
  M5.Display.fillCircle(cx, cy, 5, TFT_WHITE);
  M5.Display.fillCircle(cx, cy, 3, center);
}

void drawAmbientStatusDots() {
  int margin = 10;
  if (cfg.ambientNowEnable) {
    drawAmbientDot(margin, margin, ambientNowStatus);       // 瞬間電力: 左上
  }
  if (cfg.ambientTotalEnable) {
    drawAmbientDot(margin, screenH() - margin, ambientTotalStatus); // 積算電力: 左下
  }
}

void drawPower() {
  M5.Display.fillScreen(TFT_BLACK);

  drawBeepBadge();
  drawAmbientStatusDots();

  bool show = u.instant_power_valid && !dataMute;
  if (!show) return; // タイムアウト/未受信時は数値非表示（黒背景のみ）

  uint16_t fc = TFT_WHITE;
  bool warn = (u.instant_power >= (long)(cfg.ampereLimit * cfg.ampereRed * 100));
  if (warn) {
    fc = TFT_RED;
    M5.Display.setBrightness(255); // 警告時は最大輝度で強調
  } else {
    M5.Display.setBrightness(cfg.backlightBrightness); // 通常時はWeb設定の輝度
  }

  int w = screenW(), h = screenH();

  // 電力値本体（Web設定で選んだフォント・倍率を使用。倍率0=自動計算）
  M5.Display.setTextFont(cfg.fontId);

  float scale = cfg.fontScale;
  if (scale <= 0.0f) {
    // 自動計算: 4桁の数字("8888")が画面幅の約80%になる倍率を実測して求める
    M5.Display.setTextSize(1);
    int refW = M5.Display.textWidth("8888");
    if (refW < 1) refW = 1;
    scale = (float)w * 0.8f / (float)refW;
    if (scale < 1.0f) scale = 1.0f;
    scale = floorf(scale * 2.0f) / 2.0f; // 0.5刻みに丸める
  }
  M5.Display.setTextSize(scale);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(fc, TFT_BLACK);
  String numStr = String(u.instant_power);
  int numW = M5.Display.textWidth(numStr);
  int numH = M5.Display.fontHeight();
  M5.Display.drawString(numStr, w / 2, h / 2);

  // 単位"W"は、選んだフォントに数字以外の字形が無いことがあるため、
  // 常に標準フォントを使う。大きさは数値フォントの1.2倍、
  // 位置は数値の右下に改行した形で表示する。
  float wScale = scale * 0.4f * 3.0f;
  if (wScale < 1.0f) wScale = 1.0f;
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(wScale);
  M5.Display.setTextColor(fc, TFT_BLACK);
  M5.Display.setTextDatum(top_right); // 基準点を右上にして、数値の右下へ配置する
  int wx = w / 2 + numW / 2;
  int wy = h / 2 + numH / 2 + 2; // 数値の下端から少し余白を空ける
  M5.Display.drawString("W", wx, wy);
}

void redraw() {
  drawPower();
}

// ====================================================================
//  UNIT Buzzer（M5Stack社製、Groveポート接続の外付けパッシブブザー）
//  https://docs.m5stack.com/ja/unit/buzzer
//  SIGNAL 1本にPWM方形波を入力するだけの単純なパッシブブザーのため、
//  ESP32のLEDC(PWM)機能で直接駆動できる。主にBEEP非搭載のM5AtomS3向け。
// ====================================================================
#define UNIT_BUZZER_PIN     2   // AtomS3のGroveポート(G2)。配線に合わせて変更可
#define UNIT_BUZZER_CHANNEL 4   // ledcのPWMチャンネル番号(他用途と重複しない番号を使用)

bool unitBuzzerOn = false;
unsigned long unitBuzzerOffAt = 0;

void setupUnitBuzzer() {
  if (!cfg.unitBuzzerEnable) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(UNIT_BUZZER_PIN, 4000, 10); // Arduino-ESP32 core 3.x以降のAPI
#else
  ledcSetup(UNIT_BUZZER_CHANNEL, 4000, 10);
  ledcAttachPin(UNIT_BUZZER_PIN, UNIT_BUZZER_CHANNEL);
  ledcWrite(UNIT_BUZZER_CHANNEL, 0);
#endif
  Serial.println(">> UNIT Buzzer init OK (pin=" + String(UNIT_BUZZER_PIN) + ")");
}

// 指定周波数[Hz]・時間[ms]でトーンを再生する（非ブロッキング。実際の停止はloop()側のupdateUnitBuzzer()で行う）
void unitBuzzerTone(int freq, int durationMs) {
  if (!cfg.unitBuzzerEnable) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(UNIT_BUZZER_PIN, freq);
#else
  ledcWriteTone(UNIT_BUZZER_CHANNEL, freq);
#endif
  unitBuzzerOn = true;
  unitBuzzerOffAt = millis() + durationMs;
}

// loop()から毎回呼び出し、指定時間が経過したら自動的に音を止める
void updateUnitBuzzer() {
  if (!unitBuzzerOn) return;
  if (millis() >= unitBuzzerOffAt) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(UNIT_BUZZER_PIN, 0);
#else
    ledcWrite(UNIT_BUZZER_CHANNEL, 0);
#endif
    unitBuzzerOn = false;
  }
}

// 内蔵ブザー(HAS_BUZZER)・UNIT Buzzer(cfg.unitBuzzerEnable)のどちらでも
// 同じように鳴らせる共通関数。StickC系の内蔵ブザーと同じ「警告音」機能を提供する。
void soundTone(int freq, int durationMs) {
#if HAS_BUZZER
  M5.Speaker.tone(freq, durationMs);
#endif
  if (cfg.unitBuzzerEnable) {
    unitBuzzerTone(freq, durationMs);
  }
}

// ====================================================================
//  ESPNOW
// ====================================================================
// スマートメーターからの受信値をシリアルログへまとめて表示する。
// 瞬時電力値(E7)・積算電力量(EA72)のどちらを受信した際にも、
// その時点で分かっている両方の値を一緒に出力する。
void logPowerStatus() {
  String line = "[POWER] instant=" + String(u.instant_power) + "W";
  if (u.total_power_valid) {
    line += " / total=" + String(u.total_power, 2) + "kWh @ " + u.total_power_datetime;
  } else {
    line += " / total=N/A(not received yet)";
  }
  Serial.println(line);
}

void espnowSend(const String &payload) {
  if (!cfg.espnowEnable) return;
  esp_now_send(broadcastMac, (const uint8_t *)payload.c_str(), payload.length());
}

void setupEspNow() {
  if (!cfg.espnowEnable) return;
  if (esp_now_init() != ESP_OK) {
    Serial.println(">> ESP-NOW init failed");
    return;
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  Serial.println(">> ESP-NOW init OK");
}

// ====================================================================
//  Ambient連携（https://ambidata.io/）
//  瞬間電力値用(Ambient_1)・積算電力量用(Ambient_2)を別チャンネルとして
//  扱う構成。積算電力量は30分毎に受信するたびに、間隔を空けず都度送信する。
// ====================================================================
void setupAmbient() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  if (cfg.ambientNowEnable) {
    ambientNowStatus = 1; // 待機中(黒)。有効化されていれば丸自体は常に表示する
    if (cfg.ambientNowChannelId.length() == 0 || cfg.ambientNowWriteKey.length() == 0) {
      Serial.println(">> Ambient(Now) enabled but channelId/writeKey is empty. skip.");
    } else if (!wifiOk) {
      Serial.println(">> Ambient(Now) enabled but WiFi not connected. skip.");
    } else {
      unsigned int channelId = (unsigned int)cfg.ambientNowChannelId.toInt();
      ambientNow.begin(channelId, cfg.ambientNowWriteKey.c_str(), &ambientNowClient);
      Serial.println(">> Ambient(Now) init OK (ch=" + cfg.ambientNowChannelId + ")");
    }
  }

  if (cfg.ambientTotalEnable) {
    ambientTotalStatus = 1; // 待機中(黒)
    if (cfg.ambientTotalChannelId.length() == 0 || cfg.ambientTotalWriteKey.length() == 0) {
      Serial.println(">> Ambient(Total) enabled but channelId/writeKey is empty. skip.");
    } else if (!wifiOk) {
      Serial.println(">> Ambient(Total) enabled but WiFi not connected. skip.");
    } else {
      unsigned int channelId = (unsigned int)cfg.ambientTotalChannelId.toInt();
      ambientTotal.begin(channelId, cfg.ambientTotalWriteKey.c_str(), &ambientTotalClient);
      Serial.println(">> Ambient(Total) init OK (ch=" + cfg.ambientTotalChannelId + ")");
    }
  }
}

// 瞬時電力値(d1)を一定間隔でAmbient_1へ送信する（E7受信時に呼ばれる）
void sendAmbientNowPower() {
  if (!cfg.ambientNowEnable) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - ambientNow_tc < (unsigned long)cfg.ambientNowIntervalSec * 1000UL) return;
  ambientNow_tc = now;

  ambientNow.set(1, (int)u.instant_power);
  if (!ambientNow.send()) {
    Serial.println(">> Ambient(Now) send failed");
    ambientNowStatus = 3; // 赤(失敗)
  } else {
    Serial.println(">> Ambient(Now) send OK");
    ambientNowStatus = 2; // 緑(成功)
  }
  redraw();
}

// 積算電力量(d1)＋計測時刻(created)をAmbient_2へ送信する（EA72受信時に毎回呼ばれる）
void sendAmbientTotalPower() {
  if (!cfg.ambientTotalEnable) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (!u.total_power_valid) return;

  ambientTotal.set(1, u.total_power);
  ambientTotal.set(11, u.total_power_datetime.c_str()); // 11 = "created"（計測時刻）
  if (!ambientTotal.send()) {
    Serial.println(">> Ambient(Total) send failed");
    ambientTotalStatus = 3; // 赤(失敗)
  } else {
    Serial.println(">> Ambient(Total) send OK");
    ambientTotalStatus = 2; // 緑(成功)
  }
  redraw();
}

// ====================================================================
//  BP35A1（Wi-SUN HAT）初期化シーケンス
//  ATコマンド(SKxxxx)をテキストで送信し、"OK"等の応答を待って進める。
// ====================================================================
#if defined(TARGET_CHIP_A1)

void uartFlushDust() {
  delay(500);
  while (WisunSerial.available()) WisunSerial.read();
  uartWriteLine("");
  delay(1000);
  while (WisunSerial.available()) WisunSerial.read();
  uartWriteLine("");
  delay(500);
  Serial.println(">> UART RX/TX Data Clear!");
}

void bp35InitEchoOn() {
  uartWriteLine("SKSREG SFE 1");
  if (!waitForOK(5000)) {
    fatalError("No response from Wi-SUN module (SKSREG SFE 1).\nCheck wiring/power (BP35A1).");
  }
  Serial.println(">> BP35A1 Echo back ON set OK");
}

void bp35InfoIgnored() {
  uartWriteLine("SKINFO");
  if (!waitForOK(5000)) {
    fatalError("No response from Wi-SUN module (SKINFO).\nCheck wiring/power (BP35A1).");
  }
  Serial.println(">> BP35A1 Info OK");
}

// ROPT確認 → バイナリモードならWOPT 01でASCIIモードへ切替
void bp35SetAsciiMode() {
  uartWriteLine("ROPT");
  unsigned long start = millis();
  bool responded = false;
  bool binaryMode = false;
  while (millis() - start < 5000) {
    String line = readLineBlocking(300);
    if (line.length() == 0) continue;
    if (line.startsWith("OK 00")) { binaryMode = true; responded = true; break; }
    if (line.startsWith("OK")) { binaryMode = false; responded = true; break; }
  }
  if (!responded) {
    fatalError("No response from Wi-SUN module (ROPT).\nCheck wiring/power (BP35A1).");
  }
  if (binaryMode) {
    uartWriteLine("WOPT 01");
    if (!waitForOK(3000)) {
      fatalError("No response from Wi-SUN module (WOPT 01).\nCheck wiring/power (BP35A1).");
    }
    Serial.println(">> BP35A1 ASCII mode set OK");
  } else {
    Serial.println(">> BP35A1 already ASCII mode");
  }
}

void bp35ClearOldSession() {
  uartWriteLine("SKTERM");
  unsigned long start = millis();
  bool responded = false;
  while (millis() - start < 5000) {
    String line = readLineBlocking(300);
    if (line.length() == 0) continue;
    // "FAIL ER10"はセッションが無かった場合の正常な応答なので、これも成功扱い。
    // 応答そのものが一切来ない場合のみ、モジュール未接続とみなしfatalErrorとする。
    if (line.startsWith("OK") || line.startsWith("FAIL ER10")) { responded = true; break; }
  }
  if (!responded) {
    fatalError("No response from Wi-SUN module (SKTERM).\nCheck wiring/power (BP35A1).");
  }
}

bool bp35SetPassword() {
  uartWriteLine("SKSETPWD C " + cfg.brPswd);
  return waitForOK(5000);
}

bool bp35SetRouteId() {
  uartWriteLine("SKSETRBID " + cfg.brId);
  return waitForOK(5000);
}

// アクティブスキャンでチャンネル/PANID/MACアドレス/LQIを取得
bool bp35ActiveScan() {
  for (int s_c = 1; s_c <= SCAN_COUNT; s_c++) {
    uartWriteLine("SKSCAN 2 FFFFFFFF 6");
    if (!waitForOK(3000)) continue;
    Serial.println(">> Activescan count:" + String(s_c) + " start!");

    bool scanEnd = false;
    unsigned long scanStart = millis();
    while (!scanEnd && (millis() - scanStart < 8000)) {
      String line = readLineBlocking(300);
      if (line.length() == 0) continue;
      if (line.startsWith("EVENT 22")) {
        scanEnd = true;
      } else if (line.startsWith("Channel:")) {
        scanCache.channel = line.substring(String("Channel:").length());
        scanCache.channel.trim();
      } else if (line.startsWith("Pan ID:")) {
        scanCache.panId = line.substring(String("Pan ID:").length());
        scanCache.panId.trim();
      } else if (line.startsWith("Addr:")) {
        scanCache.macAddr = line.substring(String("Addr:").length());
        scanCache.macAddr.trim();
      } else if (line.startsWith("LQI:")) {
        scanCache.lqi = line.substring(String("LQI:").length());
        scanCache.lqi.trim();
      }
    }
    progressDot();

    if (scanCache.channel.length() == 2 && scanCache.panId.length() == 4 &&
        scanCache.macAddr.length() == 16 && scanCache.lqi.length() == 2) {
      scanCache.valid = true;
      portal.saveScan(scanCache);
      Serial.println(">> Scan All Clear!");
      return true;
    }
  }
  return false; // SCAN_COUNT回試しても見つからなかった
}

// チャンネル・PANID設定 → MACアドレス→IPv6変換 → エコーバックOFF → PANA接続
bool bp35PanaConnect() {
  int noResponseRetries = 0;
  const int MAX_NO_RESPONSE_RETRIES = 5; // 応答が一切無い状態がこの回数続いたら異常とみなす

  while (true) {
    uartWriteLine("SKSREG S2 " + scanCache.channel);
    if (!waitForOK(3000)) {
      // ここまで到達している時点でモジュール自体はwisunBegin()/wisunSetCredentials()で
      // 応答済みのため、原因は「キャッシュしたチャンネル値が無効/古い」可能性が高い。
      // fatalError()で止めてしまうと再起動しても同じキャッシュを読み直して
      // 毎回同じ場所で失敗し続けるため、キャッシュを破棄して再起動＝再スキャンさせる。
      Serial.println(">> SKSREG S2 (channel) failed. Clearing cached scan result and rebooting to rescan...");
      portal.eraseScan();
      delay(1000);
      ESP.restart();
    }

    uartWriteLine("SKSREG S3 " + scanCache.panId);
    if (!waitForOK(3000)) {
      Serial.println(">> SKSREG S3 (PAN ID) failed. Clearing cached scan result and rebooting to rescan...");
      portal.eraseScan();
      delay(1000);
      ESP.restart();
    }

    uartWriteLine("SKLL64 " + scanCache.macAddr);
    unsigned long start = millis();
    ipv6Addr = "";
    while (millis() - start < 3000) {
      String line = readLineBlocking(300);
      if (line.length() == 39) { ipv6Addr = line; break; }
    }
    if (ipv6Addr.length() != 39) {
      noResponseRetries++;
      Serial.println(">> SKLL64 failed, retry (" + String(noResponseRetries) + "/" + String(MAX_NO_RESPONSE_RETRIES) + ")");
      if (noResponseRetries >= MAX_NO_RESPONSE_RETRIES) {
        // MACアドレスのキャッシュが古い/無効な可能性が高いため、破棄して再起動する
        Serial.println(">> SKLL64 keeps failing. Clearing cached scan result and rebooting to rescan...");
        portal.eraseScan();
        delay(1000);
        ESP.restart();
      }
      continue;
    }

    uartWriteLine("SKSREG SFE 0");
    if (!waitForOK(3000)) {
      // SKSREG SFE 0はスキャンキャッシュに依存しないコマンドのため、
      // これが無応答の場合は純粋な通信異常とみなしてfatalError()で停止する。
      fatalError("No response from Wi-SUN module (SKSREG SFE 0).\nCheck wiring/power (BP35A1).");
    }
    Serial.println(">> BP35A1 Echo back OFF set OK");

    Serial.println(">> PANA authentication start!!");
    uartWriteLine("SKJOIN " + ipv6Addr);

    bool connected = false, failed = false;
    unsigned long joinStart = millis();
    while (!connected && !failed && (millis() - joinStart < 30000)) {
      String line = readLineBlocking(300);
      if (line.length() == 0) continue;
      if (line.startsWith("EVENT 24")) {
        failed = true;
      } else if (line.startsWith("EVENT 25")) {
        connected = true;
      }
    }

    if (failed) {
      Serial.println(">> PANA authentication NG! ...scan retry");
      portal.eraseScan(); // チャンネルが変わった可能性があるので破棄して再起動
      delay(1000);
      ESP.restart();
    }
    if (connected) {
      Serial.println(">> PANA authentication OK!");
      return true;
    }

    // タイムアウト(EVENT24/25とも来なかった)時は、無応答カウントとして再試行
    noResponseRetries++;
    Serial.println(">> SKJOIN response timeout, retry (" + String(noResponseRetries) + "/" + String(MAX_NO_RESPONSE_RETRIES) + ")");
    if (noResponseRetries >= MAX_NO_RESPONSE_RETRIES) {
      Serial.println(">> SKJOIN keeps timing out. Clearing cached scan result and rebooting to rescan...");
      portal.eraseScan();
      delay(1000);
      ESP.restart();
    }
  }
}

void bp35GetCoefficientAndUnit() {
  // 積算電力量係数(D3)
  if (scanCache.coefficient == 0) {
    // 長さは4桁HEXゼロ埋めで送る必要があるため、専用に組み立てる
    char lenBuf[5];
    snprintf(lenBuf, sizeof(lenBuf), "%04X", (unsigned)sizeof(GET_COEFFICIENT));
    WisunSerial.print("SKSENDTO 1 " + ipv6Addr + " 0E1A 1 " + String(lenBuf) + " ");
    WisunSerial.write(GET_COEFFICIENT, sizeof(GET_COEFFICIENT));
    Serial.println(">> [GET_COEFFICIENT] cmd send");

    unsigned long start = millis();
    while (scanCache.coefficient == 0 && (millis() - start < RES_TOUT * 1000UL)) {
      String line = readLineBlocking(300);
      if (line.length() == 0) continue;
      if (u.read(line) == "D3") {
        scanCache.coefficient = u.power_coefficient;
        Serial.println(" - COEFFICIENT: " + String(scanCache.coefficient));
      }
    }
    if (scanCache.coefficient == 0) {
      Serial.println(">> response timeout! set[COEFFICIENT = 1]");
      scanCache.coefficient = 1;
    }
    portal.saveScanCoefficient(scanCache.coefficient);
  }
  u.power_coefficient = scanCache.coefficient;
  progressDot();

  // 積算電力量単位(E1)
  if (scanCache.unit == 0.0f) {
    char lenBuf[5];
    snprintf(lenBuf, sizeof(lenBuf), "%04X", (unsigned)sizeof(GET_TOTAL_POWER_UNIT));
    WisunSerial.print("SKSENDTO 1 " + ipv6Addr + " 0E1A 1 " + String(lenBuf) + " ");
    WisunSerial.write(GET_TOTAL_POWER_UNIT, sizeof(GET_TOTAL_POWER_UNIT));
    Serial.println(">> [GET_TOTAL_POWER_UNIT] cmd send");

    unsigned long start = millis();
    while (scanCache.unit == 0.0f && (millis() - start < RES_TOUT * 1000UL)) {
      String line = readLineBlocking(300);
      if (line.length() == 0) continue;
      if (u.read(line) == "E1") {
        scanCache.unit = u.power_unit;
        Serial.println(" - UNIT: " + String(scanCache.unit, 4));
      }
    }
    if (scanCache.unit == 0.0f) {
      Serial.println(">> response timeout! set[UNIT = 0.1]");
      scanCache.unit = 0.1f;
    }
    portal.saveScanUnit(scanCache.unit);
  }
  u.power_unit = scanCache.unit;
  progressDot();
}

#endif // TARGET_CHIP_A1


// ====================================================================
//  BP35C1-J11-T01（Wi-SUN HAT-C1）初期化シーケンス
//  ユニークコード＋チェックサム付きの独自バイナリフレームでコマンドを
//  やり取りする（BP35A1のテキストATコマンドとは通信方式が異なる）。
// ====================================================================
#if defined(TARGET_CHIP_C1)

void c1HardwareReset() {
#if defined(WISUN_RESET_PIN)
  pinMode(WISUN_RESET_PIN, OUTPUT);
  digitalWrite(WISUN_RESET_PIN, LOW);
  delay(100);
  digitalWrite(WISUN_RESET_PIN, HIGH);
  Serial.println(">> BP35C1 RESET PIN release");
#endif
}

// 指定コマンドコードの応答フレームが来るまでポーリングして待つ
bool c1WaitCmd(uint16_t cmd, unsigned long timeoutMs, std::vector<uint8_t> *outData = nullptr) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (wisunC1.poll() && wisunC1.retcmd() == (int)cmd) {
      if (outData) *outData = wisunC1.retdata();
      return true;
    }
    delay(1);
  }
  return false;
}

void uartFlushDust() {
  wisunC1.clearBuf();
  Serial.println(">> UART RX/TX Data Clear");
}

bool bp35c1Begin() {
  c1HardwareReset();
  delay(500);
  wisunC1.clearBuf();

  wisunC1.sendCmd(0x00d9, {}); // F1 ハードウェアリセット要求
  if (!c1WaitCmd(0x6019, 5000)) {
    Serial.println(">> BP35C1 boot notify timeout");
    return false;
  }
  Serial.println(">> BP35C1 Boot OK");
  delay(300);

  // 初期設定（チャンネルは仮の0x04。実際のチャンネルはスキャン後に再設定）
  wisunC1.sendCmd(0x005f, {0x05, 0x00, 0x04, 0x00});
  std::vector<uint8_t> data;
  if (!c1WaitCmd(0x205f, 5000, &data) || data.empty() || data[0] != 0x01) {
    Serial.println(">> BP35C1 initial 0x005f failed");
    return false;
  }
  Serial.println(">> BP35C1 Initialize OK");
  return true;
}

bool bp35c1SetCredentials() {
  std::vector<uint8_t> cred;
  for (size_t i = 0; i < cfg.brId.length(); i++) cred.push_back((uint8_t)cfg.brId[i]);
  for (size_t i = 0; i < cfg.brPswd.length(); i++) cred.push_back((uint8_t)cfg.brPswd[i]);

  for (int retry = 0; retry < 5; retry++) {
    wisunC1.sendCmd(0x0054, cred);
    std::vector<uint8_t> data;
    if (c1WaitCmd(0x2054, 5000, &data) && !data.empty() && data[0] == 0x01) {
      Serial.println(">> BP35C1 B-root ID/PWD set OK");
      return true;
    }
    Serial.println(">> BP35C1 B-root ID/PWD set NG, retry");
    delay(3000);
  }
  return false;
}

// アクティブスキャンでチャンネル/PANID/MACアドレス/RSSIを取得
bool bp35c1ActiveScan() {
  std::vector<uint8_t> pairingId;
  String last8 = (cfg.brId.length() >= 8) ? cfg.brId.substring(cfg.brId.length() - 8) : cfg.brId;
  for (size_t i = 0; i < last8.length(); i++) pairingId.push_back((uint8_t)last8[i]);

  for (int s_c = 1; s_c <= SCAN_COUNT; s_c++) {
    std::vector<uint8_t> req = {0x06, 0x00, 0x03, 0xFF, 0xF0, 0x01};
    req.insert(req.end(), pairingId.begin(), pairingId.end());
    wisunC1.sendCmd(0x0051, req);
    Serial.println(">> Activescan count:" + String(s_c) + " start!");

    bool found = false;
    unsigned long scanStart = millis();
    while (millis() - scanStart < 12000) {
      if (!wisunC1.poll()) { delay(1); continue; }
      int cmd = wisunC1.retcmd();
      if (cmd == 0x4051) {
        std::vector<uint8_t> data = wisunC1.retdata();
        if (data.size() >= 14 && data[0] == 0x00) { // スキャン応答あり(スマートメーター発見)
          uint8_t channel = data[1];
          std::vector<uint8_t> macAddr(data.begin() + 3, data.begin() + 11);
          std::vector<uint8_t> panId(data.begin() + 11, data.begin() + 13);
          uint8_t rssi = data[13];

          char chBuf[3];
          snprintf(chBuf, sizeof(chBuf), "%02X", channel);
          scanCache.channel = String(chBuf);
          scanCache.panId   = c1BytesToHex(panId);
          scanCache.macAddr = c1BytesToHex(macAddr);
          char rssiBuf[3];
          snprintf(rssiBuf, sizeof(rssiBuf), "%02X", rssi);
          scanCache.lqi = String(rssiBuf); // C1版ではRSSIをlqiフィールドに流用して保存

          Serial.println(">> smartmeter Discovered! ch=" + scanCache.channel);
          found = true;
        }
      } else if (cmd == 0x2051) {
        break; // このスキャン1周が完了
      }
    }
    progressDot();
    if (found) {
      scanCache.valid = true;
      portal.saveScan(scanCache);
      Serial.println(">> Scan All Clear!");
      return true;
    }
  }
  return false;
}

bool bp35c1Join() {
  std::vector<uint8_t> macAddr = c1HexToBytes(scanCache.macAddr);
  std::vector<uint8_t> panId   = c1HexToBytes(scanCache.panId);
  uint8_t channel = (uint8_t)strtol(scanCache.channel.c_str(), nullptr, 16);
  if (macAddr.size() != 8 || panId.size() != 2) {
    Serial.println(">> BP35C1 invalid scan cache. Clearing and rebooting to rescan...");
    portal.eraseScan();
    delay(1000);
    ESP.restart();
  }

  // 発見したチャンネルを反映して再初期化
  bool initOk = false;
  for (int retry = 0; retry < 5; retry++) {
    wisunC1.sendCmd(0x005f, {0x05, 0x00, channel, 0x00});
    std::vector<uint8_t> data;
    if (c1WaitCmd(0x205f, 5000, &data) && !data.empty() && data[0] == 0x01) { initOk = true; break; }
    delay(1000);
  }
  if (!initOk) {
    // キャッシュしたチャンネルが古くなっている（スマートメーター側のチャンネルが
    // 変わった等）可能性が高いため、キャッシュを破棄して再起動→再スキャンさせる。
    // ここで単にfatalError()すると、再起動しても同じ古いキャッシュを読み直して
    // 同じ場所で毎回失敗し続けてしまうため、それを避ける。
    Serial.println(">> BP35C1 channel init failed. Clearing cached scan result and rebooting to rescan...");
    portal.eraseScan();
    delay(1000);
    ESP.restart();
  }
  Serial.println(">> BP35C1 Initialize OK with channel");

  // Bルート動作開始要求
  wisunC1.sendCmd(0x0053, {});
  {
    unsigned long start = millis();
    bool ok = false;
    while (millis() - start < 15000) {
      if (!wisunC1.poll()) { delay(1); continue; }
      if (wisunC1.retcmd() == 0x2053) {
        std::vector<uint8_t> data = wisunC1.retdata();
        if (!data.empty() && data[0] == 0x01) { ok = true; break; }
        if (!data.empty() && data[0] == 0x0e) { wisunC1.sendCmd(0x0053, {}); continue; } // 応答無し再送
      }
    }
    if (!ok) { Serial.println(">> BP35C1 B-root start failed"); return false; }
  }
  Serial.println(">> B-root connected");

  // UDPポートOPEN要求 (0x0e1a = ECHONET Lite)
  wisunC1.sendCmd(0x0005, {0x0e, 0x1a});
  std::vector<uint8_t> data;
  if (!c1WaitCmd(0x2005, 5000, &data) || data.empty() || data[0] != 0x01) {
    Serial.println(">> BP35C1 UDP port open failed");
    return false;
  }
  Serial.println(">> UDP port 0x0e1a opened");

  // BルートPANA開始要求 → 認証結果通知待ち
  wisunC1.sendCmd(0x0056, {});
  {
    unsigned long start = millis();
    bool connected = false, failed = false;
    while (!connected && !failed && millis() - start < 30000) {
      if (!wisunC1.poll()) { delay(1); continue; }
      int cmd = wisunC1.retcmd();
      if (cmd == 0x6028) { // PANA認証結果通知
        std::vector<uint8_t> d = wisunC1.retdata();
        if (!d.empty() && d[0] == 0x01) connected = true; else failed = true;
      } else if (cmd == 0x2056) {
        std::vector<uint8_t> d = wisunC1.retdata();
        if (d.empty() || d[0] != 0x01) wisunC1.sendCmd(0x0056, {}); // 失敗したら再送
      }
    }
    if (failed) {
      Serial.println(">> PANA authentication NG! ...scan retry");
      portal.eraseScan(); // チャンネルが変わった可能性があるので破棄して再起動
      delay(1000);
      ESP.restart();
    }
    if (!connected) { Serial.println(">> PANA authentication timeout"); return false; }
  }
  Serial.println(">> PANA authentication OK!");

  // 以降のデータ送信で使う宛先アドレス情報を構築
  c1_iph = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  c1_ipl = macAddr;
  c1_ipl[0] ^= 0x02;
  c1_port = {0x0e, 0x1a, 0x0e, 0x1a};
  return true;
}

// ECHONET Liteフレームをスマートメーターへ送信
void c1SendMeterCommand(const uint8_t *frame, size_t len) {
  std::vector<uint8_t> payload = c1DataWithLength(c1ToVector(frame, len));
  std::vector<uint8_t> req = c1_iph;
  req.insert(req.end(), c1_ipl.begin(), c1_ipl.end());
  req.insert(req.end(), c1_port.begin(), c1_port.end());
  req.insert(req.end(), payload.begin(), payload.end());
  wisunC1.sendCmd(0x0008, req);
}

// 受信データを、既存のWisunUdp::read()にそのまま渡せる疑似ERXUDP行に変換する。
// こうすることで、BP35A1(テキスト)とBP35C1(バイナリ)のどちらでも、
// 受信データの解析ロジック(WisunUdp)を共通化できる。
bool c1PollMeterLine(String &outLine) {
  if (!wisunC1.poll()) return false;
  if (wisunC1.retcmd() != 0x6018) return false; // データ受信通知以外は無視
  std::vector<uint8_t> data = wisunC1.retdata();
  if (data.size() <= 27) return false;
  String hex;
  char buf[3];
  for (size_t i = 27; i < data.size(); i++) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    hex += buf;
  }
  outLine = "ERXUDP 1 2 3 4 5 6 7 " + hex;
  return true;
}

void bp35c1GetCoefficientAndUnit() {
  if (scanCache.coefficient == 0) {
    c1SendMeterCommand(GET_COEFFICIENT, sizeof(GET_COEFFICIENT));
    Serial.println(">> [GET_COEFFICIENT] cmd send");
    unsigned long start = millis();
    while (scanCache.coefficient == 0 && millis() - start < RES_TOUT * 1000UL) {
      String line;
      if (c1PollMeterLine(line) && u.read(line) == "D3") {
        scanCache.coefficient = u.power_coefficient;
        Serial.println(" - COEFFICIENT: " + String(scanCache.coefficient));
      }
      delay(1);
    }
    if (scanCache.coefficient == 0) {
      Serial.println(">> response timeout! set[COEFFICIENT = 1]");
      scanCache.coefficient = 1;
    }
    portal.saveScanCoefficient(scanCache.coefficient);
  }
  u.power_coefficient = scanCache.coefficient;
  progressDot();

  if (scanCache.unit == 0.0f) {
    c1SendMeterCommand(GET_TOTAL_POWER_UNIT, sizeof(GET_TOTAL_POWER_UNIT));
    Serial.println(">> [GET_TOTAL_POWER_UNIT] cmd send");
    unsigned long start = millis();
    while (scanCache.unit == 0.0f && millis() - start < RES_TOUT * 1000UL) {
      String line;
      if (c1PollMeterLine(line) && u.read(line) == "E1") {
        scanCache.unit = u.power_unit;
        Serial.println(" - UNIT: " + String(scanCache.unit, 4));
      }
      delay(1);
    }
    if (scanCache.unit == 0.0f) {
      Serial.println(">> response timeout! set[UNIT = 0.1]");
      scanCache.unit = 0.1f;
    }
    portal.saveScanUnit(scanCache.unit);
  }
  u.power_unit = scanCache.unit;
  progressDot();
}

#endif // TARGET_CHIP_C1


// ====================================================================
//  チップ非依存の共通ラッパー（setup()/loop()はこちらだけを呼ぶ）
// ====================================================================
bool wisunBegin() {
#if defined(TARGET_CHIP_A1)
  uartFlushDust();
  bp35InitEchoOn();
  bp35InfoIgnored();
  bp35SetAsciiMode();
  bp35ClearOldSession();
  return true;
#else
  return bp35c1Begin();
#endif
}

bool wisunSetCredentials() {
#if defined(TARGET_CHIP_A1)
  if (!bp35SetPassword()) return false;
  if (!bp35SetRouteId()) return false;
  return true;
#else
  return bp35c1SetCredentials();
#endif
}

bool wisunScan() {
#if defined(TARGET_CHIP_A1)
  return bp35ActiveScan();
#else
  return bp35c1ActiveScan();
#endif
}

bool wisunJoin() {
#if defined(TARGET_CHIP_A1)
  return bp35PanaConnect();
#else
  return bp35c1Join();
#endif
}

void wisunGetCoefficientAndUnit() {
#if defined(TARGET_CHIP_A1)
  bp35GetCoefficientAndUnit();
#else
  bp35c1GetCoefficientAndUnit();
#endif
}

// ECHONET Liteフレームをスマートメーターへ送信（チップ非依存）
void wisunSendMeterCommand(const uint8_t *frame, size_t len) {
#if defined(TARGET_CHIP_A1)
  char lenBuf[5];
  snprintf(lenBuf, sizeof(lenBuf), "%04X", (unsigned)len);
  WisunSerial.print("SKSENDTO 1 " + ipv6Addr + " 0E1A 1 " + String(lenBuf) + " ");
  WisunSerial.write(frame, len);
#else
  c1SendMeterCommand(frame, len);
#endif
}

// スマートメーターからの受信データを1件チェックする（チップ非依存）。
// 受信が完了していれば true を返し、type にECHONET種別("E7"/"EA72"等)を入れる。
bool wisunPollMeterLine(String &type) {
#if defined(TARGET_CHIP_A1)
  while (WisunSerial.available()) {
    char c = WisunSerial.read();
    if (c == '\n') {
      String line = rxLineBuf;
      rxLineBuf = "";
      line.trim();
      if (line.length() == 0) continue;
      type = u.read(line);
      return true;
    } else if (c != '\r') {
      rxLineBuf += c;
      if (rxLineBuf.length() > 200) rxLineBuf = ""; // 異常に長い場合は破棄
    }
  }
  return false;
#else
  String line;
  if (c1PollMeterLine(line)) {
    type = u.read(line);
    return true;
  }
  return false;
#endif
}

// ====================================================================
//  ボタン / ブザー処理
// ====================================================================
void handleButtons() {
  if (M5.BtnA.wasClicked()) {
    // BEEP(ブザー警告)のON/OFF切替。バックライト輝度はWeb設定の固定値を使うため、
    // ボタンAはシングルクリックのみでBEEP切替を行う。
    if (buzzerAvailable()) {
      beepEnabled = !beepEnabled;
      redraw();
    }
  }
  if (M5.BtnB.wasClicked()) {
    // ボタンBで「重力方向による画面自動回転」のON/OFFをトグル
    //   OFFにした場合はその時点の向きに固定される
    autoRotateEnabled = !autoRotateEnabled;
    rotationCandidate = currentRotation;
    Serial.println(autoRotateEnabled ? ">> Auto-rotate ON" : ">> Auto-rotate OFF (fixed)");
    redraw();
  }
}

void handleBeep() {
  if (!buzzerAvailable()) return;
  static unsigned long lastBeep = 0;
  if (dataMute || !u.instant_power_valid) return;
  bool warn = (u.instant_power >= (long)(cfg.ampereLimit * cfg.ampereRed * 100));
  if (warn && beepEnabled) {
    if (millis() - lastBeep >= 2100) {
      soundTone(220, 200);
      lastBeep = millis();
    }
  }
}

// ====================================================================
//  setup / loop
// ====================================================================

// 設定ポータル動作中の案内画面。
//   接続先SSID・アクセス先URL・スマホ等の接続有無を継続的に表示する。
static const char *SETUP_AP_SSID = "WiSUN-HAT-Setup";

void drawSetupScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(4, 4);
  M5.Display.println("== Setup Mode ==");

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  M5.Display.println("WiFiに接続してください:");
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.println(SETUP_AP_SSID);

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  M5.Display.println("ブラウザで開く:");
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.print("http://");
  M5.Display.println(WiFi.softAPIP());

  int stationCount = WiFi.softAPgetStationNum();
  M5.Display.println("");
  M5.Display.setTextColor(stationCount > 0 ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  M5.Display.print(stationCount > 0 ? "* Connected" : "* Waiting for connection...");
}

void enterSetupPortalIfNeeded() {
  bool forceSetup = M5.BtnA.isPressed(); // 起動時ボタンA長押しで強制的に設定ポータルへ
  bool configured = portal.load(cfg);

  if (!configured || forceSetup) {
    M5.Display.setRotation(0); // 設定画面は常に標準向きで表示
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println("Starting setup portal...");

    // idleTick はキャプチャ無しラムダ(=関数ポインタ変換可)。
    // static変数とグローバルオブジェクトのみを参照して定期的に画面更新する。
    portal.runPortal(cfg, []() {
      M5.update();
      static unsigned long lastDraw = 0;
      if (millis() - lastDraw >= 500) {
        lastDraw = millis();
        drawSetupScreen();
      }
    });

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.println("Saved!");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println("Rebooting...");
    delay(1500);
    ESP.restart();
  }
}

void setup() {
  auto cfgM5 = M5.config();
  M5.begin(cfgM5);
  Serial.begin(115200);

  M5.Display.setRotation(0);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  // IMU(加速度センサー)の有無確認。M5.begin()の時点で自動初期化されている。
  imuAvailable = M5.Imu.isEnabled();
  Serial.println(imuAvailable ? ">> IMU detected (auto-rotate enabled)"
                               : ">> IMU not detected (auto-rotate disabled)");
#if HAS_BUZZER
  Serial.println(">> Buzzer: internal buzzer available (TARGET_BOARD_STICKC)");
#else
  Serial.println(">> Buzzer: no internal buzzer (TARGET_BOARD_ATOMS3). UNIT Buzzer setting will be checked after config load.");
#endif
  currentRotation = 0;
  rotationCandidate = 0;
  M5.Display.setRotation(currentRotation);

  // ---- 設定ポータル（未設定 or 起動時ボタン長押しで起動） ----
  enterSetupPortalIfNeeded();
  progressDot();

  // UNIT Buzzer（外付けブザー）初期化。設定読込後でないと有効/無効が分からないためここで実施
  setupUnitBuzzer();
  Serial.println(buzzerAvailable() ? ">> Buzzer feature: available" : ">> Buzzer feature: not available");

  // ---- WiFi接続（ESPNOW/将来のNTP等に使用。失敗しても続行） ----
  if (cfg.wifiSsid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
      delay(200);
      progressDot();
    }
    Serial.println(WiFi.status() == WL_CONNECTED ? ">> WiFi connected" : ">> WiFi connect timeout (continue)");
  } else {
    WiFi.mode(WIFI_STA); // ESPNOWのためSTAモードだけ有効化
  }
  progressDot();

  // ---- ESPNOW初期化 ----
  setupEspNow();
  progressDot();

  // ---- Wi-SUNモジュール UART初期化 ----
  WisunSerial.begin(115200, SERIAL_8N1, WISUN_UART_RX_PIN, WISUN_UART_TX_PIN);
  progressDot();

  if (!wisunBegin()) fatalError("Wi-SUN module init failed");
  progressDot();

  if (!wisunSetCredentials()) fatalError("B-root ID/PWD set failed");
  progressDot();
  delay(1000);

  // ---- チャンネルスキャン（キャッシュが無ければ実施） ----
  if (!portal.loadScan(scanCache)) {
    if (!wisunScan()) {
      fatalError("Scan retry count over! Please Reboot!");
    }
  } else {
    Serial.println(">> Using cached Wi-SUN scan result");
  }
  progressDot();

  // ---- PANA接続 ----
  if (!wisunJoin()) fatalError("PANA join failed");
  progressDot();

  // ---- 積算電力量係数・単位の取得 ----
  wisunGetCoefficientAndUnit();

  // ---- Ambient初期化（有効時のみ） ----
  setupAmbient();

  // ---- 画面初期化 ----
  M5.Display.setBrightness(cfg.backlightBrightness);
  redraw();

  np_tc = tp_tc = cmd_tc = ambientNow_tc = millis();
  Serial.println(">> Start mainloop!");
}

void loop() {
  M5.update();
  handleButtons();
  updateAutoRotation(); // 加速度センサーによる画面自動回転
  handleBeep();
  updateUnitBuzzer(); // UNIT Buzzer(外付け)の自動消音タイマー処理

  // ---- スマートメーターへのコマンド送信処理 ----
  if (!cmd_w) {
    unsigned long now = millis();
    if ((now - tp_tc >= 30UL * 60UL * 1000UL) ||
        (!tp_f && (now - tp_tc >= 10000UL))) {
      wisunSendMeterCommand(GET_TOTAL_POWER_30, sizeof(GET_TOTAL_POWER_30));
      Serial.println(">> [GET_TOTAL_POWER_30] cmd send");
      tp_tc = now;
      tp_f = false;
      cmd_tc = now;
      cmd_w = true;
      cmd_rc++;
    } else if (now - np_tc >= (unsigned long)cfg.npIntervalSec * 1000UL) {
      wisunSendMeterCommand(GET_NOW_P, sizeof(GET_NOW_P));
      Serial.println(">> [GET_NOW_P] cmd send");
      np_tc = now;
      cmd_tc = now;
      cmd_w = true;
      cmd_rc++;
    }
  }

  // ---- スマートメーターからの受信処理 ----
  if (cmd_w) {
    String type;
    while (wisunPollMeterLine(type)) {
      if (type == "E7") {
        cmd_w = false;
        cmd_rc = 0;
        dataMute = false;
        redraw();
        espnowSend("NPD=" + String(u.instant_power));
        sendAmbientNowPower(); // Ambient(瞬間電力)へ送信（内部で送信間隔を判定）
        logPowerStatus();
      } else if (type == "EA72") {
        cmd_w = false;
        cmd_rc = 0;
        tp_f = true;
        espnowSend("TPD=" + String(u.total_power, 2) + "/" + u.total_power_datetime);
        sendAmbientTotalPower(); // Ambient(積算電力量)へ都度送信（30分毎の受信ごと）
        logPowerStatus();
      }
    }

    if (cmd_rc <= 10) {
      if (millis() - cmd_tc >= (unsigned long)RES_TOUT * 1000UL) {
        Serial.println(">> cmd response timeout");
        cmd_w = false;
        cmd_tc = millis();
      }
    } else {
      Serial.println(">> cmd send retry count over! Reboot!!");
      ESP.restart();
    }
  }

  // ---- 長時間無応答時の処理 ----
  if (u.instant_power_valid) {
    unsigned long since = millis() - u.instant_power_millis;
    if (since >= (unsigned long)cfg.timeoutSec * 1000UL) {
      if (!dataMute) { dataMute = true; redraw(); }
      if (since >= (unsigned long)cfg.timeoutSec * 4UL * 1000UL) {
        Serial.println(">> Communication failure?? Reboot!!");
        ESP.restart();
      }
    }
  }

  delay(2);
}
