// =====================================================================
//  ViewConfigM5Stack.h
//  子機（モニター表示機）用の設定値保存(NVS)＋Web設定ポータル。
//
//  親機(wisun_Arduino)がESPNOWでブロードキャストする瞬時電力値・
//  積算電力量を受信し、画面に表示するためだけの機能に絞った、
//  軽量な設定ポータルです。WiFiは、親機と同じアクセスポイントに
//  接続することで、ESPNOWの通信チャンネルを親機側と一致させる目的で
//  使用します（インターネット接続自体が必要というわけではありません）。
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

struct ViewConfig {
  String wifiSsid = "";
  String wifiPass = "";

  // 警報しきい値: 瞬時電力値が (ampereLimit * ampereRed * 100) [W] 以上で
  // 警告表示(赤字)・BEEP鳴動の対象になる
  int   ampereLimit = 30;    // 契約ブレーカー値[A]
  float ampereRed   = 0.7f;  // 警告係数(0.0-1.0)
  int   timeoutSec  = 30;    // 親機からの受信が途絶えたとみなすまでの時間[秒]

  int   backlightNormal = 255; // 通常時バックライト輝度(10〜255。0は見えなくなるため不可)
  int   backlightMuted  = 0;   // ボタンAでミュートした際のバックライト輝度(0〜255。0=消灯も許容)

  int   fontId    = 7;    // 電力値表示に使うフォント番号（M5GFXの setTextFont() 準拠）
  float fontScale = 0.0f; // フォントの表示倍率。0=自動（表示エリア幅に応じて計算）

  bool  configured = false;
};

class ViewConfigPortal {
public:
  bool load(ViewConfig &cfg) {
    Preferences p;
    p.begin("wisunview", true);
    cfg.configured = p.getBool("configured", false);
    if (cfg.configured) {
      cfg.wifiSsid = p.getString("wifiSsid", "");
      cfg.wifiPass = p.getString("wifiPass", "");
      cfg.ampereLimit = p.getInt("ampereLimit", 30);
      cfg.ampereRed   = p.getFloat("ampereRed", 0.7f);
      cfg.timeoutSec  = p.getInt("timeoutSec", 30);
      cfg.backlightNormal = p.getInt("bkNormal", 255);
      cfg.backlightMuted  = p.getInt("bkMuted", 0);
      cfg.fontId    = p.getInt("fontId", 7);
      cfg.fontScale = p.getFloat("fontScale", 0.0f);
    }
    p.end();
    return cfg.configured;
  }

  void save(const ViewConfig &cfg) {
    Preferences p;
    p.begin("wisunview", false);
    p.putBool("configured", true);
    p.putString("wifiSsid", cfg.wifiSsid);
    p.putString("wifiPass", cfg.wifiPass);
    p.putInt("ampereLimit", cfg.ampereLimit);
    p.putFloat("ampereRed", cfg.ampereRed);
    p.putInt("timeoutSec", cfg.timeoutSec);
    p.putInt("bkNormal", cfg.backlightNormal);
    p.putInt("bkMuted", cfg.backlightMuted);
    p.putInt("fontId", cfg.fontId);
    p.putFloat("fontScale", cfg.fontScale);
    p.end();
  }

  void eraseAll() {
    Preferences p;
    p.begin("wisunview", false);
    p.clear();
    p.end();
  }

  bool runPortal(ViewConfig &cfg, void (*idleTick)() = nullptr) {
    const char *AP_SSID = "WiSUN-View-Setup";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);

    DNSServer dns;
    dns.start(53, "*", WiFi.softAPIP());

    WebServer server(80);
    bool done = false;

    server.on("/", HTTP_GET, [&]() {
      server.send(200, "text/html; charset=utf-8", buildFormHtml(cfg));
    });

    server.on("/scan", HTTP_GET, [&]() {
      server.send(200, "application/json", scanWifiJson());
    });

