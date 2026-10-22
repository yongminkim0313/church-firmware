// 상태 불 — 세 겹에서 색을 골라 WS2812 한 알에 보낸다.
// 어떤 색이 무엇을 뜻하는지는 led.h 머리말에 있다. 이 파일은 그것을 어떻게 하는지만 담는다.

#include "led.h"

#if LED_ENABLE

#include <string.h>

// ── 색 ───────────────────────────────────────────────────────────
// 꽉 찬 값(0~255)으로 적고, 밝기는 아래 표의 bright 로 따로 깎는다.
// ★ 노랑의 녹색이 255 가 아닌 까닭은 config.h 의 LED_YELLOW_G 에 적어 두었다
//   (WS2812 의 녹색 알이 밝아 255 로 두면 연두로 보인다).
struct LedColor {
  uint8_t r, g, b;
};
static const LedColor C_RED = {255, 0, 0};
static const LedColor C_YELLOW = {255, LED_YELLOW_G, 0};
static const LedColor C_GREEN = {0, 255, 0};

struct LedSpec {
  LedColor c;
  uint8_t bright;  // 0~255 — 아래 scale() 로 색에 곱한다
  bool blink;
  const char *name;
};

// 자리는 LedState 와 같은 차례다(SPECS[st]).
static const LedSpec SPECS[LED_STATE_COUNT] = {
    {C_YELLOW, LED_BRIGHT, false, "켜지는중"},
    {C_GREEN, LED_BRIGHT_IDLE, false, "준비됨"},
    {C_RED, LED_BRIGHT, true, "와이파이끊김"},
    {C_YELLOW, LED_BRIGHT, false, "물어보는중"},
    {C_GREEN, LED_BRIGHT, false, "됐다"},
    {C_YELLOW, LED_BRIGHT, false, "알림"},
    {C_RED, LED_BRIGHT, false, "서버무응답"},
    {C_RED, LED_BRIGHT, false, "치명"},
};

// ── 세 겹 ────────────────────────────────────────────────────────
#define LED_NONE 0xFF  // 잠깐 겹이 비었다

static uint8_t base = LED_BOOT;
static bool fatal = false;
static bool busy = false;
static uint8_t flashState = LED_NONE;
static uint32_t flashUntil = 0;

// 마지막으로 핀에 보낸 것. 같은 값이면 다시 보내지 않는다.
static uint8_t lastR = 0, lastG = 0, lastB = 0;
static bool wroteOnce = false;

static uint32_t changes = 0;

static inline uint8_t scale(uint8_t v, uint8_t bright) {
  return (uint8_t)(((uint16_t)v * bright) / 255);
}

static uint8_t pick() {
  if (fatal) return LED_FATAL;
  if (busy) return LED_BUSY;
  if (flashState != LED_NONE) {
    if ((int32_t)(millis() - flashUntil) < 0) return flashState;
    flashState = LED_NONE;  // 지났다 — 바탕으로 내려온다
  }
  return base;
}

static void paint(uint8_t st, bool on) {
  const LedSpec &s = SPECS[st];
  const uint8_t r = on ? scale(s.c.r, s.bright) : 0;
  const uint8_t g = on ? scale(s.c.g, s.bright) : 0;
  const uint8_t b = on ? scale(s.c.b, s.bright) : 0;
  if (wroteOnce && r == lastR && g == lastG && b == lastB) return;
  rgbLedWrite(LED_PIN, r, g, b);
  lastR = r;
  lastG = g;
  lastB = b;
  wroteOnce = true;
  changes++;
}

// 겹을 다시 셈해 지금 색을 밝힌다. 세터마다 이것을 부른다 — setup 과 치명 화면처럼
// **본 고리가 돌지 않는 자리**에서도 색이 바로 바뀌어야 한다.
static void apply() {
  const uint8_t st = pick();
  // 깜빡이는 상태는 '켜짐' 반쪽부터 — 바뀐 것이 눈에 먼저 들어오게.
  paint(st, SPECS[st].blink ? (((millis() / LED_BLINK_MS) & 1) == 0) : true);
}

// ── 바깥에서 부르는 것 ───────────────────────────────────────────
void ledBegin() {
  Serial.printf("[불] GPIO%d · 밝기 %d/255 (대기 %d) · WS2812 한 알\n", LED_PIN, LED_BRIGHT,
                LED_BRIGHT_IDLE);
  apply();
}

void ledLoop() { apply(); }

void ledBase(uint8_t st) {
  if (fatal) return;  // 기기가 섰다 — 그 뒤로는 아무것도 안 덮는다
  if (st >= LED_STATE_COUNT) return;
  if (st == LED_FATAL) fatal = true;
  base = st;
  apply();
}

