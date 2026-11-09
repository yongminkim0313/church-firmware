#pragma once
// ─────────────────────────────────────────────────────────────────
// PN532 — UART(HSU) 로 직접 말한다 (simple_v1 · JC3248W535)
//
// 이 보드의 P3 커넥터(JST 1.25mm 4핀: GND · 3V3 · IO17 · IO18 — 회로도로 확인)에 PN532 를 그대로 꽂는다.
// 옆의 P4(QWIIC 1.0mm)도 같은 핀이라 케이블이 맞는 쪽 아무 데나(둘 다는 안 된다).
//   3V3 → VCC · GND → GND · IO17(송신) → PN532 RXD · IO18(수신) → PN532 TXD   ★ 송수신은 엇갈려 꽂는다
//   PN532 모듈의 딥스위치는 **HSU**(보통 둘 다 OFF).
// USB 옆 P1(5V · IO43 · IO44 · GND)에 꽂아도 된다 — 켤 때 두 자리를 차례로 두드려 대답하는 쪽을 쓴다.
//
// v2·v3 리더는 Adafruit_PN532(SPI)를 썼다. 이 보드에는 SPI 를 낼 핀이 없어(8핀 커넥터는 전원이 없다)
// UART 로 왔고, 쓸 명령이 셋(펌웨어 확인 · 설정 · 카드 찾기)뿐이라 heartHunter(같은 보드)에서 돌던
// 구현을 그대로 가져왔다. 같은 카드를 올려 둔 채로 거르는 것은 main.cpp 의 nfcScanOnce 가 한다(v2 와 같다).
//
// ★ nfcTask(코어 0) **만** 부른다 — 한 번에 한 명령만 오가야 응답이 어긋나지 않는다.
// ─────────────────────────────────────────────────────────────────

#include <stddef.h>
#include <stdint.h>

// UART 를 열고 PN532 를 찾는다. 찾았으면 true(펌웨어 버전을 ver·rev 에, 꽂힌 자리를 where 에).
bool nfcBegin(uint8_t *ver, uint8_t *rev, const char **where);

// 카드 한 장을 찾아본다. 있으면 true 와 UID(4·7·10 바이트). 없으면 곧바로(수십 ms) false.
bool nfcRead(uint8_t *uid, uint8_t *uidLen);