    server.on("/save", HTTP_POST, [&]() {
      cfg.wifiSsid   = server.arg("wifiSsid");
      cfg.wifiPass   = server.arg("wifiPass");
      cfg.ampereLimit = server.arg("ampereLimit").toInt();
      cfg.ampereRed   = server.arg("ampereRed").toFloat();
      cfg.timeoutSec  = server.arg("timeoutSec").toInt();
      cfg.backlightNormal = server.arg("backlightNormal").toInt();
      // 通常時輝度は0(真っ暗)にできてしまうと画面が見えなくなるため下限を設ける
      if (cfg.backlightNormal < 10) cfg.backlightNormal = 10;
      if (cfg.backlightNormal > 255) cfg.backlightNormal = 255;

      cfg.backlightMuted  = server.arg("backlightMuted").toInt();
      // ミュート時は意図的に0(消灯)を許容するため下限は設けない。上限のみガード
      if (cfg.backlightMuted < 0) cfg.backlightMuted = 0;
      if (cfg.backlightMuted > 255) cfg.backlightMuted = 255;
      cfg.fontId    = server.arg("fontId").toInt();
      cfg.fontScale = server.arg("fontScale").toFloat();
      if (cfg.fontScale < 0.0f) cfg.fontScale = 0.0f;
      if (cfg.fontScale > 8.0f) cfg.fontScale = 8.0f;

      String msg = "";
      if (cfg.wifiSsid.length() == 0) msg += "WiFi SSIDを入力してください。<br>";
      if (cfg.ampereLimit < 10) msg += "契約ブレーカー値は10A以上を入力してください。<br>";
      if (cfg.ampereRed <= 0.0f || cfg.ampereRed > 1.0f) msg += "警告係数は0.0より大きく1.0以下で入力してください。<br>";

      if (msg.length() > 0) {
        server.send(200, "text/html; charset=utf-8",
          "<html><body><h3>入力エラー</h3><p>" + msg + "</p><a href=\"/\">戻る</a></body></html>");
        return;
      }

      cfg.configured = true;
      save(cfg);
      server.send(200, "text/html; charset=utf-8",
        "<html><body><h3>保存しました。再起動します…</h3></body></html>");
      done = true;
    });

    server.on("/reset", HTTP_POST, [&]() {
      eraseAll();
      server.send(200, "text/html; charset=utf-8",
        "<html><body><h3>全ての設定を初期化しました。再起動します…</h3></body></html>");
      done = true;
    });

    server.onNotFound([&]() {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    });

    server.begin();

    while (!done) {
      dns.processNextRequest();
      server.handleClient();
      if (idleTick) idleTick();
      delay(2);
    }
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
      json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
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

  String buildFormHtml(const ViewConfig &cfg) {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html lang='ja'><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Wi-SUN View 設定</title>";
    html += "<style>body{font-family:sans-serif;margin:16px;background:#f4f4f4;}"
            "h2{color:#333;} .card{background:#fff;padding:16px;border-radius:8px;"
            "box-shadow:0 1px 3px rgba(0,0,0,.2);margin-bottom:16px;}"
            "label{display:block;margin-top:10px;font-weight:bold;}"
            "input[type=text],input[type=password],input[type=number],select{"
            "width:100%;padding:8px;box-sizing:border-box;margin-top:4px;}"
            "button{margin-top:16px;padding:10px 20px;background:#2a7de1;color:#fff;"
            "border:none;border-radius:4px;font-size:16px;}"
            "small{color:#666;}</style></head><body>";
    html += "<h2>Wi-SUN View（子機）設定</h2>";
    html += "<form action='/save' method='POST'>";

    html += "<div class='card'><h3>WiFi設定</h3>";
    html += "<label>SSID（親機と同じWiFiに接続してください）</label>";
    html += "<input type='text' id='wifiSsid' name='wifiSsid' list='ssidList' value='" + htmlEscape(cfg.wifiSsid) + "' required>";
    html += "<datalist id='ssidList'></datalist>";
    html += "<small id='scanStatus'>ページを開くと周辺のAPを自動スキャンします</small>";
    html += "<label>パスワード</label>";
    html += "<input type='password' name='wifiPass' value='" + htmlEscape(cfg.wifiPass) + "'>";
    html += "</div>";

    html += "<div class='card'><h3>警報しきい値</h3>";
    html += "<label>契約ブレーカー値 [A]</label>";
    html += "<input type='number' name='ampereLimit' min='10' value='" + String(cfg.ampereLimit) + "'>";
    html += "<label>警告係数 (0.0〜1.0)</label>";
    html += "<input type='number' step='0.05' min='0.05' max='1' name='ampereRed' value='" + String(cfg.ampereRed, 2) + "'>";
    html += "<small>瞬時電力値が「契約ブレーカー値×警告係数×100」[W]以上になると、"
            "画面が赤字表示・BEEP鳴動の対象になります。</small>";
    html += "<label>受信タイムアウト [秒]</label>";
    html += "<input type='number' min='5' name='timeoutSec' value='" + String(cfg.timeoutSec) + "'>";
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
    html += "<small>0の場合、表示モード（瞬時電力値の大画面表示／棒グラフ上部の帯表示）"
            "それぞれの表示エリア幅に収まるよう自動計算します。</small>";
    html += "<label>バックライト輝度（通常時） (10〜255)</label>";
    html += "<input type='number' min='10' max='255' name='backlightNormal' value='" + String(cfg.backlightNormal) + "'>";
    html += "<label>バックライト輝度（ボタンAでミュートした時） (0〜255)</label>";
    html += "<input type='number' min='0' max='255' name='backlightMuted' value='" + String(cfg.backlightMuted) + "'>";
    html += "</div>";

    html += "<button type='submit'>保存して再起動</button>";
    html += "</form>";

    html += "<div class='card' style='border:2px solid #d9362f'>";
    html += "<h3 style='color:#d9362f'>全設定の初期化</h3>";
    html += "<p>保存されている設定を全て削除します。端末を譲渡・廃棄する際などにご利用ください。<br>"
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
};
