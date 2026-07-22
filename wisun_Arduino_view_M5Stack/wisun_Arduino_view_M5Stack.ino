// =====================================================================
//  wisun_Arduino_view_M5Stack.ino
//
//  rev0.0.3 2026.7.22 @rin_ofumi
//
//  wisun_Arduino（親機）がESPNOWでブロードキャストする瞬時電力値・
//  積算電力量を受信し、画面に表示するモニター（子機）用サンプル
//  プログラムです。M5Stack (Basic/Gray) / M5Stack Core2 / M5Stack CoreS3
//  を対象としています。
//
//  これらの機種は画面解像度(320x240)・ボタン操作(A/B/C。Core2/CoreS3は
//  タッチによる仮想ボタンをM5Unifiedが自動的にエミュレート)が共通のため、
//  ボード選択の切替は不要です（M5Unifiedが実行時に自動判別します）。
//  画面は横長固定です（元のMicroPython版にも回転機能はありません。
//  棒グラフ等のレイアウトが横長前提のため、本プログラムでも踏襲しています）。
//
//  BEEP(ブザー)の扱いは、親機(wisun_Arduino)と同じ考え方に揃えています
//  （内蔵スピーカー＋任意でUNIT Buzzerを併用可能、共通の周波数で鳴動）。
//
//  必要ライブラリ (Arduino IDE / PlatformIO):
//   - M5Unified            (M5Stack公式)
//   - WiFi / WebServer / DNSServer / Preferences / esp_now
//     （いずれも ESP32 Arduino core 同梱）
//
//  同じフォルダに ViewConfigM5Stack.h が必要です。
// =====================================================================

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>

#include "ViewConfigM5Stack.h"

// ---- ブザー(BEEP)機能 ----
// M5Stack(Basic/Gray)・Core2・CoreS3はいずれも内蔵スピーカーを搭載しているため、
// 外付けブザーは不要で、常に内蔵スピーカーでBEEPを鳴らす。
#define HAS_BUZZER 1

// 警告音パラメータ（親機 wisun_Arduino と共通の値。低い周波数だと機種に
// よっては音が小さく聞き取りにくいことがあるため4000Hzにしている）
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

bool lcdMute     = false; // ボタンAでのバックライトミュート状態
bool espNowInitialized = false; // esp_now_init()が成功したかどうか
bool wifiStarted = false; // WiFi.mode(WIFI_STA)等を一度でも呼んだことがあるか
bool beepEnabled = true;  // ボタンCでのBEEP有効/無効
bool dispModeLarge = true; // true:瞬時電力値の大画面表示 / false:積算電力量の棒グラフ表示

// 30分値の棒グラフ用バッファ。TIME_TB[n]が「当日のn番目(0-47)の30分スロット」に対応し、
// TotalPower[]は「前日の後半48スロット(1-48) + 当日48スロット(49-96)」を保持する
// リングバッファ状の構成（元のMicroPython実装をそのまま踏襲）。
const char *TIME_TB[48] = {
  "00:00:00","00:30:00","01:00:00","01:30:00","02:00:00","02:30:00",
  "03:00:00","03:30:00","04:00:00","04:30:00","05:00:00","05:30:00",
  "06:00:00","06:30:00","07:00:00","07:30:00","08:00:00","08:30:00",
  "09:00:00","09:30:00","10:00:00","10:30:00","11:00:00","11:30:00",
  "12:00:00","12:30:00","13:00:00","13:30:00","14:00:00","14:30:00",
  "15:00:00","15:30:00","16:00:00","16:30:00","17:00:00","17:30:00",
  "18:00:00","18:30:00","19:00:00","19:30:00","20:00:00","20:30:00",
  "21:00:00","21:30:00","22:00:00","22:30:00","23:00:00","23:30:00",
};
long   totalPowerBuf[97] = {0}; // 単位: Wh
String dayBuf = "";
String lastTotalPowerDate = "";
String lastTotalPowerTime = "";

int timeTbIndex(const String &t) {
  for (int i = 0; i < 48; i++) {
    if (t == TIME_TB[i]) return i;
  }
  return -1;
}

// ====================================================================
//  ブザー（内蔵スピーカー）
// ====================================================================
// M5Stack/Core2/CoreS3はいずれも内蔵スピーカーを搭載しているため、
// 外付けブザーの分岐は不要。setup()での初期化も不要。
void soundTone(int freqHz, int durationMs) {
  M5.Speaker.tone(freqHz, durationMs);
}

// ====================================================================
//  画面表示
// ====================================================================
// 画面レイアウト（320x240を想定、横長固定）:
//   y=0〜189   : 瞬時電力値の大画面表示 or 積算電力量の棒グラフ
//   y=190〜239 : 下部バー（BEEPボタン・最終受信日時）
static const int GRAPH_TOP    = 64;
static const int GRAPH_BOTTOM = 186;
static const int FOOTER_TOP   = 205;

