// =====================================================================
//  wisun_Arduino_view_StickC.ino
//
//  rev0.0.3 2026.7.22 @rin_ofumi
//
//  wisun_Arduino（親機）がESPNOWでブロードキャストする瞬時電力値を
//  受信し、画面に表示するモニター（子機）用サンプルプログラムです。
//  M5StickC / M5StickC Plus / M5StickC Plus2 / M5StickS3 / M5AtomS3 /
//  AtomS3R を対象としています。
//
//  ボード選択・BEEP(ブザー)の扱い・画面自動回転の考え方は、親機
//  (wisun_Arduino) と同じ設計に揃えています。
//
//  必要ライブラリ (Arduino IDE / PlatformIO):
//   - M5Unified            (M5Stack公式)
//   - WiFi / WebServer / DNSServer / Preferences / esp_now
//     （いずれも ESP32 Arduino core 同梱）
//
//  同じフォルダに ViewConfigStickC.h が必要です。
// =====================================================================

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>

#include "ViewConfigStickC.h"

// ------------------------------------------------------------------
// 対象ボードの選択
//  使用する組み合わせの行だけコメントを外してください（1つだけ有効にすること）。
//  BEEP(ブザー)機能の有無・UNIT Buzzerの既定ピン・画面回転の基準値が
//  この選択に連動します（親機 wisun_Arduino と同じ考え方です）。
// ------------------------------------------------------------------
// #define TARGET_BOARD_STICKC      // M5StickC（無印、ブザー無し）
// #define TARGET_BOARD_STICKCPLUS  // M5StickC Plus / Plus2（スピーカー有り）
// #define TARGET_BOARD_STICKS3     // M5StickS3（スピーカー有り）
#define TARGET_BOARD_ATOMS3      // M5AtomS3 / AtomS3R（ブザー無し）

// ---- ブザー(BEEP)機能の有無（ボード依存） ----
// M5StickC（無印）とM5AtomS3/AtomS3Rは内蔵ブザー/スピーカーが無いため0。
// M5StickC Plus/Plus2とM5StickS3はスピーカーを搭載しているため1。
#if defined(TARGET_BOARD_STICKC) || defined(TARGET_BOARD_ATOMS3)
  #define HAS_BUZZER 0
#elif defined(TARGET_BOARD_STICKCPLUS) || defined(TARGET_BOARD_STICKS3)
  #define HAS_BUZZER 1
#else
  #error "TARGET_BOARD_* を1つ定義してください"
#endif

// ---- UNIT Buzzer（外付け）の既定接続ピン（ボード依存） ----
#if defined(TARGET_BOARD_ATOMS3)
  #define UNIT_BUZZER_PIN 2   // M5AtomS3 / AtomS3R のGroveポート(G2)
#elif defined(TARGET_BOARD_STICKC) || defined(TARGET_BOARD_STICKCPLUS)
  #define UNIT_BUZZER_PIN 32  // M5StickC / Plus / Plus2 のPort A(G32)
#elif defined(TARGET_BOARD_STICKS3)
  #define UNIT_BUZZER_PIN 9   // M5StickS3 のPort A(G9)
#endif
#define UNIT_BUZZER_CHANNEL 4 // ledcのPWMチャンネル番号

// ---- 画面回転の基準値（ボード依存） ----
// M5StickS3は公式サンプルでも setRotation(1) が正位置とされているため、
// M5StickC系を基準(0)としたときのズレ分として+1を設定している。
#if defined(TARGET_BOARD_STICKS3)
  #define BOARD_ROTATION_OFFSET 1
#else
  #define BOARD_ROTATION_OFFSET 0
#endif

// ---- 内蔵ブザー/UNIT Buzzer共通の警告音パラメータ ----
// 低い周波数だと機種によって音が小さく聞き取りにくいことがあるため、
// UNIT Buzzerと同じ周波数を内蔵スピーカーの再生にも使用している。
#define BUZZER_TONE_FREQ_HZ     4000
#define BUZZER_TONE_DURATION_MS 200

