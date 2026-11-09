// PN532 HSU — 왜 이렇게 생겼는지는 nfc.h 머리말에. 프레임 다루기는 heartHunter/src/nfc.cpp 와 같다.

#include "nfc.h"

#include <Arduino.h>

#include "config.h"

// ── PN532 프레임 ────────────────────────────────────────────────
//   00 00 FF  LEN  LCS  TFI  PD0..PDn  DCS  00
//   LEN = TFI + 데이터 바이트 수 · LCS = LEN 의 2의 보수 · TFI = D4(보냄) / D5(받음)
//   DCS = TFI + 데이터 합의 2의 보수
// 명령을 받으면 PN532 는 ACK(00 00 FF 00 FF 00) 를 먼저 보내고 그다음 응답을 보낸다.
static const uint8_t HOST_TO_PN532 = 0xD4;
static const uint8_t PN532_TO_HOST = 0xD5;

struct Candidate {
  int8_t tx, rx;
  const char *where;
};
static const Candidate CANDIDATES[] = {
    {NFC_TX_PIN, NFC_RX_PIN, "P3/P4 커넥터 (GND/3V3/IO17/IO18)"},
    {43, 44, "P1 커넥터 (5V/IO43/IO44/GND)"},
};

static int readByte(uint32_t deadline) {
  while ((int32_t)(millis() - deadline) < 0) {
    if (Serial1.available()) return Serial1.read();
    vTaskDelay(1);  // 코어 0 의 idle 에 자리를 내준다(워치독)
  }
  return -1;
}

static void flushInput() {
  while (Serial1.available()) Serial1.read();
}

static bool findPreamble(uint32_t deadline) {
  uint8_t a = 0xFF, b = 0xFF;  // 0 이 아닌 값으로 시작해야 헛걸림이 없다
  while (true) {
    const int c = readByte(deadline);
    if (c < 0) return false;
    if (a == 0x00 && b == 0x00 && (uint8_t)c == 0xFF) return true;
    a = b;
    b = (uint8_t)c;
  }
}

static void sendCommand(const uint8_t *cmd, uint8_t cmdLen) {
  const uint8_t len = cmdLen + 1;
  uint8_t sum = HOST_TO_PN532;
  for (uint8_t i = 0; i < cmdLen; i++) sum += cmd[i];
  const uint8_t head[] = {0x00, 0x00, 0xFF, len, (uint8_t)(0x100 - len), HOST_TO_PN532};
  Serial1.write(head, sizeof(head));
  Serial1.write(cmd, cmdLen);
  const uint8_t tail[] = {(uint8_t)(0x100 - sum), 0x00};
  Serial1.write(tail, sizeof(tail));
  Serial1.flush();
}

static bool readAck(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  if (!findPreamble(deadline)) return false;
  const int a = readByte(deadline);
  const int b = readByte(deadline);
  readByte(deadline);  // 포스트앰블
  return a == 0x00 && b == 0xFF;
}

// 응답의 데이터부를 buf 에(buf[0] = 응답 명령 코드). 길이를 돌려준다 — 실패하면 -1.
static int readResponse(uint8_t *buf, uint8_t maxLen, uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  if (!findPreamble(deadline)) return -1;
  const int len = readByte(deadline);
  const int lcs = readByte(deadline);
  if (len < 0 || lcs < 0 || ((len + lcs) & 0xFF) != 0) return -1;
  if (len < 1 || (len - 1) > maxLen) return -1;
  const int tfi = readByte(deadline);
  if (tfi != PN532_TO_HOST) return -1;
  uint8_t sum = (uint8_t)tfi;
  const uint8_t n = (uint8_t)(len - 1);
  for (uint8_t i = 0; i < n; i++) {
    const int c = readByte(deadline);
    if (c < 0) return -1;
    buf[i] = (uint8_t)c;
    sum += (uint8_t)c;
  }
  const int dcs = readByte(deadline);
  if (dcs < 0 || ((sum + dcs) & 0xFF) != 0) return -1;
  readByte(deadline);
  return n;
}

static bool command(const uint8_t *cmd, uint8_t cmdLen, uint32_t ackTimeout = 100) {
  flushInput();
  sendCommand(cmd, cmdLen);
  return readAck(ackTimeout);
}

// 카드 찾기를 이쪽이 먼저 포기하면 PN532 는 계속 찾고 있다 — ACK 를 보내 그 명령을 거둔다.
// 안 거두면 다음 명령의 응답이 하나씩 밀린다.
static void abortCommand() {
  static const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  Serial1.write(ACK, sizeof(ACK));
  Serial1.flush();
  delay(2);
  flushInput();
}

// HSU 로 잠든 PN532 를 깨운다(0x55 와 0 을 길게).
static void wakeup() {
  static const uint8_t WAKE[] = {0x55, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Serial1.write(WAKE, sizeof(WAKE));
  Serial1.flush();
  delay(50);
  flushInput();
}

static bool probe(const Candidate &c, uint8_t *ver, uint8_t *rev) {
  Serial1.end();
  Serial1.begin(NFC_BAUD, SERIAL_8N1, c.rx, c.tx);
  delay(20);
  wakeup();
  const uint8_t getFirmware[] = {0x02};
  uint8_t resp[16];
  if (!command(getFirmware, sizeof(getFirmware))) return false;
  if (readResponse(resp, sizeof(resp), 200) < 4 || resp[0] != 0x03) return false;
  *ver = resp[2];
  *rev = resp[3];
  return true;
}

bool nfcBegin(uint8_t *ver, uint8_t *rev, const char **where) {
  const Candidate *hit = nullptr;
  for (const Candidate &c : CANDIDATES) {
    if (probe(c, ver, rev)) {
      hit = &c;
      break;
    }
  }
  if (!hit) {
    Serial1.end();
    return false;
  }
  *where = hit->where;
  uint8_t resp[16];
  // SAMConfiguration — 보안칩 없이 보통 모드로. 빠뜨리면 카드를 못 찾는다.
  const uint8_t samConfig[] = {0x14, 0x01, 0x14, 0x01};
  command(samConfig, sizeof(samConfig));
  readResponse(resp, sizeof(resp), 200);
  // RFConfiguration — 카드 찾기 재시도를 NFC_RETRIES 번으로. 기본값(무한)이면 카드가 없을 때 답이 안 온다.
  const uint8_t rfConfig[] = {0x32, 0x05, 0xFF, 0x01, NFC_RETRIES};
  command(rfConfig, sizeof(rfConfig));
  readResponse(resp, sizeof(resp), 200);
  return true;
}

bool nfcRead(uint8_t *uid, uint8_t *uidLen) {
  // InListPassiveTarget: 한 장 · 106kbps Type A (키링 NTAG213 · 교통카드 · 휴대폰)
  const uint8_t listTarget[] = {0x4A, 0x01, 0x00};
  if (!command(listTarget, sizeof(listTarget), 60)) {
    abortCommand();
    return false;
  }
  uint8_t resp[40];
  const int n = readResponse(resp, sizeof(resp), NFC_SCAN_TIMEOUT_MS);
  if (n < 2) {
    abortCommand();
    return false;
  }
  // [0]=4B [1]=찾은 수 [2]=대상 번호 [3..4]=SENS_RES [5]=SEL_RES [6]=UID 길이 [7..]=UID
  if (resp[0] != 0x4B || resp[1] < 1 || n < 7) return false;
  const uint8_t len = resp[6];
  if (len == 0 || len > 10 || n < 7 + len) return false;
  memcpy(uid, &resp[7], len);
  *uidLen = len;
  return true;
}
