// =====================================================================
//  ConfigPortal.h
//  設定入力用のWebサーバー（設定ポータル）
//    - 初回起動時（未設定時）や、起動時にボタンA長押しした場合に
//      SoftAP + Webサーバーによる「設定ポータル」を立ち上げる。
//    - ブラウザから WiFi SSID/Password、Bルート認証ID/パスワード、
//      アンペアブレーカー値、警告係数、タイムアウト、ESPNOW有無、
//      Ambient連携（チャンネルID/ライトキー）、画面表示設定、
//      UNIT Buzzer使用有無を設定できる。
//    - 設定は NVS(Preferences) に保存し、再起動して通常動作へ移行する。
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

struct WisunHatConfig {
  // WiFi (ESPNOW送出やNTP同期のためSTA接続に使用。必須ではない)
  String wifiSsid = "";
  String wifiPass = "";

  // Bルート認証情報（必須）
  String brId   = "";   // 32桁
  String brPswd = "";   // 12桁

  // 運用パラメータ
  int   ampereLimit = 30;    // 契約ブレーカー値[A]
  float ampereRed   = 0.7f;  // 警告係数(0.0-1.0)
  int   timeoutSec  = 30;    // スマートメーター無応答タイムアウト[秒]
  int   npIntervalSec = 5;   // 瞬時電力値要求サイクル[秒]
  bool  espnowEnable = true; // ESPNOWブロードキャスト配信の有無

  // UNIT Buzzer（M5Stack社製、Groveポート接続の外付けブザー）を使うか。
  // 主にBEEP非搭載のボード（M5StickC無印、M5AtomS3等）向け。
  // 内蔵ブザーを持つ他ボードと同じ「警告音」機能を提供する。
  bool  unitBuzzerEnable = false;

  // バックライト輝度（通常時）。0〜255。警告時は自動的に最大輝度になる。
  int   backlightBrightness = 180;

  // 画面表示設定
  int   fontId    = 7;    // 電力値表示に使うフォント番号（M5GFXの setTextFont() 準拠）
  float fontScale = 0.0f; // フォントの表示倍率。0=自動（画面幅の約80%になる倍率を実行時に計算）

  // Ambient連携（任意）。瞬間電力値用/積算電力量用を別チャンネルとして扱う構成。
  // それぞれ使わない場合はxxxEnable=falseのままでOK
  bool   ambientNowEnable      = false;
  String ambientNowChannelId  = "";   // Ambient_1のチャンネルID(数字)　※瞬間電力値(d1)
  String ambientNowWriteKey   = "";   // Ambient_1のライトキー
  int    ambientNowIntervalSec = 30;  // 瞬間電力値の送信間隔[秒]（Ambient無料枠は5秒/回が下限）

  bool   ambientTotalEnable      = false;
  String ambientTotalChannelId  = ""; // Ambient_2のチャンネルID(数字)　※30分毎積算電力量(d1+created)
  String ambientTotalWriteKey   = ""; // Ambient_2のライトキー
  // 積算電力量は30分毎に受信するたびに、間隔を空けず都度送信する

  bool  configured = false;  // 設定済みフラグ
};

// スキャンキャッシュ（元の Wi-SUN_SCAN.txt 相当）
struct WisunScanCache {
  String channel = "";       // 2桁hex
  String panId   = "";       // 4桁hex
  String macAddr = "";       // 16桁hex
  String lqi     = "";       // 2桁hex
  uint32_t coefficient = 0;
  float    unit        = 0.0f;
  bool     valid = false;
};