// ====================================================================
//  グローバル状態
// ====================================================================
ViewConfig       cfg;
ViewConfigPortal portal;

long          instantPower      = 0;
bool          instantPowerValid = false;
unsigned long instantPowerMillis = 0;
bool          dataMute = false;

bool beepEnabled = true;

bool imuAvailable      = false;
bool autoRotateEnabled = true;
int  currentRotation   = 0;
int  rotationCandidate = 0;
unsigned long rotationCandidateSince = 0;

bool unitBuzzerOn = false;
bool espNowInitialized = false; // esp_now_init()が成功したかどうか
bool wifiStarted = false; // WiFi.mode(WIFI_STA)等を一度でも呼んだことがあるか
unsigned long unitBuzzerOffAt = 0;

// ====================================================================
//  UNIT Buzzer（外付けブザー）駆動
// ====================================================================
void setupUnitBuzzer() {
  if (!cfg.unitBuzzerEnable) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(UNIT_BUZZER_PIN, 4000, 10); // Arduino-ESP32 core 3.x以降のAPI
#else
  ledcSetup(UNIT_BUZZER_CHANNEL, 4000, 10);
  ledcAttachPin(UNIT_BUZZER_PIN, UNIT_BUZZER_CHANNEL);
  ledcWrite(UNIT_BUZZER_CHANNEL, 0);
#endif
}

void unitBuzzerTone(int freqHz, int durationMs) {
  if (!cfg.unitBuzzerEnable) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(UNIT_BUZZER_PIN, freqHz);
#else
  ledcWriteTone(UNIT_BUZZER_CHANNEL, freqHz);
#endif
  unitBuzzerOn = true;
  unitBuzzerOffAt = millis() + durationMs;
}

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

// 内蔵ブザー(HAS_BUZZER)か、UNIT Buzzer(外付け, cfg.unitBuzzerEnable)のいずれかが
// 使える状態かどうか。BEEP表示・トグル操作の有効/無効判定に使う。
bool buzzerAvailable() {
  return HAS_BUZZER || cfg.unitBuzzerEnable;
}

// 内蔵ブザー・UNIT Buzzerのどちらでも同じように鳴らせる共通関数
void soundTone(int freqHz, int durationMs) {
#if HAS_BUZZER
  M5.Speaker.tone(freqHz, durationMs);
#endif
  if (cfg.unitBuzzerEnable) {
    unitBuzzerTone(freqHz, durationMs);
  }
}

// ====================================================================
//  画面表示
// ====================================================================
int screenW() { return M5.Display.width(); }
int screenH() { return M5.Display.height(); }

// updateAutoRotation()が判定する「論理的な向き」(0/1/2/3)を、
// ボードごとのパネル取付け向きに合わせて実際のsetRotation()値へ変換する。
void applyDisplayRotation(int logicalRotation) {
  M5.Display.setRotation((logicalRotation + BOARD_ROTATION_OFFSET) % 4);
}

void drawBeepBadge() {
  if (!buzzerAvailable() || !beepEnabled) return;
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_right);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.drawString("BEEP", screenW() - 2, 2);
}

