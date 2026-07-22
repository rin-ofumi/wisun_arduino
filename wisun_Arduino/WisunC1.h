// =====================================================================
//  WisunC1.h
//  BP35C1-J11-T01（Wi-SUN HAT-C1）用のバイナリプロトコル送受信ドライバ
//
//  BP35A1がテキストベースのATコマンド（SKxxxx + "\r\n"）であるのに対し、
//  BP35C1-J11-T01は独自のバイナリフレーム形式（12バイト固定ヘッダ＋
//  可変長データ部、チェックサム付き）でやり取りする。
//
//  フレーム構造（ROHM「J11 UART IFコマンド仕様書」準拠）:
//   [0:4)  ユニークコード  要求:0xD0EA83FC / 応答:0xD0F9EE5D
//   [4:6)  コマンドコード
//   [6:8)  メッセージ長（ヘッダ+データ チェックサム4バイトを含む）
//   [8:10) ヘッダ部チェックサム
//   [10:12)データ部チェックサム
//   [12:]  データ部（可変長）
// =====================================================================
#pragma once
#include <Arduino.h>
#include <vector>

class WisunC1 {
public:
  explicit WisunC1(HardwareSerial &s) : port(s) {}

  void clearBuf() {
    while (port.available()) port.read();
    readbuf.clear();
    line.clear();
  }

  // 受信バッファから1フレーム抽出できたら true を返す（line に格納）。
  // 呼び出し側はループの中で毎回ポーリングすること。
  bool poll() {
    while (port.available()) readbuf.push_back((uint8_t)port.read());

    static const uint8_t MAGIC[4] = {0xD0, 0xF9, 0xEE, 0x5D};
    size_t n = readbuf.size();
    for (size_t i = 0; i + 4 <= n; i++) {
      if (readbuf[i] == MAGIC[0] && readbuf[i+1] == MAGIC[1] &&
          readbuf[i+2] == MAGIC[2] && readbuf[i+3] == MAGIC[3]) {
        if (i + 8 > n) return false; // メッセージ長フィールドがまだ来ていない
        int length = readbuf[i+6] * 256 + readbuf[i+7] - 4;
        if (length < 0 || length > 4096) {
          continue; // 誤検出（マジック的な偶然の並び）。次のiで再探索
        }
        size_t total = i + 12 + (size_t)length;
        if (n < total) return false; // データ部がまだ全部来ていない
        line.assign(readbuf.begin() + i, readbuf.begin() + total);
        readbuf.erase(readbuf.begin(), readbuf.begin() + total);
        return true;
      }
    }
    if (readbuf.size() > 4096) { // ゴミで無限に肥大化しないようガード
      readbuf.erase(readbuf.begin(), readbuf.begin() + (readbuf.size() - 256));
    }
    return false;
  }

  int retcmd() const {
    if (line.size() >= 6) return line[4] * 256 + line[5];
    return -1;
  }

  std::vector<uint8_t> retdata() const {
    if (line.size() < 12) return {};
    int len = line[6] * 256 + line[7] - 4;
    if (len <= 0 || line.size() < (size_t)(12 + len)) return {};
    return std::vector<uint8_t>(line.begin() + 12, line.begin() + 12 + len);
  }

  void sendCmd(uint16_t cmd, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> bin = {0xd0, 0xea, 0x83, 0xfc,
                                 (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xff)};
    uint16_t length = 4 + (uint16_t)data.size();
    bin.push_back((uint8_t)(length >> 8));
    bin.push_back((uint8_t)(length & 0xff));

    uint32_t sum = 0;
    for (auto b : bin) sum = (sum + b) & 0xffff;
    bin.push_back((uint8_t)(sum >> 8));
    bin.push_back((uint8_t)(sum & 0xff));

    uint32_t sum2 = 0;
    for (auto b : data) sum2 = (sum2 + b) & 0xffff;
    bin.push_back((uint8_t)(sum2 >> 8));
    bin.push_back((uint8_t)(sum2 & 0xff));

    bin.insert(bin.end(), data.begin(), data.end());
    port.write(bin.data(), bin.size());
  }

private:
  HardwareSerial &port;
  std::vector<uint8_t> readbuf;
  std::vector<uint8_t> line;
};

// ---- 共通ヘルパー ----

// ECHONET-Lite電文Payload用のデータ長プレフィックス付与（2byte BigEndian長 + data）
inline std::vector<uint8_t> c1DataWithLength(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> out;
  uint16_t len = (uint16_t)data.size();
  out.push_back((uint8_t)(len >> 8));
  out.push_back((uint8_t)(len & 0xff));
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

inline std::vector<uint8_t> c1ToVector(const uint8_t *data, size_t len) {
  return std::vector<uint8_t>(data, data + len);
}

// 16進文字列 <-> バイト列 相互変換（scanCacheの保存フォーマットをA1と共通化するため）
inline String c1BytesToHex(const std::vector<uint8_t> &data) {
  String s;
  char buf[3];
  for (auto b : data) {
    snprintf(buf, sizeof(buf), "%02X", b);
    s += buf;
  }
  return s;
}

inline std::vector<uint8_t> c1HexToBytes(const String &hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.length(); i += 2) {
    out.push_back((uint8_t)strtol(hex.substring(i, i + 2).c_str(), nullptr, 16));
  }
  return out;
}