class ConfigPortal {
public:
  bool load(WisunHatConfig &cfg) {
    Preferences p;
    p.begin("wisun", true);
    cfg.configured = p.getBool("configured", false);
    if (cfg.configured) {
      cfg.wifiSsid   = p.getString("wifiSsid", "");
      cfg.wifiPass   = p.getString("wifiPass", "");
      cfg.brId       = p.getString("brId", "");
      cfg.brPswd     = p.getString("brPswd", "");
      cfg.ampereLimit = p.getInt("ampereLimit", 30);
      cfg.ampereRed   = p.getFloat("ampereRed", 0.7f);
      cfg.timeoutSec  = p.getInt("timeoutSec", 30);
      cfg.npIntervalSec = p.getInt("npInterval", 5);
      cfg.espnowEnable = p.getBool("espnowEn", true);
      cfg.unitBuzzerEnable = p.getBool("unitBuzzEn", false);
      cfg.backlightBrightness = p.getInt("backlight", 180);
      cfg.fontId    = p.getInt("fontId", 7);
      cfg.fontScale = p.getFloat("fontScale", 0.0f);
      cfg.ambientNowEnable = p.getBool("ambNEn", false);
      cfg.ambientNowChannelId = p.getString("ambNCh", "");
      cfg.ambientNowWriteKey  = p.getString("ambNKey", "");
      cfg.ambientNowIntervalSec = p.getInt("ambNInterval", 30);
      cfg.ambientTotalEnable = p.getBool("ambTEn", false);
      cfg.ambientTotalChannelId = p.getString("ambTCh", "");
      cfg.ambientTotalWriteKey  = p.getString("ambTKey", "");
    }
    p.end();
    return cfg.configured;
  }

  void save(const WisunHatConfig &cfg) {
    Preferences p;
    p.begin("wisun", false);
    p.putBool("configured", true);
    p.putString("wifiSsid", cfg.wifiSsid);
    p.putString("wifiPass", cfg.wifiPass);
    p.putString("brId", cfg.brId);
    p.putString("brPswd", cfg.brPswd);
    p.putInt("ampereLimit", cfg.ampereLimit);
    p.putFloat("ampereRed", cfg.ampereRed);
    p.putInt("timeoutSec", cfg.timeoutSec);
    p.putInt("npInterval", cfg.npIntervalSec);
    p.putBool("espnowEn", cfg.espnowEnable);
    p.putBool("unitBuzzEn", cfg.unitBuzzerEnable);
    p.putInt("backlight", cfg.backlightBrightness);
    p.putInt("fontId", cfg.fontId);
    p.putFloat("fontScale", cfg.fontScale);
    p.putBool("ambNEn", cfg.ambientNowEnable);
    p.putString("ambNCh", cfg.ambientNowChannelId);
    p.putString("ambNKey", cfg.ambientNowWriteKey);
    p.putInt("ambNInterval", cfg.ambientNowIntervalSec);
    p.putBool("ambTEn", cfg.ambientTotalEnable);
    p.putString("ambTCh", cfg.ambientTotalChannelId);
    p.putString("ambTKey", cfg.ambientTotalWriteKey);
    p.end();
  }

  void eraseAll() {
    Preferences p;
    p.begin("wisun", false);
    p.clear();
    p.end();
  }

  // --- スキャンキャッシュ（チャンネル・PANID等）---
  bool loadScan(WisunScanCache &sc) {
    Preferences p;
    p.begin("wisunscan", true);
    sc.valid = p.getBool("valid", false);
    if (sc.valid) {
      sc.channel = p.getString("channel", "");
      sc.panId   = p.getString("panId", "");
      sc.macAddr = p.getString("macAddr", "");
      sc.lqi     = p.getString("lqi", "");
      sc.coefficient = p.getUInt("coeff", 0);
      sc.unit        = p.getFloat("unit", 0.0f);
    }
    p.end();
    return sc.valid;
  }

  void saveScan(const WisunScanCache &sc) {
    Preferences p;
    p.begin("wisunscan", false);
    p.putBool("valid", true);
    p.putString("channel", sc.channel);
    p.putString("panId", sc.panId);
    p.putString("macAddr", sc.macAddr);
    p.putString("lqi", sc.lqi);
    p.putUInt("coeff", sc.coefficient);
    p.putFloat("unit", sc.unit);
    p.end();
  }