void redraw() {
  M5.Display.fillScreen(TFT_BLACK);
  drawBeepBadge();

  bool show = instantPowerValid && !dataMute;
  if (!show) return; // タイムアウト/未受信時は数値非表示（黒背景のみ）

  uint16_t fc = TFT_WHITE;
  bool warn = (instantPower >= (long)(cfg.ampereLimit * cfg.ampereRed * 100));
  if (warn) {
    fc = TFT_RED;
    M5.Display.setBrightness(255); // 警告時は最大輝度で強調
  } else {
    M5.Display.setBrightness(cfg.backlightBrightness);
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
  String numStr = String(instantPower);
  int numW = M5.Display.textWidth(numStr);
  int numH = M5.Display.fontHeight();
  M5.Display.drawString(numStr, w / 2, h / 2);

  // 単位"W": 数値フォントの1.2倍サイズで、数値の右下に改行して表示
  float wScale = scale * 0.4f * 3.0f;
  if (wScale < 1.0f) wScale = 1.0f;
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(wScale);
  M5.Display.setTextColor(fc, TFT_BLACK);
  M5.Display.setTextDatum(top_right);
  int wx = w / 2 + numW / 2;
  int wy = h / 2 + numH / 2 + 2;
  M5.Display.drawString("W", wx, wy);
}

// ====================================================================
//  加速度センサーによる画面自動回転
// ====================================================================
static const float ROT_TH_ENTER = 0.55f;
static const unsigned long ROT_HOLD_MS = 350;

void updateAutoRotation() {
  if (!imuAvailable || !autoRotateEnabled) return;
  if (!M5.Imu.update()) return;

  auto data = M5.Imu.getImuData();
  float ax = data.accel.x;
  float ay = data.accel.y;

  int candidate = currentRotation;
  if (ay >= ROT_TH_ENTER)       candidate = 0; // 上向き（標準の持ち方）
  else if (ay <= -ROT_TH_ENTER) candidate = 2; // 180度（上下逆さ）
  else if (ax >= ROT_TH_ENTER)  candidate = 1; // 時計回り90度
  else if (ax <= -ROT_TH_ENTER) candidate = 3; // 反時計回り90度
  else return;

  if (candidate != rotationCandidate) {
    rotationCandidate = candidate;
    rotationCandidateSince = millis();
  }
  if (candidate != currentRotation && millis() - rotationCandidateSince >= ROT_HOLD_MS) {
    currentRotation = candidate;
    applyDisplayRotation(currentRotation);
    redraw();
  }
}

// ====================================================================
//  ボタン / ブザー処理
// ====================================================================
void handleButtons() {
  if (M5.BtnA.wasClicked()) { // BEEP ON/OFF切替
    if (buzzerAvailable()) {
      beepEnabled = !beepEnabled;
      redraw();
    }
  }
  if (M5.BtnB.wasClicked()) { // 画面自動回転のON/OFF切替
    autoRotateEnabled = !autoRotateEnabled;
    rotationCandidate = currentRotation;
    redraw();
  }
}

void handleBeep() {
  if (!buzzerAvailable()) return;
  static unsigned long lastBeep = 0;
  if (dataMute || !instantPowerValid) return;
  bool warn = (instantPower >= (long)(cfg.ampereLimit * cfg.ampereRed * 100));
  if (warn && beepEnabled) {
    if (millis() - lastBeep >= 2100) {
      soundTone(BUZZER_TONE_FREQ_HZ, BUZZER_TONE_DURATION_MS);
      lastBeep = millis();
    }
  }
}

// ====================================================================
//  ESPNOW受信
// ====================================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len <= 0) return;
  String s;
  s.reserve(len);
  for (int i = 0; i < len; i++) s += (char)data[i];
  s.trim();

  if (s.startsWith("NPD=")) {
    long v = s.substring(4).toInt();
    instantPower = v;
    instantPowerValid = true;
    dataMute = false;
    instantPowerMillis = millis();
    redraw();
    Serial.println("[POWER] instant=" + String(instantPower) + "W");
  }
  // "TPD="（積算電力量）はこのボード向けモニターでは扱わない
  // （M5Stack/Core2/CoreS3向けモニターの棒グラフ表示で使用）
}

// ====================================================================
//  設定ポータル
// ====================================================================
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
  M5.Display.println("WiSUN-View-Setup");

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