void ledBusy(bool on) {
  busy = on;
  apply();
}

void ledFlash(uint8_t st, uint32_t ms) {
  if (st >= LED_STATE_COUNT) return;
  flashState = st;
  flashUntil = millis() + ms;
  apply();
}

// ── 안내문 한 줄 → 색 ───────────────────────────────────────────
// ★ 여기 글자는 main.cpp 가 화면에 그리는 글자와 **같아야** 한다(sound.cpp 의
//   NOTE_CLIPS 와 같은 사정이다 — 고칠 때 두 표를 함께 본다). 어긋나도 초록으로
//   거짓말은 하지 않는다: 아래 마지막 갈래가 모르는 안내문을 노랑으로 받기 때문에,
//   잃는 것은 '서버에 물어보지 못했어요' 하나뿐이다(빨강이어야 할 것이 노랑이 된다).
//   그래서 이 표에는 **빨강만 적어도 된다** — 노랑은 적지 않아도 노랑으로 떨어진다.
//   적어 두는 까닭은 어느 문구가 어느 색인지 한눈에 보이게 하려는 것이다.
static const char NOTE_ATTEND_PREFIX[] = "출석 완료 +";

struct NoteLed {
  const char *note;
  uint8_t st;
};
static const NoteLed NOTE_LEDS[] = {
    {"이미 출석했어요", LED_WARN},
    {"등록되지 않은 키링", LED_WARN},
    {"서버에 물어보지 못했어요", LED_ERROR},
};

void ledTag(const char *note, bool known, uint32_t ms) {
  if (!note) note = "";

  // 지급됐다 — 가장 좋은 끝이다.
  if (!strncmp(note, NOTE_ATTEND_PREFIX, sizeof(NOTE_ATTEND_PREFIX) - 1)) {
    ledFlash(LED_OK, ms);
    return;
  }

  for (size_t i = 0; i < sizeof(NOTE_LEDS) / sizeof(NOTE_LEDS[0]); i++) {
    if (!strcmp(note, NOTE_LEDS[i].note)) {
      ledFlash(NOTE_LEDS[i].st, ms);
      return;
    }
  }

  // 안내문 칸이 비었다 = 그냥 조회다. 잔액이 떴으면 잘 끝난 것이다.
  // 이름표에 없는 아이는 노랑 — 화면에는 '등록되지 않은 키링' 이 떠 있다.
  if (!note[0]) {
    ledFlash(known ? LED_OK : LED_WARN, ms);
    return;
  }

  // 모르는 안내문 — 서버가 교회 설정으로 지어 보낸 거절 안내다('화요일은 출석을 안
  // 받아요' 따위). 글자는 미리 알 수 없지만 **거절이라는 것**은 안다 → 노랑.
  ledFlash(LED_WARN, ms);
}

// ── 시험·상태 ────────────────────────────────────────────────────
// ★ 바탕(ledBase)이 아니라 **잠깐 겹**에 얹는다 — 본 고리가 매번 바탕을 와이파이
//   상태로 되돌려 놓으므로, 바탕에 적으면 다음 한 바퀴에 지워져 눈에 보이지도 않는다.
//   치명(LED_FATAL)도 여기서는 색만 보는 것이라 잠깐 겹으로 둔다(기기를 세우지 않는다).
#define LED_TEST_MS 4000
void ledTestNext() {
  static uint8_t step = 0;
  Serial.printf("[불] 시험 %u/%u — %s\n", step + 1, (unsigned)LED_STATE_COUNT, SPECS[step].name);
  ledFlash(step, LED_TEST_MS);
  step = (uint8_t)((step + 1) % LED_STATE_COUNT);
}

void ledReport() {
  const uint8_t st = pick();
  Serial.printf("[불] %s (바탕 %s%s%s) · rgb %d,%d,%d · 바뀜 %lu번\n", SPECS[st].name,
                SPECS[base].name, busy ? " · 물어보는중" : "",
                flashState != LED_NONE ? " · 잠깐" : "", lastR, lastG, lastB,
                (unsigned long)changes);
}

#else  // LED_ENABLE 0 — 불 없이 구울 때. 부르는 자리를 그대로 두고 아무 일도 안 한다.

void ledBegin() {}
void ledLoop() {}
void ledBase(uint8_t) {}
void ledBusy(bool) {}
void ledFlash(uint8_t, uint32_t) {}
void ledTag(const char *, bool, uint32_t) {}
void ledTestNext() { Serial.println("[불] 꺼 두었습니다 (LED_ENABLE 0)"); }
void ledReport() { Serial.println("[불] 꺼 두었습니다 (LED_ENABLE 0)"); }

#endif