  void saveScanCoefficient(uint32_t coeff) {
    Preferences p;
    p.begin("wisunscan", false);
    p.putUInt("coeff", coeff);
    p.end();
  }

  void saveScanUnit(float unit) {
    Preferences p;
    p.begin("wisunscan", false);
    p.putFloat("unit", unit);
    p.end();
  }

  void eraseScan() {
    Preferences p;
    p.begin("wisunscan", false);
    p.clear();
    p.end();
  }

  // -----------------------------------------------------------------
  // 設定ポータル本体（ブロッキング）。保存されると true を返し、
  // 呼び出し側で ESP.restart() することを想定。
  // -----------------------------------------------------------------
  bool runPortal(WisunHatConfig &cfg, void (*idleTick)() = nullptr) {
    const char *AP_SSID = "WiSUN-HAT-Setup";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    DNSServer dns;
    dns.start(53, "*", apIP);

    WebServer server(80);
    bool saved = false;

    server.on("/", HTTP_GET, [&]() {
      server.send(200, "text/html; charset=utf-8", buildFormHtml(cfg));
    });

    server.on("/scan", HTTP_GET, [&]() {
      server.send(200, "application/json", scanWifiJson());
    });

    server.on("/save", HTTP_POST, [&]() {
      cfg.wifiSsid   = server.arg("wifiSsid");
      cfg.wifiPass   = server.arg("wifiPass");
      cfg.brId       = server.arg("brId");
      cfg.brPswd     = server.arg("brPswd");
      cfg.ampereLimit = server.arg("ampereLimit").toInt();
      cfg.ampereRed   = server.arg("ampereRed").toFloat();
      cfg.timeoutSec  = server.arg("timeoutSec").toInt();
      cfg.npIntervalSec = server.arg("npInterval").toInt();
      cfg.fontId    = server.arg("fontId").toInt();
      cfg.fontScale = server.arg("fontScale").toFloat();
      if (cfg.fontScale < 0.0f) cfg.fontScale = 0.0f; // 0=自動
      if (cfg.fontScale > 8.0f) cfg.fontScale = 8.0f;
      cfg.espnowEnable  = server.hasArg("espnowEnable");
      cfg.unitBuzzerEnable = server.hasArg("unitBuzzerEnable");
      cfg.backlightBrightness = server.arg("backlight").toInt();
      if (cfg.backlightBrightness < 10) cfg.backlightBrightness = 10;
      if (cfg.backlightBrightness > 255) cfg.backlightBrightness = 255;

      cfg.ambientNowEnable    = server.hasArg("ambientNowEnable");
      cfg.ambientNowChannelId = server.arg("ambientNowChannelId");
      cfg.ambientNowWriteKey  = server.arg("ambientNowWriteKey");
      cfg.ambientNowIntervalSec = server.arg("ambientNowInterval").toInt();
      if (cfg.ambientNowIntervalSec < 5) cfg.ambientNowIntervalSec = 5; // Ambient無料枠の下限保護

      cfg.ambientTotalEnable    = server.hasArg("ambientTotalEnable");
      cfg.ambientTotalChannelId = server.arg("ambientTotalChannelId");
      cfg.ambientTotalWriteKey  = server.arg("ambientTotalWriteKey");

      // 入力値の簡易バリデーション（Bルート認証情報は桁数が固定のため）
      String msg = "";
      if (cfg.brId.length() != 32) msg += "BルートIDは32文字で入力してください。<br>";
      if (cfg.brPswd.length() != 12) msg += "Bルートパスワードは12文字で入力してください。<br>";
      if (cfg.wifiSsid.length() == 0) msg += "WiFi SSIDを入力してください。<br>";
      if (cfg.ambientNowEnable && cfg.ambientNowChannelId.length() == 0) msg += "Ambient(瞬間電力用)のチャンネルIDを入力してください。<br>";
      if (cfg.ambientNowEnable && cfg.ambientNowWriteKey.length() == 0) msg += "Ambient(瞬間電力用)のライトキーを入力してください。<br>";
      if (cfg.ambientTotalEnable && cfg.ambientTotalChannelId.length() == 0) msg += "Ambient(積算電力用)のチャンネルIDを入力してください。<br>";
      if (cfg.ambientTotalEnable && cfg.ambientTotalWriteKey.length() == 0) msg += "Ambient(積算電力用)のライトキーを入力してください。<br>";

      if (msg.length() > 0) {
        server.send(200, "text/html; charset=utf-8",
          "<html><body><h3>入力エラー</h3><p>" + msg +
          "</p><a href=\"/\">戻る</a></body></html>");
        return;
      }

      cfg.configured = true;
      save(cfg);
      eraseScan(); // WiFi/Bルート設定が変わった可能性があるのでスキャンキャッシュは破棄

      server.send(200, "text/html; charset=utf-8",
        "<html><body><h3>保存しました。再起動します…</h3></body></html>");
      saved = true;
    });

    // 端末を譲渡・廃棄する際などに、保存済みの設定を全て消去するためのエンドポイント。
    // ブラウザ側で確認ダイアログを出した上でPOSTする想定（buildFormHtml内のJS参照）。
    server.on("/reset", HTTP_POST, [&]() {
      eraseAll();
      eraseScan();
      server.send(200, "text/html; charset=utf-8",
        "<html><body><h3>全ての設定を初期化しました。再起動します…</h3></body></html>");
      saved = true;
    });

    server.onNotFound([&]() {
      // captive portal: どんなURLでも設定画面へ誘導
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    });

    server.begin();

    unsigned long lastBlink = millis();
    while (!saved) {
      dns.processNextRequest();
      server.handleClient();
      if (idleTick) idleTick();
      delay(2);
    }
    // 保存後、少しウェイトしてレスポンスを送り切ってから戻る
    delay(500);
    server.handleClient();
    delay(500);
    return true;
  }

private:
  String scanWifiJson() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i) json += ",";
      json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) +
              "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    WiFi.scanDelete();
    return json;
  }

  static String jsonEscape(const String &s) {
    String r;
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '"' || c == '\\') r += '\\';
      r += c;
    }
    return r;
  }

  String buildFormHtml(const WisunHatConfig &cfg) {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html lang='ja'><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Wi-SUN HAT 設定</title>";
    html += "<style>body{font-family:sans-serif;margin:16px;background:#f4f4f4;}"
            "h2{color:#333;} .card{background:#fff;padding:16px;border-radius:8px;"
            "box-shadow:0 1px 3px rgba(0,0,0,.2);margin-bottom:16px;}"
            "label{display:block;margin-top:10px;font-weight:bold;}"
            "input[type=text],input[type=password],input[type=number],select{"
            "width:100%;padding:8px;box-sizing:border-box;margin-top:4px;}"
            "button{margin-top:16px;padding:10px 20px;background:#2a7de1;color:#fff;"
            "border:none;border-radius:4px;font-size:16px;}"
            "small{color:#666;}</style></head><body>";
    html += "<h2>Wi-SUN HAT 設定</h2>";
    html += "<form action='/save' method='POST'>";

    html += "<div class='card'><h3>WiFi設定</h3>";
    html += "<label>SSID</label>";
    html += "<input type='text' id='wifiSsid' name='wifiSsid' list='ssidList' value='" + htmlEscape(cfg.wifiSsid) + "' required>";
    html += "<datalist id='ssidList'></datalist>";
    html += "<small id='scanStatus'>ページを開くと周辺のAPを自動スキャンします</small>";
    html += "<label>パスワード</label>";
    html += "<input type='password' name='wifiPass' value='" + htmlEscape(cfg.wifiPass) + "'>";
    html += "</div>";

    html += "<div class='card'><h3>Wi-SUN(Bルート)設定</h3>";
    html += "<label>Bルート認証ID (32文字)</label>";
    html += "<input type='text' name='brId' maxlength='32' value='" + htmlEscape(cfg.brId) + "' required>";
    html += "<label>Bルートパスワード (12文字)</label>";
    html += "<input type='text' name='brPswd' maxlength='12' value='" + htmlEscape(cfg.brPswd) + "' required>";
    html += "</div>";

    html += "<div class='card'><h3>運用設定</h3>";
    html += "<label>契約ブレーカー値 [A]</label>";
    html += "<input type='number' name='ampereLimit' min='20' value='" + String(cfg.ampereLimit) + "'>";
    html += "<label>警告係数 (0.0〜1.0)</label>";
    html += "<input type='number' step='0.05' min='0' max='1' name='ampereRed' value='" + String(cfg.ampereRed, 2) + "'>";
    html += "<label>スマートメーター無応答タイムアウト [秒]</label>";
    html += "<input type='number' min='5' name='timeoutSec' value='" + String(cfg.timeoutSec) + "'>";
    html += "<label>瞬時電力値の要求サイクル [秒]</label>";
    html += "<input type='number' min='5' name='npInterval' value='" + String(cfg.npIntervalSec) + "'>";
    html += "<label><input type='checkbox' name='espnowEnable' style='width:auto' " +
            String(cfg.espnowEnable ? "checked" : "") + "> ESPNOWで子機へブロードキャスト配信する</label>";
    html += "<label><input type='checkbox' name='unitBuzzerEnable' style='width:auto' " +
            String(cfg.unitBuzzerEnable ? "checked" : "") + "> UNIT Buzzer（Grove接続の外付けブザー）を使う</label>";
    html += "<small>M5StickC（無印）やM5AtomS3など本体にブザーが無い機種向けです。Groveポート等に"
            "<a href='https://docs.m5stack.com/ja/unit/buzzer' target='_blank'>UNIT Buzzer</a>"
            "を接続すると、StickC系内蔵ブザーと同じ「契約アンペア超過の警告音」機能が使えます。</small>";
    html += "</div>";

    html += "<div class='card'><h3>画面表示設定</h3>";
    html += "<label>電力値のフォント</label>";
    html += "<select name='fontId'>";
    {
      struct FontOpt { int id; const char *label; };
      static const FontOpt fontOpts[] = {
        {0, "標準フォント（小さめ、英数字OK）"},
        {2, "標準フォント（やや大きめ）"},
        {4, "標準フォント（大きめ）"},
        {6, "数字・記号フォント（大きめ）"},
        {7, "7セグメント風（数字のみ、見やすい）"},
        {8, "特大フォント（数字のみ）"},
      };
      for (auto &opt : fontOpts) {
        html += "<option value='" + String(opt.id) + "'" +
                (cfg.fontId == opt.id ? " selected" : "") + ">" + opt.label + "</option>";
      }
    }
    html += "</select>";
    html += "<label>表示倍率 (0=自動 / 1.0〜8.0で手動指定)</label>";
    html += "<input type='number' step='0.5' min='0' max='8' name='fontScale' value='" + String(cfg.fontScale, 1) + "'>";
    html += "<small>0にすると、4桁の数字が画面幅の約80%になるように自動計算されます"
            "（画面回転で幅が変わっても追従します）。手動で固定したい場合は1.0以上を入力してください。"
            "フォント種類によって実際の見た目の大きさは変わるため、保存後の画面表示を見ながら調整してください。</small>";
    html += "<label>バックライト輝度 (10〜255)</label>";
    html += "<input type='number' min='10' max='255' name='backlight' value='" + String(cfg.backlightBrightness) + "'>";
    html += "<small>通常時のバックライト輝度です。契約アンペア超過の警告時は自動的に最大輝度になります。</small>";
    html += "</div>";

    html += "<div class='card'><h3>Ambient連携（任意）</h3>";
    html += "<small>瞬間電力値用・積算電力量用を別チャンネルとして送信します。"
            "チャンネルID・ライトキーは https://ambidata.io/ のチャンネル作成後に確認できます。</small>";

    html += "<h4 style='margin-top:14px'>瞬間電力値 (d1)</h4>";
    html += "<label><input type='checkbox' id='ambNowEn' name='ambientNowEnable' style='width:auto' " +
            String(cfg.ambientNowEnable ? "checked" : "") +
            " onchange=\"document.getElementById('ambNowFields').style.display=this.checked?'block':'none'\"> "
            "瞬間電力値をAmbientへ送信する</label>";
    html += "<div id='ambNowFields' style='display:" + String(cfg.ambientNowEnable ? "block" : "none") + "'>";
    html += "<label>チャンネルID</label>";
    html += "<input type='text' name='ambientNowChannelId' inputmode='numeric' value='" + htmlEscape(cfg.ambientNowChannelId) + "'>";
    html += "<label>ライトキー</label>";
    html += "<input type='text' name='ambientNowWriteKey' value='" + htmlEscape(cfg.ambientNowWriteKey) + "'>";
    html += "<label>送信間隔 [秒] (Ambient無料枠は5秒以上推奨)</label>";
    html += "<input type='number' min='5' name='ambientNowInterval' value='" + String(cfg.ambientNowIntervalSec) + "'>";
    html += "</div>";

    html += "<h4 style='margin-top:14px'>30分毎積算電力量 (d1 + 計測時刻)</h4>";
    html += "<label><input type='checkbox' id='ambTotalEn' name='ambientTotalEnable' style='width:auto' " +
            String(cfg.ambientTotalEnable ? "checked" : "") +
            " onchange=\"document.getElementById('ambTotalFields').style.display=this.checked?'block':'none'\"> "
            "積算電力量をAmbientへ送信する</label>";
    html += "<div id='ambTotalFields' style='display:" + String(cfg.ambientTotalEnable ? "block" : "none") + "'>";
    html += "<label>チャンネルID</label>";
    html += "<input type='text' name='ambientTotalChannelId' inputmode='numeric' value='" + htmlEscape(cfg.ambientTotalChannelId) + "'>";
    html += "<label>ライトキー</label>";
    html += "<input type='text' name='ambientTotalWriteKey' value='" + htmlEscape(cfg.ambientTotalWriteKey) + "'>";
    html += "<small>スマートメーターから30分毎に受信した時点で、間隔待ちせず都度送信します。</small>";
    html += "</div>";

    html += "</div>";

    html += "<button type='submit'>保存して再起動</button>";
    html += "</form>";

    // 端末を譲渡・廃棄する際などに、保存済みの設定を全て消去するための操作。
    // 誤操作防止のため、確認ダイアログを経てから送信する。
    html += "<div class='card' style='border:2px solid #d9362f'>";
    html += "<h3 style='color:#d9362f'>全設定の初期化</h3>";
    html += "<p>WiFi・Bルート認証情報を含む、保存されている設定を全て削除します。"
            "端末を譲渡・廃棄する際などにご利用ください。<br>"
            "<strong>この操作は取り消せません。</strong></p>";
    html += "<form action='/reset' method='POST' "
            "onsubmit=\"return confirm('保存されている設定を全て削除します。よろしいですか？この操作は取り消せません。');\">";
    html += "<button type='submit' style='background:#d9362f'>全設定を初期化する</button>";
    html += "</form>";
    html += "</div>";

    html += "<script>"
            "fetch('/scan').then(r=>r.json()).then(list=>{"
            "  var dl=document.getElementById('ssidList');"
            "  list.sort((a,b)=>b.rssi-a.rssi);"
            "  list.forEach(ap=>{var o=document.createElement('option');o.value=ap.ssid;dl.appendChild(o);});"
            "  document.getElementById('scanStatus').innerText = list.length+'件のAPが見つかりました';"
            "});"
            "</script>";

    html += "</body></html>";
    return html;
  }

  static String htmlEscape(const String &s) {
    String r;
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '"') r += "&quot;";
      else if (c == '<') r += "&lt;";
      else if (c == '>') r += "&gt;";
      else if (c == '&') r += "&amp;";
      else r += c;
    }
    return r;
  }
};