// 設定ポータルを起動する（呼び出し元でトリガー条件を判定してから呼ぶこと）。
// 保存/初期化されると自動的に再起動するため、この関数から戻ることはない。
void startSetupPortal() {
  // SoftAP(設定ポータル)へ切り替える前に、ESP-NOWとWiFi STA接続を
  // 一旦きちんと停止しておく。アクティブなSTA接続やESP-NOWが残ったまま
  // WiFi.mode(WIFI_AP)へ切り替えると、ESP32のWiFiドライバが不安定になり
  // クラッシュ(意図しない再起動)することがあるための対策。
  // ※ esp_now_init()が一度も呼ばれていない状態でesp_now_deinit()を呼ぶと
  //   クラッシュするため、espNowInitializedフラグで初期化済みかを確認してから呼ぶ。
  //   （未設定の初回起動時は、esp_now_init()より前にこの関数が呼ばれるため必須）
  if (espNowInitialized) {
    esp_now_deinit();
    espNowInitialized = false;
  }
  if (wifiStarted) {
    // 一度もWiFiを起動していない(真っさらな)状態でdisconnect()を呼ぶと、
    // 機種によってはドライバが不安定になりフリーズ(ウォッチドッグによる
    // 意図しない再起動)することがあるため、必要な場合のみ呼ぶ
    // （未設定の初回起動時に必須の対策）。
    WiFi.disconnect(true, true);
    delay(100);
  }

  applyDisplayRotation(0);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("Starting setup portal...");

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
  M5.Display.println("Done!");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("Rebooting...");
  delay(1500);
  ESP.restart();
}

// 初回起動時（未設定時）は自動的に設定ポータルへ入る。
// 2回目以降の設定変更は、起動後にボタンAを長押しする方式にしている
// （handleSetupLongPress()参照。親機wisun_Arduino・M5Stack系子機と統一）。
void enterSetupPortalIfNeeded() {
  bool configured = portal.load(cfg);
  if (!configured) {
    startSetupPortal();
  }
}

// 起動後、ボタンAの長押し(2秒)を検知したら設定ポータルへ入る。
void handleSetupLongPress() {
  static bool triggered = false;
  if (M5.BtnA.pressedFor(2000)) {
    if (!triggered) {
      triggered = true;
      startSetupPortal(); // 戻らない（内部でESP.restart()する）
    }
  } else {
    triggered = false;
  }
}

// ====================================================================
//  setup / loop
// ====================================================================
void setup() {
  auto cfgM5 = M5.config();
  M5.begin(cfgM5);
  Serial.begin(115200);

  applyDisplayRotation(0); // 起動直後は仮の向きで初期化
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  imuAvailable = M5.Imu.isEnabled();
  currentRotation = 0;
  rotationCandidate = 0;
  applyDisplayRotation(currentRotation);

  enterSetupPortalIfNeeded();

  setupUnitBuzzer();

  // WiFi接続（親機と同じAPに接続し、ESPNOWの通信チャンネルを合わせる）
  WiFi.mode(WIFI_STA);
  wifiStarted = true;
  if (cfg.wifiSsid.length() > 0) {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    unsigned long wifiStart = millis();
    M5.Display.println("Connecting WiFi...");
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
      delay(200);
    }
    Serial.println(WiFi.status() == WL_CONNECTED ? ">> WiFi connected" : ">> WiFi connect timeout (continue)");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println(">> ESP-NOW init failed");
  } else {
    espNowInitialized = true;
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println(">> ESP-NOW init OK");
  }

  M5.Display.setBrightness(cfg.backlightBrightness);
  redraw();
  Serial.println(">> Start mainloop!");
}

void loop() {
  M5.update();
  handleSetupLongPress(); // ボタンA長押しで設定ポータルへ
  handleButtons();
  updateAutoRotation();
  handleBeep();
  updateUnitBuzzer();

  // WiFi切断時は定期的に再接続を試みる（ESPNOWのチャンネルずれ対策）
  static unsigned long wifiRetryTc = 0;
  if (cfg.wifiSsid.length() > 0 && WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiRetryTc >= 30000UL) {
      wifiRetryTc = millis();
      WiFi.disconnect();
      WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
      Serial.println(">> WiFi reconnecting...");
    }
  }

  // 受信タイムアウト判定
  if (instantPowerValid) {
    unsigned long since = millis() - instantPowerMillis;
    if (since >= (unsigned long)cfg.timeoutSec * 1000UL && !dataMute) {
      dataMute = true;
      redraw();
    }
  }

  delay(2);
}
