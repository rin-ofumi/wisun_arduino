// =====================================================================
//  WisunUdp.h
//  スマートメーター(ECHONET Lite / 低圧スマート電力量メータクラス 0x0288 01)
//  からの応答を解析するパーサー。
//
//  BP35A1 / BP35C1 から ASCIIモード(WOPT 01)で受信した ERXUDP 行を解析し、
//  瞬時電力計測値(E7)、瞬時電流計測値(E8)、積算電力量係数(D3)、
//  積算電力量単位(E1)、定時積算電力量(EA、応答/一斉同報の両方)を取り出す。
//
//  BP35A1コマンドリファレンス上のERXUDPの列構成（ASCIIモード, 空白区切り）:
//    [0]ERXUDP [1]SENDER_IP [2]DEST_IP [3]RPORT [4]LPORT
//    [5]SENDER_LLA [6]SECURED [7]SIDE(orDATALEN) [8]DATA
//   -> 列数9、cols[8] が ECHONET Lite フレーム(16進ASCII文字列)
// =====================================================================
#pragma once
#include <Arduino.h>

class WisunUdp {
public:
  // --- スマートメーターから取得できる値 ---
  uint32_t power_coefficient = 0;      // 積算電力量係数 (D3)
  float    power_unit        = 0.0f;   // 積算電力量単位 (E1)

  long          instant_power       = 0;      // 瞬時電力計測値 [W]
  bool          instant_power_valid = false;   // 一度でも受信したか
  unsigned long instant_power_millis = 0;       // 受信時刻(millis)

  float instant_amp_r = 0.0f;  // 瞬時電流計測値 R相(T相との対を想定) [A]
  float instant_amp_t = 0.0f;  // 瞬時電流計測値 T相 [A]

  double total_power          = 0.0;   // 定時積算電力量 [kWh換算後]
  String total_power_datetime = "";    // 計測時刻 "YYYY-MM-DD HH:MM:SS"
  bool   total_power_valid    = false;

  // 直近 read() で判定された種別: "" / "D3" / "E7" / "E1" / "EA72" / "EA73"
  String type = "";

  // UART受信した1行（ERXUDP ...）を渡して解析する
  // 戻り値: type と同じ（空文字なら対象外の行）
  String read(const String &lineIn) {
    type = "";

    String line = lineIn;
    line.trim();
    if (!line.startsWith("ERXUDP")) {
      return type;
    }

    // 空白区切りで分割（列数9のはず）
    String cols[10];
    int colCount = splitBySpace(line, cols, 10);
    if (colCount != 9) {
      return type; // 列数不一致（不完全行など）は無視
    }

    String res = cols[8];               // ECHONET Liteフレーム(hex ASCII)
    if ((int)res.length() < 24) {
      return type;                       // 最低限のヘッダ長にも満たない
    }

    String seoj = res.substring(8, 14);  // 送信元オブジェクトコード
    String esv  = res.substring(20, 22); // サービスコード

    if (seoj == "028801" && esv == "72") {
      // ---- Get_Res（要求に対する応答） ----
      int len = res.length();
      String epc = res.substring(24, 26);

      if (len == 36) {
        if (epc == "D3") {                              // 積算電力量係数
          type = "D3";
          power_coefficient = strtoul(res.substring(len - 8).c_str(), nullptr, 16);
        } else if (epc == "E7") {                        // 瞬時電力計測値(単独)
          type = "E7";
          instant_power = (long)strtoul(res.substring(len - 8).c_str(), nullptr, 16);
          instant_power_valid = true;
          instant_power_millis = millis();
        }
      } else if (len == 30) {
        if (epc == "E1") {                                // 積算電力量単位
          type = "E1";
          String u = res.substring(len - 2);
          power_unit = unitCodeToFloat(u);
        }
      } else if (len == 48) {
        // 瞬時電力計測値(E7) + 瞬時電流計測値(E8) がセットで返る場合
        String epcE7 = res.substring(24, 26);
        if (epcE7 == "E7") {
          type = "E7";
          instant_power = (long)strtoul(res.substring(len - 20, len - 12).c_str(), nullptr, 16);
          instant_power_valid = true;
          instant_power_millis = millis();
        }
        String epcE8 = res.substring(36, 38);
        if (epcE8 == "E8") {
          instant_amp_r = (float)strtol(res.substring(len - 8, len - 4).c_str(), nullptr, 16) * 0.1f;
          instant_amp_t = (float)strtol(res.substring(len - 4).c_str(), nullptr, 16) * 0.1f;
        }
      } else if (len == 50) {
        if (epc == "EA") {                                // 定時積算電力量(要求への応答)
          type = "EA72";
          int Y  = strtol(res.substring(len - 22, len - 18).c_str(), nullptr, 16);
          int Mo = strtol(res.substring(len - 18, len - 16).c_str(), nullptr, 16);
          int D  = strtol(res.substring(len - 16, len - 14).c_str(), nullptr, 16);
          int H  = strtol(res.substring(len - 14, len - 12).c_str(), nullptr, 16);
          int Mi = strtol(res.substring(len - 12, len - 10).c_str(), nullptr, 16);
          int S  = strtol(res.substring(len - 10, len - 8).c_str(), nullptr, 16);
          unsigned long raw = strtoul(res.substring(len - 8).c_str(), nullptr, 16);
          total_power = (double)raw * (double)power_coefficient * (double)power_unit;
          char buf[24];
          snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", Y, Mo, D, H, Mi, S);
          total_power_datetime = String(buf);
          total_power_valid = true;
        }
      }
    } else if (seoj == "028801" && esv == "73") {
      // ---- INF（定時積算電力量の一斉同報。売電側も含まれるため長さ固定） ----
      int len = res.length();
      if (len == 76) {
        String epc = res.substring(24, 26);
        if (epc == "EA" && power_coefficient != 0 && power_unit != 0.0f) {
          type = "EA73";
          int Y  = strtol(res.substring(28, 32).c_str(), nullptr, 16);
          int Mo = strtol(res.substring(32, 34).c_str(), nullptr, 16);
          int D  = strtol(res.substring(34, 36).c_str(), nullptr, 16);
          int H  = strtol(res.substring(36, 38).c_str(), nullptr, 16);
          int Mi = strtol(res.substring(38, 40).c_str(), nullptr, 16);
          int S  = strtol(res.substring(40, 42).c_str(), nullptr, 16);
          unsigned long raw = strtoul(res.substring(42, 50).c_str(), nullptr, 16);
          total_power = (double)raw * (double)power_coefficient * (double)power_unit;
          char buf[24];
          snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", Y, Mo, D, H, Mi, S);
          total_power_datetime = String(buf);
          total_power_valid = true;
        }
      }
    }

    return type;
  }

private:
  static int splitBySpace(const String &s, String *out, int maxOut) {
    int n = 0;
    int start = 0;
    int len = s.length();
    while (start <= len && n < maxOut) {
      int sp = s.indexOf(' ', start);
      if (sp < 0) {
        out[n++] = s.substring(start);
        break;
      } else {
        out[n++] = s.substring(start, sp);
        start = sp + 1;
      }
    }
    return n;
  }

  static float unitCodeToFloat(const String &u) {
    if (u == "00") return 1.0f;
    if (u == "01") return 0.1f;
    if (u == "02") return 0.01f;
    if (u == "03") return 0.001f;
    if (u == "04") return 0.0001f;
    if (u == "0A") return 10.0f;
    if (u == "0B") return 100.0f;
    if (u == "0C") return 1000.0f;
    if (u == "0D") return 10000.0f;
    return 0.0f;
  }
};