void drawBeepButton() {
  int x = 230, y = 213, w = 58, h = 20;
  uint16_t bg = beepEnabled ? TFT_GREEN : TFT_BLACK;
  uint16_t tc = beepEnabled ? TFT_WHITE : 0x7BEF; // 消灯時はグレー
  M5.Display.fillRoundRect(x, y, w, h, 10, bg);
  M5.Display.drawRoundRect(x, y, w, h, 10, TFT_CYAN);
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(tc, bg);
  M5.Display.drawString("BEEP", x + w / 2, y + h / 2);
}

void drawDateLabel() {
  M5.Display.fillRect(0, FOOTER_TOP, 229, 35, TFT_BLACK);
  if (lastTotalPowerDate.length() == 0) return;
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(lastTotalPowerDate + " " + lastTotalPowerTime, 2, FOOTER_TOP + 8);
}

// 瞬時電力値の描画（表示モードにより、大画面表示 or 上部帯のみの表示に切替）
void drawPowerValue() {
  uint16_t fc = TFT_WHITE;
  bool show = instantPowerValid && !dataMute;
  bool warn = show && (instantPower >= (long)(cfg.ampereLimit * cfg.ampereRed * 100));
  if (warn) {
    fc = TFT_RED;
    if (lcdMute) M5.Display.setBrightness(cfg.backlightNormal); // 警告時はバックライト強制ON
  } else {
    if (lcdMute) M5.Display.setBrightness(cfg.backlightMuted);
  }

  int areaW = M5.Display.width();
  int areaTop, areaH;
  if (dispModeLarge) {
    areaTop = 0;
    areaH = 190; // 下部バーを除いた領域
  } else {
    areaTop = 0;
    areaH = GRAPH_TOP; // 上部の帯のみ
  }
  M5.Display.fillRect(0, areaTop, areaW, areaH, TFT_BLACK);

  if (!show) return;

  int centerY = areaTop + areaH / 2;
  String numStr = String(instantPower);

  if (dispModeLarge) {
    // 瞬時電力値の大画面表示: 数値を中央に、"W"は数値の右下に改行して表示
    // （wisun_Arduino_view_StickC と同じ考え方）
    M5.Display.setTextFont(cfg.fontId); // Web設定で選んだフォント
    float scale = cfg.fontScale;
    if (scale <= 0.0f) {
      // 自動計算: 表示エリア幅に収まる倍率を実測して求める(0=自動時)
      M5.Display.setTextSize(1);
      int refW = M5.Display.textWidth("8888");
      if (refW < 1) refW = 1;
      float targetW = areaW * 0.62f; // "W"表示分の余白を残す
      scale = targetW / (float)refW;
      if (scale < 1.0f) scale = 1.0f;
      scale = floorf(scale * 2.0f) / 2.0f;
    }
    M5.Display.setTextSize(scale);
    M5.Display.setTextColor(fc, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    int numW = M5.Display.textWidth(numStr);
    int numH = M5.Display.fontHeight();
    int centerX = areaW / 2;
    M5.Display.drawString(numStr, centerX, centerY);

    // 単位"W": 数値フォントの1.2倍サイズで、数値の右下に改行して表示
    float wScale = scale * 1.2f;
    if (wScale < 1.0f) wScale = 1.0f;
    M5.Display.setTextFont(0);
    M5.Display.setTextSize(wScale);
    M5.Display.setTextColor(fc, TFT_BLACK);
    M5.Display.setTextDatum(top_right);
    M5.Display.drawString("W", centerX + numW / 2, centerY + numH / 2 + 2);
  } else {
    // 棒グラフモード上部の帯表示: "W"は改行せず数値の右に添え、
    // 「数値+W」全体をまとめて表示エリアの中央に寄せる
    M5.Display.setTextFont(cfg.fontId);
    float scale = cfg.fontScale;
    if (scale <= 0.0f) {
      M5.Display.setTextSize(1);
      int refW = M5.Display.textWidth("8888");
      if (refW < 1) refW = 1;
      float targetW = areaW * 0.34f;
      scale = targetW / (float)refW;
      if (scale < 1.0f) scale = 1.0f;
      scale = floorf(scale * 2.0f) / 2.0f;
    }
    M5.Display.setTextSize(scale); // 高さ測定の前に正しいサイズを反映させておく
    int numW = M5.Display.textWidth(numStr);
    int numH = M5.Display.fontHeight();

    const int wFontSize = 2;
    const int gap = 8; // 数値と"W"の間隔[px]
    M5.Display.setTextFont(0);
    M5.Display.setTextSize(wFontSize);
    int wWidth = M5.Display.textWidth("W");

    int totalW = numW + gap + wWidth;
    int startX = (areaW - totalW) / 2;
    if (startX < 0) startX = 0;

    // 数値と"W"のフォントサイズが異なるため、middle(上下中央)基準だと
    // "W"が宙に浮いて見える。bottom(下揃え)基準にし、かつ表示エリアの
    // 下端(=棒グラフ領域のすぐ上)に直接揃えることで、棒グラフへの
    // はみ出しを防ぐ。数値が大きすぎてエリア上端をはみ出す場合は、
    // 上端基準に切り替えて上方向へのはみ出しを防ぐ。
    int margin = 2;
    int baselineY = areaTop + areaH - margin;
    if (baselineY - numH < areaTop) {
      baselineY = areaTop + numH;
    }

    M5.Display.setTextFont(cfg.fontId);
    M5.Display.setTextSize(scale);
    M5.Display.setTextColor(fc, TFT_BLACK);
    M5.Display.setTextDatum(bottom_left);
    M5.Display.drawString(numStr, startX, baselineY);

    M5.Display.setTextFont(0);
    M5.Display.setTextSize(wFontSize);
    M5.Display.setTextColor(fc, TFT_BLACK);
    M5.Display.setTextDatum(bottom_left);
    M5.Display.drawString("W", startX + numW + gap, baselineY);
  }
}

// 積算電力量の棒グラフ描画（30分毎、当日+前日の96コマ分）
void drawGraphTp() {
  const float graphScale = 3.0f;   // グラフ表示倍率（ややサチる値にしている）
  const float graphRed = 0.7f;     // グラフ赤色しきい値(0.0-1.0)
  const float graphOrange = 0.3f;  // グラフ橙色しきい値(0.0-1.0)
  const int width = 5;
  const int barAreaTop = GRAPH_TOP + 1;
  const int barAreaBottom = GRAPH_BOTTOM;
  const int barAreaHeight = barAreaBottom - barAreaTop;

  M5.Display.drawLine(0, GRAPH_TOP, 320, GRAPH_TOP, 0xAEAEAE);
  M5.Display.drawLine(0, GRAPH_BOTTOM, 320, GRAPH_BOTTOM, 0xAEAEAE);
  for (int i = 0; i <= 48; i += 12) {
    int x = 15 + 6 * i;
    M5.Display.drawLine(x, GRAPH_BOTTOM, x, GRAPH_BOTTOM + 4, 0xAEAEAE);
  }

  M5.Display.setTextFont(0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  const char *axisLabels[5] = {"00:00", "06:00", "12:00", "18:00", "24:00"};
  int axisX[5] = {0, 72, 144, 216, 288};
  for (int i = 0; i < 5; i++) {
    M5.Display.drawString(axisLabels[i], axisX[i], GRAPH_BOTTOM + 2);
  }

  M5.Display.fillRect(0, barAreaTop, 320, barAreaHeight, TFT_BLACK);
  for (int i = 0; i <= 48; i += 12) {
    int x = 15 + 6 * i;
    M5.Display.drawLine(x, barAreaTop, x, barAreaBottom, 0x303030);
  }

  for (int n = 1; n <= 96; n++) {
    long hPower = 0;
    if (totalPowerBuf[n] != 0 && totalPowerBuf[n - 1] != 0) {
      hPower = totalPowerBuf[n] - totalPowerBuf[n - 1];
    }

    int height = 0;
    if (hPower > 0) {
      height = (int)((float)hPower * graphScale / (float)cfg.ampereLimit);
    }
    if (height > 120) height = 120;

    int xStart;
    uint16_t color = TFT_BLACK;
    if (n <= 48) {
      xStart = ((n - 1) * 6) + 16;
      if (height != 0) color = 0xAEAEAE; // 前日分は灰色
    } else {
      xStart = (((n - 1) - 48) * 6) + 16;
      if (height != 0) {
        if (height > (120 * graphRed)) color = TFT_RED;
        else if (height > (120 * graphOrange)) color = 0xFFAC00;
        else color = 0x2ACF00;
      }
    }

    int yStart = barAreaBottom - 1 - height;
    if (height != 0) {
      M5.Display.fillRect(xStart, yStart, width, height, color);
    }
  }
}

void redraw() {
  // グラフ領域の掃除(or描画)を先に行い、瞬時電力値の描画はその後にする。
  // 逆順にすると、大画面モード時に「グラフ領域の掃除」が瞬時電力値の
  // 描画範囲(y方向)と重なっており、せっかく描画した数値を消してしまう。
  if (!dispModeLarge) {
    drawGraphTp();
  } else {
    M5.Display.fillRect(0, GRAPH_TOP, 320, FOOTER_TOP - GRAPH_TOP, TFT_BLACK);
  }
  drawPowerValue();
  drawDateLabel();
  drawBeepButton();
}

// ====================================================================
//  ボタン処理
// ====================================================================
void handleButtons() {
  if (M5.BtnA.wasClicked()) { // バックライトミュート切替
    lcdMute = !lcdMute;
    M5.Display.setBrightness(lcdMute ? cfg.backlightMuted : cfg.backlightNormal);
  }
  if (M5.BtnB.wasClicked()) { // 表示モード切替（瞬時電力値 ⇔ 棒グラフ）
    dispModeLarge = !dispModeLarge;
    redraw();
  }
  if (M5.BtnC.wasClicked()) { // BEEP ON/OFF切替
    beepEnabled = !beepEnabled;
    drawBeepButton();
  }
}

void handleBeep() {
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
void handleTotalPowerMessage(const String &payload) {
  // payload形式: "<kWh>/<YYYY-MM-DD HH:MM:SS>"（親機からのTPD=メッセージのデータ部）
  int slashIdx = payload.indexOf('/');
  if (slashIdx < 0) return;
  float kwh = payload.substring(0, slashIdx).toFloat();
  long wh = (long)(kwh * 1000.0f);

  String datetime = payload.substring(slashIdx + 1);
  datetime.trim();
  int spaceIdx = datetime.indexOf(' ');
  if (spaceIdx < 0) return;
  String tpdDate = datetime.substring(0, spaceIdx);
  String tpdTime = datetime.substring(spaceIdx + 1);

  int idx = timeTbIndex(tpdTime);
  if (idx < 0) return; // 30分境界以外の時刻は想定外のため無視

  // 日跨ぎ処理
  if (dayBuf.length() == 0) {
    dayBuf = tpdDate;
    if (idx == 0) totalPowerBuf[48] = wh;
  } else if (dayBuf != tpdDate) {
    if (idx >= 1) {
      dayBuf = tpdDate;
      for (int n = 48; n <= 96; n++) totalPowerBuf[n - 48] = totalPowerBuf[n];
      for (int n = 49; n <= 96; n++) totalPowerBuf[n] = 0;
    }
  }

  if (idx == 0) {
    totalPowerBuf[96] = wh;
  } else {
    totalPowerBuf[idx + 48] = wh;
  }

  lastTotalPowerDate = tpdDate;
  lastTotalPowerTime = tpdTime;

  Serial.println("[TOTAL] " + String(kwh, 2) + "kWh @ " + tpdDate + " " + tpdTime);

  drawDateLabel();
  if (!dispModeLarge) {
    drawGraphTp();
  }
}

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
    drawPowerValue();
    drawBeepButton();
    Serial.println("[POWER] instant=" + String(instantPower) + "W");
  } else if (s.startsWith("TPD=")) {
    handleTotalPowerMessage(s.substring(4));
  }
}

// ====================================================================
//  設定ポータル
// ====================================================================
void drawSetupScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextFont(0);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(8, 8);
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
// ※ Core2/CoreS3はタッチ式の仮想ボタンのため、M5.begin()直後・M5.update()未実行の
//   段階ではisPressed()が電源ON時点の押下状態を正しく取得できないことがある。
//   そのため「起動時にボタンAを押しながら電源ON」による強制起動はサポートせず、
//   起動後にloop()側でボタンAの長押しを検知して起動する方式にしている
//   （handleSetupLongPress()参照）。
void enterSetupPortalIfNeeded() {
  bool configured = portal.load(cfg);
  if (!configured) {
    startSetupPortal();
  }
}

// 起動後、ボタンAの長押し(2秒)を検知したら設定ポータルへ入る。
// 物理ボタン(M5Stack Basic/Gray)・タッチ式仮想ボタン(Core2/CoreS3)のどちらでも、
// loop()内でM5.update()が呼ばれた後の状態を見るため確実に検知できる。
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

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  enterSetupPortalIfNeeded();

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

  M5.Display.setBrightness(cfg.backlightNormal);
  redraw();
  Serial.println(">> Start mainloop!");
}

void loop() {
  M5.update();
  handleSetupLongPress(); // ボタンA長押しで設定ポータルへ
  handleButtons();
  handleBeep();

  static unsigned long wifiRetryTc = 0;
  if (cfg.wifiSsid.length() > 0 && WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiRetryTc >= 30000UL) {
      wifiRetryTc = millis();
      WiFi.disconnect();
      WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
      Serial.println(">> WiFi reconnecting...");
    }
  }

  if (instantPowerValid) {
    unsigned long since = millis() - instantPowerMillis;
    if (since >= (unsigned long)cfg.timeoutSec * 1000UL && !dataMute) {
      dataMute = true;
      drawPowerValue();
    }
  }

  delay(2);
}
