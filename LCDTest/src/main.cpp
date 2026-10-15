// LCDTest — PN532 NFC 리더(SPI) + GC9A01 원형 디스플레이(SPI), 글자만으로 그리는 판
//
// 보드: ESP32-S3-N16R8 · GC9A01 240×240 원형 IPS(SPI) · PN532(SPI)
// nfcProject/master 의 기능을 이 하드웨어에 옮긴 것이다. 다른 점만 먼저 적는다:
//   · 사진이 없다 — 화면은 **글자만**으로 그린다(microSD 도 없어 사진을 둘 곳도 없다).
//   · 리더가 이 보드에 직접 붙어 있다 — 참고 기기는 리더가 딴 보드라 선(I2C)·ESP-NOW 로
//     받아 왔지만, 여기서는 PN532 를 직접 읽는다. 그 두 길은 넣지 않았다.
//   · 터치가 없다 — 보드의 BOOT 버튼으로 대신한다(짧게: 대기 화면 · 길게: 설정 받기).
//
// ── 화면 네 가지 ──────────────────────────────────────────────────
//   대기   : 금색 고리가 돌며 "카드를 올려 주세요". 아래에 연결 상태를 한 줄로.
//   NFC    : 이름(큰 글씨) · 지금 기분 · 안내문 · 잔여 포인트. NFC_SHOW_MS 뒤 대기로.
//   일괄   : 관리 화면의 일괄 지급·사용 — 제목 + 받은 아이 명단을 한 화면에.
//   상태   : 한 줄 알림(동기화·오류 등).
//
// ── 태그는 세 갈래로 온다 ─────────────────────────────────────────
//   PN532     이 보드에 붙은 리더가 읽는다(주된 길).
//   WebSocket 서버(:81)에 붙은 브라우저·폰·외부 시스템이 {"uid":"…"} 를 보낸다.
//   허브      이 보드가 펀펀포인트 서버에 client 로 붙어 있고, 서버가 밀어 준다.
//   세 길의 글자는 **똑같다** — 푸는 곳도 handleCommand 하나다.
//
// ── 서버 설정 · 출석모드 (관리자 화면 '리더 설정 → v2.0') ─────────
//   와이파이에 붙으면 GET /api/talent/config/v2 로 받고, 그 뒤 CONFIG_TTL_MS 마다 다시.
//     · 밝기(backlight 1~16)     받는 즉시 백라이트 PWM 에 적용된다
//     · 출석모드(attendanceMode) 켜면 키링을 댄 그 자리에서 바로 출석 포인트가 지급된다
//     · 출석 카드(attendanceCard) 얼마를 어떤 사유로 줄지는 서버의 그 카드가 정한다.
//                                비어 있으면 지급하지 않고 잔액만 띄운다.
//   같은 키링이 ATTEND_COOLDOWN_MS 안에 또 오면 "이미 출석했어요" 만 띄우고 주지 않는다.
//   ★ 관리 화면에서 값을 바꾸면 서버가 허브로 {"state":"config"} 를 밀어 준다 —
//     5분 주기를 기다리지 않고 그 자리에서 다시 받는다. 받은 뒤 출석모드가 바뀌었으면
//     지금 상태를 알려 주므로(buildState 의 `attend`), 관리 화면이 그것을 되비춘다.
//
// ── 배선 ──────────────────────────────────────────────────────────
//   GC9A01 — FSPI(SPI2)
//     VCC→3V3  GND→GND  SCL/SCK→GPIO15  SDA/MOSI→GPIO16
//     CS→GPIO4  DC/RS→GPIO5  RST→GPIO6  BLK→GPIO7(PWM)
//   PN532 — HSPI(SPI3), 디스플레이와 별도 버스
//     VCC→3V3  GND→GND  SCK→GPIO9  MISO→GPIO10  MOSI→GPIO11  SS/CS→GPIO12
//     IRQ·RSTO 미연결 · 모듈 모드 스위치를 SPI 로 (보통 SEL0=OFF, SEL1=ON)
//   ※ GPIO3 은 이 보드에서 LOW 로 고착되어 쓰지 않는다.
//
// ── 시리얼 명령 (115200) ──────────────────────────────────────────
//   h 대기 화면 · c 설정 받기 · i 상태 · t 시험 화면 · g 고리 3초 · w 와이파이 다시
//   s 최근 출석 목록 · r 재부팅 · ? 도움말
// ─────────────────────────────────────────────────────────────────

#include <Adafruit_PN532.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <U8g2lib.h>  // 굴림 한글 폰트
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

// https 왕복·JSON·WebSocket 이 모두 loop 에서 돈다 — 기본 8KB 로는 빠듯하다.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ── 핀 ───────────────────────────────────────────────────────────
#define TFT_CS 4
#define TFT_DC 5
#define TFT_RST 6
#define TFT_BLK 7
#define TFT_SCK 15
#define TFT_MOSI 16

#define TFT_SPI_HZ 40000000L
#define BLK_FREQ 5000
#define BLK_RES 8

#define PN532_SCK 9
#define PN532_MISO 10
#define PN532_MOSI 11
#define PN532_CS 12

#define SCAN_TIMEOUT_MS 60  // 한 번의 스캔에 쓰는 시간. 길면 화면이 느려진다

// ── 한글 폰트 ────────────────────────────────────────────────────
// 굴림 16px 로 그린다. 한 글자가 16픽셀 칸이라 2배로 늘려도 자모가 또박또박 남는다.
// 다만 굴림에는 KS X 1001 의 2350 자만 있다(흔한 이름·문구는 다 되지만 '똠·뷁' 류가 없다).
// 그런 글자가 한 자라도 섞이면 그 줄만 한글이 전부 든 quan7 로 되돌려 그린다 —
// 글자가 없으면 u8g2 는 그 자리를 비우고 지나가, 이름이 통째로 사라진다.
#define FONT_MAIN u8g2_font_gulim16_t_korean2
#define FONT_MAIN_PX 16
#define FONT_ALL u8g2_font_quan7_h_cjk  // Arduino_GFX 에 딸려 있다
#define FONT_ALL_PX 8

#define COL_GOLD RGB565(255, 200, 60)
#define COL_DIM RGB565(120, 120, 130)
#define TEXT_MARGIN 10  // 원 테두리에서 띄울 여백

// 글자 크기 — 240 화면이라 참고 기기(480)의 절반쯤으로 잡았다
#define PX_NAME 32
#define PX_MOOD 16
#define PX_NOTE 16
#define PX_POINTS 32
#define PX_LABEL 16
#define PX_BULK_TITLE 16
#define PX_BULK_NAME 16

static const uint32_t NFC_SHOW_MS = 6000;  // NFC 화면을 보여 준 뒤 대기로 돌아가는 시간

// ── 사진 ─────────────────────────────────────────────────────────
// 서버가 주는 그림은 TALENT_PX 정사각 RGB565(리틀엔디안)이고, 원 밖은 비침색이다.
// ESP32 도 리틀엔디안이라 받은 바이트를 그대로 화면 버퍼로 읽으면 된다.
#define TRANSPARENT_KEY 0xF81F   // 서버 talentArt.js 의 TRANSPARENT_KEY 와 같은 값(자홍)
#define PHOTO_SCALE (TALENT_PX / 240)  // 480 이면 2 (이웃 점 고르기로 줄인다), 240 이면 1

// NFC 화면 글자 자리 — 사진을 깔기 때문에 흐르지 않고 **고정**이다. 위(이름·기분)와
// 아래(안내문·잔액)에만 글자를 두고 가운데는 비워 둔다 — 얼굴이 가려지지 않게.
#define NAME_CY 46
#define MOOD_CY 74
#define NOTE_CY 168
#define POINTS_CY 198
#define UID_CY 222
// 글자가 사진 위에서 묻히지 않게 어둡게 깔 띠
#define BAND_TOP_Y0 26
#define BAND_TOP_Y1 90
#define BAND_BOT_Y0 154
#define BAND_BOT_Y1 234
#define BAND_KEEP 80  // 앞(사진)색 비중 0~255. 낮을수록 더 어둡다

// ── 이름표 (UID → 사진 파일 이름) ────────────────────────────────
// 사진 이름은 /feed 응답에 없고 /roster 에만 있다(서버 쪽 규칙 — /feed·/roster 응답을
// 키우지 않는다). 그래서 이름표를 한 번 받아 UID→img 를 들고 있다가, 태깅할 때
// 그 img 로 사진 한 장만 받는다. 이름표는 ROSTER_TTL_MS 마다 다시 받는다 —
// 아이가 사진을 바꾸면 img 이름이 달라져 그때 새 사진을 받게 된다.
#define ROSTER_UID_LEN 24
#define ROSTER_IMG_LEN 80  // "nm-<base64url(이름) 최대 64자>-480.565" 가 들어갈 크기
struct RosterRow {
  char uid[ROSTER_UID_LEN];
  char img[ROSTER_IMG_LEN];
};
static RosterRow *roster = nullptr;  // PSRAM 에 잡는다(400칸 = 약 41KB)
static int rosterCount = 0;
static bool rosterGotOnce = false;
static uint32_t rosterNextAt = 0;

// ── 사진 캐시 (PSRAM, 가장 오래 안 쓴 것부터 밀어낸다) ───────────
// 한 장이 240*240*2 = 115,200바이트다. 칸은 필요할 때 한 장씩 잡는다 —
// 아이가 열 명인데 6.9MB 를 미리 묶어 두지 않는다.
#define PHOTO_BYTES ((size_t)240 * 240 * 2)
#if PHOTO_CACHE_MAX > 0
struct PhotoCacheEntry {
  char img[ROSTER_IMG_LEN];  // 열쇠 — 사진 파일 이름(해시가 들어 있어 '있다 = 최신')
  uint16_t *px;              // PSRAM 의 240x240 RGB565 (비침색 그대로 담는다)
  uint32_t usedAt;           // 마지막으로 쓴 시각 — 밀어낼 칸을 고르는 데 쓴다
  bool valid;                // 끝까지 받은 것만 true. 받다 만 것은 내보내지 않는다
};
static PhotoCacheEntry photoCache[PHOTO_CACHE_MAX];
static int photoCacheSlots = 0;  // 지금까지 PSRAM 자리를 잡은 칸 수
static uint32_t photoHit = 0, photoMiss = 0;
#endif

// ── 일괄 지급·사용 ───────────────────────────────────────────────
// 관리 화면에서 여러 아이에게 한 번에 포인트를 줄 때, 한 아이씩 NFC 화면으로 올리면
// 화면이 아이 수만큼 빠르게 바뀌어 누가 받았는지 놓친다. 그래서 일괄은 한 화면에
// 제목 + 명단을 함께 띄운다 — {"bulk":{"headline":"…","names":[…]}}
#define BULK_NAMES_MAX 40  // 받을 이름 수 상한(서버도 같은 수로 자른다)
#define BULK_NAME_LEN 20   // 이름 한 칸의 최대 길이(널 포함)
#define BULK_ROWS_ON_SCREEN 5  // 240 화면에 글자로 들어가는 줄 수(나머지는 "외 N명")
static const uint32_t BULK_SHOW_BASE_MS = 4000;
static const uint32_t BULK_SHOW_PER_NAME_MS = 400;
static const uint32_t BULK_SHOW_MAX_MS = 15000;

// ── 하드웨어 ─────────────────────────────────────────────────────
static Arduino_DataBus *bus =
    new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED /* MISO 미사용 */);
static Arduino_GFX *panel = new Arduino_GC9A01(bus, TFT_RST, 0, true /* IPS */);
static Arduino_Canvas *gfx = new Arduino_Canvas(240, 240, panel);

// 디스플레이가 FSPI(SPI2)를 점유하므로 PN532 는 HSPI(SPI3)에 올린다
static SPIClass pnSpi(HSPI);
static Adafruit_PN532 nfc(PN532_CS, &pnSpi);

static WebSocketsServer webSocket(WS_PORT);
static WebSocketsClient hub;

static const int16_t CX = 120;
static const int16_t CY = 120;
static const int16_t R = 120;

// ── 상태 ─────────────────────────────────────────────────────────
enum Mode { MODE_IDLE, MODE_NFC, MODE_BULK };
static Mode mode = MODE_IDLE;
static uint32_t nfcUntil = 0;  // 이 시각이 지나면 NFC·일괄 화면을 접는다

static char nfcUid[32] = "";
static char nfcName[40] = "";
static long nfcPoints = 0;
static bool nfcKnown = false;
static char nfcNote[32] = "";
// 아이가 앱·조회 화면에 적어 둔 '지금 기분' 한 줄. 서버가 이모티콘을 한국어 이름으로
// 바꿔 보내 주므로(who.status.label) 여기 담기는 것은 그릴 수 있는 글자뿐이다 —
// 폰트에 이모티콘 글리프가 없어 😀 가 그대로 오면 네모(두부)가 뜬다.
// '기쁨 오늘 소풍 가요' 처럼 label 과 text 를 이어 둔 꼴이다. 비어 있으면 그 줄을 안 그린다.
static char nfcMood[160] = "";
// 이 화면에 깔 사진의 파일 이름(이름표에서 찾은 것). 비어 있으면 글자만 그린다.
static char nfcImg[ROSTER_IMG_LEN] = "";

static char bulkHeadline[48] = "";
static char bulkNames[BULK_NAMES_MAX][BULK_NAME_LEN];
static int bulkCount = 0;

// 리더·WebSocket·허브가 넘긴 UID 를 여기 담아 두고 loop 에서 처리한다.
// 콜백 안에서 서버를 부르지 않는 이유: 조회·지급은 수백 ms 가 걸리는 일인데, 그동안
// 콜백이 돌아오지 않으면 같은 소켓으로 들어온 다음 것이 밀려 라이브러리가 엉킨다.
static char pendingUid[32] = "";

static bool online = false;     // 화면에 그려 둔 연결 상태
static bool serversUp = false;  // mDNS·WebSocket 을 켰나(와이파이가 붙은 뒤 한 번만)
static bool hubBegun = false;
static bool hubUp = false;

// 서버 설정 — 못 받으면 아래 기본값으로 돈다(값은 서버 스키마와 같게 둘 것)
static uint8_t cfgBacklight = BL_LEVEL;
static bool cfgAttendMode = false;
static char cfgAttendCard[24] = "";
static uint32_t cfgNextAt = 0;
static bool cfgGotOnce = false;
// 관리 화면이 "지금 다시 받아라" 를 밀면 서는 깃발({"state":"config"} — deviceHub 의
// 'config'). 주기(CONFIG_TTL_MS · 5분)를 기다리지 않고 그 자리에서 받아 오려는 것이다 —
// 출석모드를 켜는 자리는 주일 아침 줄이 선 현장이라 5분이 "켰는데 안 켜진다" 로 보인다.
// ★ 받는 자리에서 바로 fetchConfig 를 부르지 않는다. 수백 ms 짜리 https 왕복이라
//   받는 콜백 위에서 돌면 그동안 다른 것을 못 받는다 — 태그 처리(pendingUid)와 같은
//   규율이다. 깃발만 세우고 푸는 것은 loop 가 한다. volatile 은 그래서 붙인다.
static volatile bool cfgWanted = false;

// 최근 출석한 키링 — 같은 아이가 잇달아 대도 한 번만 센다
struct AttendSeen {
  char uid[24];
  uint32_t at;
};
static AttendSeen attendSeen[ATTEND_RECENT_MAX];
static int attendSeenCount = 0;

// PN532 로 읽은 마지막 카드 — 올려둔 동안 서버를 반복해 찌르지 않으려고 기억한다
static uint8_t heldUid[7] = {0};
static uint8_t heldUidLen = 0;
static uint32_t heldAt = 0;
static uint32_t readCount = 0;

// 지금 기분 한 벌
struct Mood {
  char line[160];  // label + text 를 이어 둔 것(화면에 그릴 한 줄)
};

// ── 그림 자리 다툼 막기 ──────────────────────────────────────────
// 참고 기기는 RGB 패널이라 PSRAM 프레임버퍼를 아무 때나 찔러도 됐다. 이 보드는 SPI 로
// 한 장(115KB)을 밀어 넣는 방식이라, 고리를 돌리는 일꾼과 본 고리가 같이 밀면 화면이
// 깨진다. 그래서 화면 한 장을 그리는 동안은 자물쇠를 쥔다.
static SemaphoreHandle_t gfxLock;
#define GFX_LOCK() xSemaphoreTake(gfxLock, portMAX_DELAY)
#define GFX_UNLOCK() xSemaphoreGive(gfxLock)

// spinWant 는 고리 일꾼이 읽는다 — volatile 로 둬야 컴파일러가 가두지 않는다.
// spinDepth 는 본 고리에서만 세므로(spinStart·spinStop) volatile 이 아니다.
static volatile bool spinWant = false;
static int spinDepth = 0;

// ── 백라이트 ─────────────────────────────────────────────────────
// 서버가 주는 밝기는 1~16 이다. PWM 듀티로 옮긴다(1 이어도 보이게 바닥을 둔다).
static void backlightOn(uint8_t level) {
  if (level < 1) level = 1;
  if (level > 16) level = 16;
  const uint8_t duty = (uint8_t)(16 + (uint16_t)(level - 1) * (255 - 16) / 15);
  ledcWrite(TFT_BLK, duty);
}

// ── 글자 다루기 ──────────────────────────────────────────────────
// 정수를 천 단위 콤마로 (1200 → "1,200")
static void commafy(long v, char *out, size_t n) {
  char tmp[16];
  bool neg = v < 0;
  if (neg) v = -v;
  const int len = snprintf(tmp, sizeof(tmp), "%ld", v);
  size_t o = 0;
  if (neg && o < n - 1) out[o++] = '-';
  for (int i = 0; i < len && o < n - 1; i++) {
    if (i > 0 && (len - i) % 3 == 0 && o < n - 1) out[o++] = ',';
    out[o++] = tmp[i];
  }
  out[o] = '\0';
}

// 서버·브라우저에서 온 글자를 JSON 에 그대로 끼워 넣어도 안전하게 베낀다.
// 이 파일이 내보내는 상태 알림은 snprintf 로 조립한 JSON 이라(buildState) 값 안에 " 나 \
// 가 섞이면 그 자리에서 깨지고, 받는 쪽이 답을 통째로 못 알아본다. '지금 기분' 은 아이가
// 제 손으로 적은 글이라 그런 글자가 실제로 들어올 수 있다. 따옴표·역슬래시·제어문자는
// 떨어뜨린다(바꿔 끼우지 않고 버리는 쪽: 글자 수가 늘지 않아 버퍼 계산이 그대로 맞는다).
// 잘라야 할 때는 UTF-8 글자 경계에서 끊는다 — 한 글자를 반만 베끼면 깨진 네모가 뜬다.
static void copyJsonSafe(char *dst, size_t cap, const char *src) {
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)(src ? src : "");
  while (*p) {
    const unsigned char c = *p;
    if (c == '"' || c == '\\' || c < 0x20) {  // 버린다
      p++;
      continue;
    }
    int len = 1;
    if (c >= 0xF0) {
      len = 4;
    } else if (c >= 0xE0) {
      len = 3;
    } else if (c >= 0xC0) {
      len = 2;
    }
    if (o + len >= cap) break;  // 글자 경계에서 끊는다
    for (int i = 0; i < len && p[i]; i++) dst[o++] = (char)p[i];
    p += len;
  }
  dst[o] = '\0';
}

// UTF-8 한 글자를 유니코드 번호로 꺼내고, 다음 글자 자리를 돌려준다.
static const char *utf8Next(const char *s, uint16_t *cp) {
  const uint8_t c = (uint8_t)*s++;
  uint16_t v;
  int more;
  if (c < 0x80) {
    *cp = c;
    return s;
  } else if (c < 0xE0) {
    v = c & 0x1F;
    more = 1;
  } else if (c < 0xF0) {
    v = c & 0x0F;
    more = 2;
  } else {
    v = c & 0x07;
    more = 3;
  }
  while (more-- && ((uint8_t)*s & 0xC0) == 0x80) {
    v = (v << 6) | ((uint8_t)*s++ & 0x3F);
  }
  *cp = v;
  return s;
}

static uint16_t fontWord(const uint8_t *f, uint8_t off) {
  return ((uint16_t)f[off] << 8) | f[off + 1];
}

// u8g2 폰트 표에 이 글자가 있나 — Arduino_GFX 가 글자를 찾는 길을 그대로 따라간다
// (머리 23바이트 뒤로 1바이트짜리 글자들이 늘어서고, 그 뒤에 유니코드 찾기표가 있다).
static bool fontHasGlyph(const uint8_t *font, uint16_t cp) {
  const uint8_t *p = font + 23;
  if (cp <= 255) {
    for (;;) {
      if (p[1] == 0) return false;
      if (p[0] == cp) return true;
      p += p[1];
    }
  }
  const uint8_t *table = font + 23 + fontWord(font, 21);
  p = table;
  uint16_t last;
  do {
    p += fontWord(table, 0);
    last = fontWord(table, 2);
    table += 4;
  } while (last < cp);
  for (;;) {
    const uint16_t e = fontWord(p, 0);
    if (e == 0 || e > cp) return false;  // 번호순이라 지나쳤으면 없는 글자다
    if (e == cp) return true;
    p += p[2];
  }
}

// 이 줄을 굴림으로 다 그릴 수 있나(한 자라도 없으면 quan7 로 간다)
static bool mainFontCovers(const char *s) {
  uint16_t cp;
  while (*s) {
    s = utf8Next(s, &cp);
    if (!fontHasGlyph((const uint8_t *)FONT_MAIN, cp)) return false;
  }
  return true;
}

// ── 원형 화면에 글자 놓기 ────────────────────────────────────────
// 네모난 화면이면 240 을 그대로 쓰면 되지만, 지름 240 원이라 위아래로 갈수록 좁다.
static int roundWidthAt(int y0, int y1) {
  int dy = abs(y0 - CY);
  const int dy1 = abs(y1 - CY);
  if (dy1 > dy) dy = dy1;
  if (dy >= R) return 0;
  const int half = (int)sqrtf((float)(R * R - dy * dy));
  const int w = 2 * half - 2 * TEXT_MARGIN;
  return w > 0 ? w : 0;
}

// 이 자리에 놓을 폰트·배수를 고르고 그때의 크기를 재 둔다. 돌아올 때 폰트·배수는 맞춰져 있다.
static int fitTextScale(const char *s, int cy, int px, int16_t *x1, int16_t *y1, uint16_t *w,
                        uint16_t *h) {
  const bool useMain = mainFontCovers(s);
  const int unit = useMain ? FONT_MAIN_PX : FONT_ALL_PX;
  gfx->setFont(useMain ? (const uint8_t *)FONT_MAIN : (const uint8_t *)FONT_ALL);
  gfx->setUTF8Print(true);

  int scale = px / unit;
  if (scale < 1) scale = 1;
  for (;;) {
    gfx->setTextSize(scale);
    gfx->getTextBounds(s, 0, 0, x1, y1, w, h);
    if (scale <= 1) break;
    if ((int)*w <= roundWidthAt(cy - *h / 2, cy + *h / 2)) break;
    scale--;  // 원 안에 안 들어간다 — 한 단계 줄인다
  }
  return scale;
}

// 글자 한 줄을 가로 가운데, 세로 cy 중앙에 놓고 그린다.
//   px : 바라는 글자 높이. 원 안에 안 들어가면 배수를 한 단계씩 줄인다(긴 이름도 안 잘린다).
static void drawUTF8(const char *s, int cy, int px, uint16_t color) {
  if (!s || !s[0]) return;
  int16_t x1, y1;
  uint16_t w, h;
  fitTextScale(s, cy, px, &x1, &y1, &w, &h);
  gfx->setTextColor(color);
  gfx->setCursor(CX - w / 2 - x1, cy - h / 2 - y1);
  gfx->print(s);
}

// 배수 1 에서도 원 안에 안 들어가는 줄은 뒤를 떼고 '...' 를 붙인다. 기분은 서른 자까지
// 적을 수 있어 그냥 두면 양옆이 말없이 잘려 "뒤에 더 있다" 가 안 읽힌다.
// ★ '…'(U+2026)도 '·'(U+00B7)도 굴림16 에 없다 — 쓰면 그 한 자 때문에 줄 전체가
//   quan7(8px)로 되돌려 그려져 글자가 확 작아진다. 그래서 ASCII 마침표 셋만 쓴다.
static void fitEllipsis(char *s, size_t cap, int cy) {
  if (!s || !s[0] || cap < 8) return;
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setFont(mainFontCovers(s) ? (const uint8_t *)FONT_MAIN : (const uint8_t *)FONT_ALL);
  gfx->setUTF8Print(true);
  gfx->setTextSize(1);
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  const int avail = roundWidthAt(cy - h / 2, cy + h / 2);
  if ((int)w <= avail) return;

  size_t len = strlen(s);
  while (len > 0) {
    do {
      len--;
    } while (len > 0 && ((uint8_t)s[len] & 0xC0) == 0x80);  // UTF-8 경계까지 한 글자 떼기
    char tmp[192];
    snprintf(tmp, sizeof(tmp), "%.*s...", (int)len, s);
    gfx->getTextBounds(tmp, 0, 0, &x1, &y1, &w, &h);
    if ((int)w <= avail) {
      strlcpy(s, tmp, cap);
      return;
    }
  }
}

// 아스키 작은 글씨(UID·상태 줄)는 기본 폰트로 — 한글 폰트로 바꿔 둔 상태를 되돌린다
static void drawAscii(const char *s, int cy, uint8_t size, uint16_t color) {
  gfx->setFont((const GFXfont *)nullptr);
  gfx->setUTF8Print(false);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(CX - (int16_t)(strlen(s) * 3 * size), cy - 4 * size);
  gfx->print(s);
}

// 두 색을 섞는다(a = 앞색 비중 0~255). 띠를 반투명 검정처럼 덮는 데 쓴다.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  const uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
  const uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
  const uint16_t r = (r1 * a + r2 * (255 - a)) / 255;
  const uint16_t g = (g1 * a + g2 * (255 - a)) / 255;
  const uint16_t b = (b1 * a + b2 * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}

// 화면 버퍼의 한 가로 띠를 어둡게 만든다 — 글자가 사진 위에서 잘 보이게.
static void darkenBand(int y0, int y1, uint8_t keep) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return;
  if (y0 < 0) y0 = 0;
  if (y1 > 240) y1 = 240;
  for (int y = y0; y < y1; y++) {
    uint16_t *row = fb + (int32_t)y * 240;
    for (int x = 0; x < 240; x++) row[x] = blend565(row[x], 0x0000, keep);
  }
}

// 이름표에서 이 UID 의 사진 이름을 찾는다. 서버는 UID 를 대문자로 다듬어 두지만
// (normUid), 들고 있는 값이 어떻든 맞도록 대소문자를 가리지 않고 견준다.
static const char *rosterImgFor(const char *uid) {
  if (!roster || !uid || !uid[0]) return "";
  for (int i = 0; i < rosterCount; i++) {
    if (!strcasecmp(roster[i].uid, uid)) return roster[i].img;
  }
  return "";
}

// 캐시(또는 막 받은) 사진을 화면 버퍼에 깐다. 비침색인 점은 건너뛴다 — 그 자리는
// 바탕(검정)이 그대로 남아 둥근 테두리 밖이 지저분해지지 않는다.
// 둘 다 240x240 한 줄씩 나란한 꼴이라 번호를 그대로 견준다.
static void blitPhoto(const uint16_t *src, int rows) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb || !src) return;
  if (rows > 240) rows = 240;
  const int32_t n = (int32_t)rows * 240;
  for (int32_t i = 0; i < n; i++) {
    const uint16_t c = src[i];
    if (c != TRANSPARENT_KEY) fb[i] = c;
  }
}

#if PHOTO_CACHE_MAX > 0
// 이 사진이 캐시에 온전히 들어 있나 — 있으면 쓴 시각을 갱신해 돌려준다.
static uint16_t *photoCacheGet(const char *img) {
  for (int i = 0; i < photoCacheSlots; i++) {
    if (photoCache[i].valid && !strcmp(photoCache[i].img, img)) {
      photoCache[i].usedAt = millis();
      return photoCache[i].px;
    }
  }
  return nullptr;
}

// 이 사진을 담을 칸을 얻는다. 빈 칸이 있으면 PSRAM 을 새로 잡고, 다 찼으면 가장
// 오래 안 쓴 칸을 밀어낸다. 돌려준 칸은 아직 valid 가 아니다 — 끝까지 받은 뒤에 세운다.
// 자리를 못 잡으면 nullptr(그때는 캐시 없이 화면에 바로 그린다).
static uint16_t *photoCacheClaim(const char *img) {
  int slot = -1;

  for (int i = 0; i < photoCacheSlots; i++) {  // 같은 사진 칸이 있으면 그 자리를 다시 쓴다
    if (!strcmp(photoCache[i].img, img)) {
      slot = i;
      break;
    }
  }

  if (slot < 0 && photoCacheSlots < PHOTO_CACHE_MAX) {  // 새 칸 — PSRAM 에서 한 장 잡는다
    uint16_t *mem = (uint16_t *)ps_malloc(PHOTO_BYTES);
    if (mem) {
      slot = photoCacheSlots++;
      photoCache[slot].px = mem;
    }
  }

  if (slot < 0) {  // 다 찼다 — 가장 오래 안 쓴 칸을 밀어낸다
    if (!photoCacheSlots) return nullptr;
    slot = 0;
    for (int i = 1; i < photoCacheSlots; i++) {
      // millis() 는 49일쯤에 한 바퀴 돈다 — 뺀 값의 부호로 견줘야 그때도 맞다
      if ((int32_t)(photoCache[i].usedAt - photoCache[slot].usedAt) < 0) slot = i;
    }
    Serial.printf("[사진] 캐시가 찼다 — %s 를 밀어낸다\n", photoCache[slot].img);
  }

  strlcpy(photoCache[slot].img, img, ROSTER_IMG_LEN);
  photoCache[slot].usedAt = millis();
  photoCache[slot].valid = false;
  return photoCache[slot].px;
}

// 끝까지 받았다 — 이제부터 이 칸을 내보낸다.
static void photoCacheCommit(const uint16_t *px) {
  for (int i = 0; i < photoCacheSlots; i++) {
    if (photoCache[i].px == px) {
      photoCache[i].valid = true;
      photoCache[i].usedAt = millis();
      return;
    }
  }
}

// 받다 만 칸을 못 쓰게 만든다 — 다음에 그 사진을 달라고 하면 다시 받는다.
static void photoCacheDrop(const uint16_t *px) {
  for (int i = 0; i < photoCacheSlots; i++) {
    if (photoCache[i].px == px) {
      photoCache[i].valid = false;
      photoCache[i].img[0] = '\0';
      return;
    }
  }
}

static void photoCacheClear() {
  for (int i = 0; i < photoCacheSlots; i++) {
    photoCache[i].valid = false;
    photoCache[i].img[0] = '\0';
  }
  photoHit = photoMiss = 0;
  Serial.printf("[사진] 캐시를 비웠다 (자리 %d칸은 그대로 들고 있는다)\n", photoCacheSlots);
}

static void photoCacheReport() {
  int live = 0;
  for (int i = 0; i < photoCacheSlots; i++) {
    if (photoCache[i].valid) live++;
  }
  Serial.printf("[사진] 캐시 %d/%d장 · 자리 %d칸(%u KB) · 적중 %lu · 빗나감 %lu · 남은 PSRAM %lu KB\n",
                live, PHOTO_CACHE_MAX, photoCacheSlots,
                (unsigned)((size_t)photoCacheSlots * PHOTO_BYTES / 1024), (unsigned long)photoHit,
                (unsigned long)photoMiss, (unsigned long)(ESP.getFreePsram() / 1024));
  const uint32_t nowMs = millis();
  for (int i = 0; i < photoCacheSlots; i++) {
    if (!photoCache[i].valid) continue;
    Serial.printf("  %-40s %lu초 전\n", photoCache[i].img,
                  (unsigned long)((nowMs - photoCache[i].usedAt) / 1000));
  }
}
#endif

// 사진 받아 깔기는 서버와 말하는 쪽(beginHttp·Spinner)이 아래에 있어 거기에 둔다.
static bool drawPhoto(const char *img);

static void drawRing(uint16_t color) {
  gfx->drawCircle(CX, CY, R - 1, color);
  gfx->drawCircle(CX, CY, R - 2, color);
}

// ── 불러오는 중 고리 ─────────────────────────────────────────────
// 서버에 무엇을 물을 때(조회·출석·설정) 화면 테두리를 금색 고리가 돈다. 따로 띄운
// 일꾼이 그린다 — 서버 왕복이 loop 를 붙잡고 있는 동안에도 돌아간다.
// 캔버스에 이미 그려 둔 화면 위에 고리만 덧그리고 밀어 넣는다(자물쇠로 본 고리와 번갈아).
static void spinnerTask(void *) {
  int deg = 0;
  for (;;) {
    if (!spinWant) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    GFX_LOCK();
    // 자물쇠를 기다리는 동안 고리가 멈췄을 수 있다 — 그러면 그리지 않는다.
    // (안 보면 본 고리가 마지막으로 그린 화면 위에 고리가 남는다)
    if (!spinWant) {
      GFX_UNLOCK();
      continue;
    }
    gfx->fillArc(CX, CY, R - 2, R - 7, 0, 360, RGB565_BLACK);  // 지난 자리 지우기
    gfx->fillArc(CX, CY, R - 2, R - 7, deg, deg + 70, COL_GOLD);
    gfx->flush();
    GFX_UNLOCK();
    deg = (deg + 15) % 360;
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

static void spinStart() {
  spinDepth++;
  spinWant = true;
}

static void spinStop() {
  if (spinDepth > 0) spinDepth--;
  if (spinDepth == 0) spinWant = false;
}

// 서버를 부르는 함수들이 들고 다니는 고리. 함수가 어디로 돌아가도 반드시 멈춘다.
struct Spinner {
  Spinner() { spinStart(); }
  ~Spinner() { spinStop(); }
};

// ── 화면들 ───────────────────────────────────────────────────────
// 연결 상태 한 줄 — 와이파이·허브·출석모드를 한눈에
static void statusLine(char *out, size_t cap) {
  snprintf(out, cap, "%s %s %s", online ? "wifi" : "WIFI-", hubUp ? "hub" : "hub-",
           cfgAttendMode ? (cfgAttendCard[0] ? "attend" : "attend?") : "");
}

static void drawIdle() {
  const float angle = fmodf(millis() / 1500.0f, 1.0f) * 360.0f;

  GFX_LOCK();
  gfx->fillScreen(RGB565_BLACK);
  drawRing(RGB565_DARKSLATEGREY);
  gfx->fillArc(CX, CY, R - 8, R - 14, angle, angle + 70, RGB565_CYAN);

  drawUTF8("카드를 올려 주세요", 104, PX_NAME, RGB565_WHITE);

  char st[48];
  statusLine(st, sizeof(st));
  drawAscii(st, 142, 1, COL_DIM);

  char cnt[24];
  snprintf(cnt, sizeof(cnt), "reads %lu", (unsigned long)readCount);
  drawAscii(cnt, 162, 1, RGB565_DARKGREY);
  gfx->flush();
  GFX_UNLOCK();
}

// NFC 화면 — 사진을 깔고 그 위에 이름·기분·안내문·잔액을 얹는다.
// 글자 자리가 고정인 까닭: 사진을 가리지 않게 위·아래에만 두고 가운데(얼굴)를 비워 둔다.
// 사진을 받는 데 1초쯤 걸려 그동안 고리가 돌아야 하므로, 자물쇠는 **마지막에만** 쥔다
// (화면 버퍼에 점을 쓰는 일끼리는 겹치지 않는다 — 부딪히는 것은 패널로 밀어 넣는 쪽이다).
static void drawNfc() {
  gfx->fillScreen(RGB565_BLACK);
  const bool hasPhoto = drawPhoto(nfcImg);

  if (hasPhoto) {
    darkenBand(BAND_TOP_Y0, BAND_TOP_Y1, BAND_KEEP);
    darkenBand(BAND_BOT_Y0, BAND_BOT_Y1, BAND_KEEP);
  }

  GFX_LOCK();
  // 사진이 없으면 테두리 안쪽을 채워 빈 화면이 덜 허전하게 둔다
  if (!hasPhoto) {
    gfx->fillArc(CX, CY, R - 8, R - 14, 0, 360,
                 nfcKnown ? RGB565_DARKGREEN : RGB565_DARKORANGE);
  }
  drawRing(nfcKnown ? RGB565_FORESTGREEN : COL_GOLD);

  drawUTF8(nfcName[0] ? nfcName : nfcUid, NAME_CY, PX_NAME, RGB565_WHITE);

  // 기분은 이름 아래. 적어 둔 것이 없으면 그 줄을 그리지 않는다.
  if (nfcMood[0]) {
    char mood[160];
    strlcpy(mood, nfcMood, sizeof(mood));
    fitEllipsis(mood, sizeof(mood), MOOD_CY);
    drawUTF8(mood, MOOD_CY, PX_MOOD, COL_GOLD);
  }

  // 안내문 — '출석 완료 +5P' · '이미 출석했어요' · '등록되지 않은 키링' 등
  drawUTF8(nfcNote[0] ? nfcNote : "잔여 포인트", NOTE_CY, PX_NOTE,
           nfcNote[0] ? RGB565_CYAN : COL_DIM);

  char pts[24], line[32];
  commafy(nfcPoints, pts, sizeof(pts));
  snprintf(line, sizeof(line), "%s P", pts);
  drawUTF8(line, POINTS_CY, PX_POINTS, COL_GOLD);

  drawAscii(nfcUid, UID_CY, 1, RGB565_DARKGREY);
  gfx->flush();
  GFX_UNLOCK();
}

// 일괄 화면 — 제목 + 명단. 240 에는 몇 줄밖에 안 들어가니 나머지는 "외 N명" 으로.
static void drawBulk() {
  GFX_LOCK();
  gfx->fillScreen(RGB565_BLACK);
  drawRing(COL_GOLD);

  drawUTF8(bulkHeadline, 54, PX_BULK_TITLE, COL_GOLD);

  const int shown = bulkCount < BULK_ROWS_ON_SCREEN ? bulkCount : BULK_ROWS_ON_SCREEN;
  const int lineH = 22;
  const int firstY = 86;
  for (int i = 0; i < shown; i++) {
    drawUTF8(bulkNames[i], firstY + i * lineH, PX_BULK_NAME, RGB565_WHITE);
  }
  if (bulkCount > shown) {
    char more[24];
    snprintf(more, sizeof(more), "외 %d명", bulkCount - shown);
    drawUTF8(more, firstY + shown * lineH, PX_BULK_NAME, COL_GOLD);
  }
  gfx->flush();
  GFX_UNLOCK();
}

// 한 줄 알림 (동기화·오류 등)
static void drawStatus(const char *msg) {
  GFX_LOCK();
  gfx->fillScreen(RGB565_BLACK);
  drawRing(COL_DIM);
  char line[96];
  strlcpy(line, msg, sizeof(line));
  fitEllipsis(line, sizeof(line), CY);
  drawUTF8(line, CY, PX_NOTE, RGB565_WHITE);
  gfx->flush();
  GFX_UNLOCK();
}

static void drawFatal(const char *line1, const char *line2) {
  GFX_LOCK();
  gfx->fillScreen(RGB565_BLACK);
  drawRing(RGB565_RED);
  drawUTF8(line1, 100, 24, RGB565_RED);
  drawUTF8(line2, 132, PX_NOTE, COL_DIM);
  gfx->flush();
  GFX_UNLOCK();
}

// ── 상태 알림 (붙은 쪽에게) ──────────────────────────────────────
// `attend` 는 **이 기기가 실제로 들고 있는** 출석모드다 — 관리 화면의 스위치는
// 서버에 저장된 값이고, 이것은 기기가 받아 간 값이다. 둘을 나란히 두어야 "켰는데
// 이 기기만 아직 꺼져 있다"(설정을 못 받았다 · 기기 전용 값이 따로 걸려 있다)가 보인다.
// 켜도 카드를 고르지 않았으면 지급하지 않으므로, 그 상태는 켜짐으로 세지 않는다.
static void buildState(char *msg, size_t cap) {
  const char *attend = (cfgAttendMode && cfgAttendCard[0]) ? "true" : "false";
  if (mode == MODE_NFC) {
    snprintf(msg, cap,
             "{\"mode\":\"nfc\",\"uid\":\"%s\",\"name\":\"%s\",\"points\":%ld,"
             "\"known\":%s,\"note\":\"%s\",\"mood\":\"%s\",\"attend\":%s,\"online\":%s}",
             nfcUid, nfcName, nfcPoints, nfcKnown ? "true" : "false", nfcNote, nfcMood,
             attend, online ? "true" : "false");
  } else if (mode == MODE_BULK) {
    snprintf(msg, cap,
             "{\"mode\":\"bulk\",\"headline\":\"%s\",\"count\":%d,\"attend\":%s,\"online\":%s}",
             bulkHeadline, bulkCount, attend, online ? "true" : "false");
  } else {
    snprintf(msg, cap, "{\"mode\":\"idle\",\"attend\":%s,\"online\":%s}",
             attend, online ? "true" : "false");
  }
}

// only < 0 이면 붙은 모두에게, only >= 0 이면 그 ws client 한 곳에만.
// 버퍼는 nfcMood(160) 가 통째로 실릴 자리를 둔다 — 모자라면 snprintf 가 뒤를 잘라
// JSON 이 깨지고, 받는 쪽이 상태를 통째로 못 읽는다.
static void sendState(int8_t only = -1) {
  char msg[640];   // `attend` 가 붙으며 여유가 줄었다 — 넘치면 snprintf 가 잘라 JSON 이 깨진다
  buildState(msg, sizeof(msg));
  if (only >= 0) {
    webSocket.sendTXT((uint8_t)only, msg);
    return;
  }
  webSocket.broadcastTXT(msg);
  // 붙어 있으면 서버에도 같은 글자를 보낸다. 관리 화면의 '기기' 표시가 이것을 받아
  // "지금 무엇을 띄우고 있나" 를 보여 준다.
  if (hubUp) hub.sendTXT(msg);
}

static void goIdle(bool announce = true) {
  mode = MODE_IDLE;
  drawIdle();
  if (announce) sendState();
}

static void showNfc(const char *uid, const char *name, long points, bool known,
                    const char *note = "", const Mood *mood = nullptr) {
  copyJsonSafe(nfcUid, sizeof(nfcUid), uid);
  // 이름·안내문도 copyJsonSafe 로 받는다 — 관리 화면(브라우저)이 직접 보내 올 수 있어
  // 이름에 따옴표가 섞이면 상태 알림의 JSON 이 그 자리에서 깨진다. 글자 경계에서
  // 자르는 것도 함께 얻는다(strncpy 는 한글 한 자를 반만 베낀다).
  copyJsonSafe(nfcName, sizeof(nfcName), (name && name[0]) ? name : uid);
  copyJsonSafe(nfcNote, sizeof(nfcNote), note);
  copyJsonSafe(nfcMood, sizeof(nfcMood), mood ? mood->line : "");
  nfcPoints = points;
  nfcKnown = known;
  // 이 아이의 사진 이름. 이름표에 없으면(미등록 카드이거나 이름표를 못 받았다) 빈 값이고
  // 그때는 글자만 그린다.
  strlcpy(nfcImg, rosterImgFor(nfcUid), sizeof(nfcImg));

  mode = MODE_NFC;
  nfcUntil = millis() + NFC_SHOW_MS;
  drawNfc();
  sendState();
}

// 서버가 보낸 일괄 지급·사용 알림. 보여 줄 이름이 하나도 없으면 아무 일도 하지 않는다.
static void showBulk(const char *headline, JsonVariantConst names) {
  copyJsonSafe(bulkHeadline, sizeof(bulkHeadline), headline);
  bulkCount = 0;
  if (names.is<JsonArrayConst>()) {
    for (JsonVariantConst v : names.as<JsonArrayConst>()) {
      if (bulkCount >= BULK_NAMES_MAX) break;
      const char *s = v.as<const char *>();
      if (!s || !s[0]) continue;
      copyJsonSafe(bulkNames[bulkCount], BULK_NAME_LEN, s);
      bulkCount++;
    }
  }
  if (!bulkCount) return;

  mode = MODE_BULK;
  uint32_t dur = BULK_SHOW_BASE_MS + (uint32_t)bulkCount * BULK_SHOW_PER_NAME_MS;
  if (dur > BULK_SHOW_MAX_MS) dur = BULK_SHOW_MAX_MS;
  nfcUntil = millis() + dur;
  drawBulk();
  sendState();
}

// 시험 화면 — 카드도 서버도 없이 NFC 화면을 띄운다(글자 크기·자리를 눈으로 보려고).
// 시리얼 't' 를 누를 때마다 다음 보기로 넘어가고, 한 번 띄우면 1분 동안 그대로 둔다.
static void showTestScreen() {
  static const struct {
    const char *name;
    long points;
    const char *note;
    const char *mood;
  } SAMPLES[] = {
      {"홍길동", 1205, "출석 완료 +5P", "기쁨 오늘 소풍 가요"},
      {"김영민", 0, "", ""},
      {"박하늘별님구름햇님보다사랑스러우리", 12345, "", "신남 생일이에요"},
      {"이서준", 50, "이미 출석했어요", "피곤 어제 늦게 잤어요 그래서 조금 졸려요"},
      {"", 0, "등록되지 않은 키링", ""},
  };
  static int idx = 0;

  const auto &s = SAMPLES[idx];
  Serial.printf("[시험] %d/%d  %s · %ld P · %s · %s\n", idx + 1,
                (int)(sizeof(SAMPLES) / sizeof(SAMPLES[0])), s.name, s.points, s.note, s.mood);
  Mood m;
  strlcpy(m.line, s.mood, sizeof(m.line));
  showNfc("TEST0001", s.name, s.points, s.name[0] != '\0', s.note, &m);
  nfcImg[0] = '\0';  // 시험 화면은 사진 없이 — 글자 자리만 본다
  nfcUntil = millis() + 60000;  // 6초면 들여다볼 새가 없다
  idx = (idx + 1) % (int)(sizeof(SAMPLES) / sizeof(SAMPLES[0]));
}

// ── 서버 ─────────────────────────────────────────────────────────
// HTTPClient 를 열어 x-talent-key 를 실어 보낸다. https 면 인증서 검사는 건너뛴다.
static void beginHttp(HTTPClient &http, WiFiClientSecure &scli, WiFiClient &cli, const char *url) {
  if (strncmp(TALENT_SERVER, "https", 5) == 0) {
    scli.setInsecure();
    http.begin(scli, url);
  } else {
    http.begin(cli, url);
  }
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);
}

// ── 사진 한 장 받아 화면 버퍼에 깔기 ────────────────────────────
// 원본은 TALENT_PX 정사각이고 원 밖은 비침색이다 — 그 점은 건너뛴다(바탕이 그대로 남는다).
// 줄일 때는 **이웃 점 고르기**(평균 아님)를 쓴다. 평균을 내면 원 테두리에서 비침색(자홍)이
// 진짜 색과 섞여 얼굴 둘레에 자홍 테가 생긴다.
//
// 한 줄씩 흘려 읽는다 — 480 짜리 한 장은 460KB 라 통째로 받아 둘 자리가 없다(heap 은
// 250KB 남짓이다). 한 줄(960바이트)만 들고 바로 화면 버퍼에 옮긴다.
static bool drawPhoto(const char *img) {
#if !PHOTO_ENABLE
  return false;
#else
  if (!img || !img[0]) return false;
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return false;

#if PHOTO_CACHE_MAX > 0
  // 1) 들고 있는 사진이면 내려받지 않는다 — 와이파이가 끊겨 있어도 뜬다.
  const uint16_t *hit = photoCacheGet(img);
  if (hit) {
    photoHit++;
    blitPhoto(hit, 240);
    return true;
  }
  photoMiss++;
#endif

  if (WiFi.status() != WL_CONNECTED) return false;

  Spinner spin;  // 받는 동안(1초쯤) 테두리에 금색 고리가 돈다

  char url[220];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, img);

  HTTPClient http;
  WiFiClientSecure scli;
  WiFiClient cli;
  beginHttp(http, scli, cli, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  const uint32_t t0 = millis();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[사진] 실패 code=%d (%s)\n", code, img);
    return false;
  }

  WiFiClient *st = http.getStreamPtr();
  const int expect = http.getSize();  // Content-Length (모르면 -1)

  // 받을 길이가 TALENT_PX 와 안 맞으면 바로 접는다. 가장 흔한 원인은 **서버가 그 크기를
  // 모르는 것**이다 — DEVICE_SIZES 에 없는 px 를 달라고 하면 서버가 기본값(48)로 되돌려
  // 이름표에 ph-…-48.565 를 실어 주므로, 여기서 4,608바이트가 온다. 그대로 읽으면
  // 화면에 잡음만 깔린다. (서버에 240 을 넣었으면 TALENT_PX 를 240 으로 바꿀 것)
  const int want = TALENT_PX * TALENT_PX * 2;
  if (expect > 0 && expect != want) {
    http.end();
    Serial.printf("[사진] 크기가 다르다 — 받을 길이 %d바이트, 기대 %d바이트(px %d). "
                  "서버 DEVICE_SIZES 에 %d 이 있는지 확인하세요\n",
                  expect, want, TALENT_PX, TALENT_PX);
    return false;
  }

  // 한 줄을 다 모을 때까지 기다려 가며 읽는다.
  // ★ readBytes() 의 시간 끊기에만 맡기면 안 된다 — TLS 버퍼가 잠깐 비는 순간
  //   0 이 돌아와 "끊겼다" 로 잘못 읽는다(15KB 쯤에서 멈추는 것을 실제로 봤다).
  //   available() 이 0 이어도 연결이 살아 있으면 아직 오는 중이다.
  auto fill = [&](uint8_t *dst, size_t need) -> bool {
    size_t got = 0;
    uint32_t idleFrom = millis();
    while (got < need) {
      if (st->available() > 0) {
        const int n = st->read(dst + got, need - got);
        if (n > 0) {
          got += (size_t)n;
          idleFrom = millis();
          continue;
        }
      }
      if (!st->connected() && st->available() == 0) return false;  // 정말 끝났다
      if (millis() - idleFrom > HTTP_TIMEOUT_MS) return false;     // 너무 오래 조용하다
      delay(1);  // 쉬는 동안 고리 일꾼이 돈다
    }
    return true;
  };

  // 2) 받을 자리를 캐시에서 얻는다. 못 얻으면(캐시를 껐거나 PSRAM 이 모자라다)
  //    예전처럼 화면 버퍼에 바로 그린다 — 캐시만 못 쓰고 동작은 그대로다.
  uint16_t *slot = nullptr;
#if PHOTO_CACHE_MAX > 0
  slot = photoCacheClaim(img);
#endif

  static uint16_t rowBuf[TALENT_PX];  // 원본 한 줄 (240이면 480바이트)
  int rows = 0;
  for (int sy = 0; sy < TALENT_PX; sy++) {
    if (!fill((uint8_t *)rowBuf, sizeof(rowBuf))) {
      http.end();
      Serial.printf("[사진] 중간에 끊김 (%d/%d줄, 기대 %d바이트, %lums)\n", sy, TALENT_PX, expect,
                    (unsigned long)(millis() - t0));
      // 받은 줄까지는 보여 주지만, 캐시 칸은 못 쓰게 한다 — 반쪽 사진을
      // 들고 있다가 다음 태깅에 그대로 내보내면 그 아이는 계속 반쪽만 본다.
      if (slot) {
        blitPhoto(slot, rows);
        photoCacheDrop(slot);
      }
      return rows > 0;
    }
    if (sy % PHOTO_SCALE) continue;  // 건너뛰는 줄
    const int dy = sy / PHOTO_SCALE;
    if (dy >= 240) break;
    if (slot) {
      // 캐시에는 **날것 그대로**(비침색까지) 담는다 — 깔 때 건너뛰면 되고,
      // 검정으로 바꿔 담으면 나중에 사진을 무언가 위에 얹을 길이 막힌다.
      uint16_t *out = slot + (int32_t)dy * 240;
      for (int dx = 0; dx < 240; dx++) out[dx] = rowBuf[dx * PHOTO_SCALE];
    } else {
      uint16_t *out = fb + (int32_t)dy * 240;
      for (int dx = 0; dx < 240; dx++) {
        const uint16_t c = rowBuf[dx * PHOTO_SCALE];
        if (c != TRANSPARENT_KEY) out[dx] = c;
      }
    }
    rows++;
  }
  http.end();

  if (slot) {
#if PHOTO_CACHE_MAX > 0
    // 끝까지 받은 것만 다음에 내보낸다(240줄을 다 채웠을 때만 온전하다고 센다)
    if (rows >= 240) {
      photoCacheCommit(slot);
    } else {
      photoCacheDrop(slot);
    }
#endif
    blitPhoto(slot, rows);
  }
  Serial.printf("[사진] %s 깔았다 (%d줄, %d바이트, %lums)\n", img, rows, expect,
                (unsigned long)(millis() - t0));
  return rows > 0;
#endif
}

// TALENT_SERVER("https://host[:port]") 에서 호스트·포트·ssl 을 떼어 낸다.
static bool hubTarget(char *host, size_t cap, uint16_t *port, bool *ssl) {
  const char *u = TALENT_SERVER;
  *ssl = (strncmp(u, "https://", 8) == 0);
  *port = *ssl ? 443 : 80;
  const char *p = strstr(u, "://");
  p = p ? p + 3 : u;
  size_t i = 0;
  while (*p && *p != '/' && *p != ':' && i < cap - 1) host[i++] = *p++;
  host[i] = '\0';
  if (*p == ':') *port = (uint16_t)atoi(p + 1);
  return i > 0;
}

// 응답의 status 는 { emoji, label, text }. **label 만 쓴다** — emoji 는 😀 같은 날글자라
// 이 폰트에 글리프가 없어 네모(두부)로 뜨고, label 이 그것의 한국어 이름이다.
// 둘을 '기쁨 오늘 소풍 가요' 처럼 한 줄로 잇는다. status 가 없으면 빈 문자열.
static void moodFromJson(JsonVariantConst status, Mood *m) {
  m->line[0] = '\0';
  if (status.isNull()) return;
  const char *label = status["label"] | "";
  const char *text = status["text"] | "";
  snprintf(m->line, sizeof(m->line), "%s%s%s", label, (label[0] && text[0]) ? " " : "", text);
}

// ── 서버 설정 받기 (리더 설정 → v2.0) ────────────────────────────
// GET /api/talent/config/v2?device=<이 기기> → { config: { backlight, attendanceMode, attendanceCard } }
// 못 받으면 아무것도 바꾸지 않는다 — 들고 있던 값(또는 펌웨어 기본값)으로 계속 돈다.
// 와이파이가 흔들릴 때마다 밝기가 튀거나 출석모드가 꺼지면 현장에서 더 혼란스럽다.
static void fetchConfig() {
  Spinner spin;
  cfgNextAt = millis() + CONFIG_TTL_MS;
  if (WiFi.status() != WL_CONNECTED) return;

  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/config/v2?device=%s", TALENT_SERVER, TALENT_DEVICE_ID);

  HTTPClient http;
  WiFiClientSecure scli;
  WiFiClient cli;
  beginHttp(http, scli, cli, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[설정] 받기 실패 code=%d\n", code);
    return;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println("[설정] 해석 실패");
    return;
  }
  JsonObject c = doc["config"].as<JsonObject>();
  if (c.isNull()) {
    Serial.println("[설정] config 가 없다");
    return;
  }

  const uint8_t bl = (uint8_t)(c["backlight"] | (int)cfgBacklight);
  const bool wasAttending = cfgAttendMode && cfgAttendCard[0];
  cfgAttendMode = c["attendanceMode"] | false;
  strlcpy(cfgAttendCard, c["attendanceCard"] | "", sizeof(cfgAttendCard));

  if (bl >= 1 && bl <= 16 && bl != cfgBacklight) {  // 바뀔 때만 흔든다
    cfgBacklight = bl;
    backlightOn(cfgBacklight);
  }
  cfgGotOnce = true;
  // 출석모드가 실제로 바뀌었으면 붙어 있는 쪽(서버·브라우저)에 바로 알린다 —
  // 관리 화면의 스위치를 누른 사람이 "이 기기가 받았구나" 를 그 자리에서 본다.
  // 화면이 바뀐 것이 아니므로 바뀌지 않았을 때는 보내지 않는다(태깅마다 오는 것과
  // 달리 이 알림은 드물어야 한다).
  if (wasAttending != (cfgAttendMode && cfgAttendCard[0])) sendState();
  Serial.printf("[설정] 밝기 %d/16 · 출석모드 %s%s%s\n", cfgBacklight,
                cfgAttendMode ? "켜짐" : "꺼짐", cfgAttendMode ? " · 카드 " : "",
                cfgAttendMode ? (cfgAttendCard[0] ? cfgAttendCard : "없음(지급 안 함)") : "");
}

// ── 이름표 받기 (UID → 사진 파일 이름) ──────────────────────────
// GET /api/talent/roster?px=<TALENT_PX> → { data: [ { uid, sid, name, img } ] }
// img 는 그 아이의 기기용 그림 이름이다 — 사진이 있으면 ph-…, 없으면 이름 그림 nm-…
// (480 에 한해). 사진 자체는 태깅할 때 한 장만 받는다(drawPhoto).
//
// 이름표는 수백 줄이라 통째로 담으면 heap 이 모자란다. **uid·img 만 남기는 체를 걸어**
// 흘려 읽는다 — name·sid 는 쓰지 않으므로 받는 즉시 버린다.
static bool fetchRoster() {
#if !PHOTO_ENABLE
  return false;
#else
  Spinner spin;
  rosterNextAt = millis() + ROSTER_TTL_MS;
  if (WiFi.status() != WL_CONNECTED || !roster) return false;

  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/roster?px=%d", TALENT_SERVER, TALENT_PX);

  HTTPClient http;
  WiFiClientSecure scli;
  WiFiClient cli;
  beginHttp(http, scli, cli, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  const uint32_t t0 = millis();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[이름표] 받기 실패 code=%d\n", code);
    return false;
  }

  JsonDocument filter;
  filter["data"][0]["uid"] = true;
  filter["data"][0]["img"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, *http.getStreamPtr(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[이름표] 해석 실패 %s\n", err.c_str());
    return false;
  }

  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) {
    Serial.println("[이름표] data 가 없다");
    return false;
  }

  // 받아 낸 뒤에 갈아 끼운다 — 중간에 실패해도 들고 있던 이름표가 남아 있게.
  int n = 0;
  bool whole = true;
  for (JsonObjectConst r : data) {
    const char *uid = r["uid"] | "";
    const char *img = r["img"] | "";
    if (!uid[0] || !img[0]) continue;  // 사진도 이름 그림도 없는 아이는 담지 않는다
    if (n >= MAX_ROSTER) {
      whole = false;
      break;
    }
    if (strlen(uid) >= ROSTER_UID_LEN || strlen(img) >= ROSTER_IMG_LEN) {
      whole = false;
      continue;
    }
    strlcpy(roster[n].uid, uid, ROSTER_UID_LEN);
    strlcpy(roster[n].img, img, ROSTER_IMG_LEN);
    n++;
  }
  rosterCount = n;
  rosterGotOnce = true;
  Serial.printf("[이름표] 사진 있는 아이 %d명%s (px %d, %lums)\n", rosterCount,
                whole ? "" : " (칸이 모자라 일부만)", TALENT_PX,
                (unsigned long)(millis() - t0));
  return true;
#endif
}

// ── 이름·잔액·기분 조회 ──────────────────────────────────────────
// GET /api/talent/feed?uid=<UID>&limit=0 → { who: { name, balance, known, status } }
static bool lookupTalent(const char *uid, char *name, size_t nameCap, long *points, bool *known,
                         Mood *mood = nullptr) {
  Spinner spin;
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/feed?uid=%s&limit=0", TALENT_SERVER, uid);

  HTTPClient http;
  WiFiClientSecure scli;
  WiFiClient cli;
  beginHttp(http, scli, cli, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  const uint32_t t0 = millis();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[서버] 조회 실패 code=%d\n", code);
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println("[서버] 응답 해석 실패");
    return false;
  }
  strlcpy(name, doc["who"]["name"] | "", nameCap);
  *points = doc["who"]["balance"] | 0L;
  *known = doc["who"]["known"] | false;
  if (mood) moodFromJson(doc["who"]["status"], mood);
  Serial.printf("[서버] %s -> %s · %ld P%s%s%s (%lums)\n", uid, name, *points,
                *known ? "" : " (서버에 없는 키링)", (mood && mood->line[0]) ? " · 기분 " : "",
                (mood && mood->line[0]) ? mood->line : "", (unsigned long)(millis() - t0));
  return true;
}

// ── 출석 지급 ────────────────────────────────────────────────────
// POST /api/talent/earn { uid, card, device } → { name, balance, delta, status }
// 금액은 보내지 않는다 — 얼마를 어떤 사유로 줄지는 서버에 등록된 그 카드가 정한다.
// 기기는 현장에 놓인 물건이라 요청을 흉내 내기 쉬운데, 값이 서버에만 있으면 그래 봐야
// 그 카드에 적힌 만큼만 올라간다.
static bool attendEarn(const char *uid, char *name, size_t nameCap, long *points, long *delta,
                       Mood *mood = nullptr) {
  Spinner spin;
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/earn", TALENT_SERVER);

  char payload[192];
  snprintf(payload, sizeof(payload), "{\"uid\":\"%s\",\"card\":\"%s\",\"device\":\"%s\"}", uid,
           cfgAttendCard, TALENT_DEVICE_ID);

  HTTPClient http;
  WiFiClientSecure scli;
  WiFiClient cli;
  beginHttp(http, scli, cli, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);
  const int code = http.POST((uint8_t *)payload, strlen(payload));
  String body = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[출석] 지급 실패 code=%d %s\n", code, body.c_str());
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println("[출석] 응답 해석 실패");
    return false;
  }
  strlcpy(name, doc["name"] | "", nameCap);
  *points = doc["balance"] | 0L;
  *delta = doc["delta"] | 0L;
  // 지급 응답도 status 를 함께 준다 — 출석모드에서는 이 한 번으로 화면을 그리므로,
  // 기분을 따로 물으면 리더 앞에 선 아이를 왕복 한 번만큼 더 기다리게 한다.
  if (mood) moodFromJson(doc["status"], mood);
  Serial.printf("[출석] %s +%ldP → 잔액 %ld P\n", uid, *delta, *points);
  return true;
}

// ── 미등록 카드 알리기 ───────────────────────────────────────────
// POST /api/talent/seen { uid, device } → 관리자 화면의 '미등록카드' 에 뜨게 한다.
// 이걸 부르지 않으면 화면에 "등록되지 않은 키링" 만 뜰 뿐 서버에는 기록이 남지 않아,
// 관리자가 등록할 대상 자체가 생기지 않는다.
static void reportSeen(const char *uid) {
  Spinner spin;
  if (WiFi.status() != WL_CONNECTED) return;

  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/seen", TALENT_SERVER);

  char payload[128];
  snprintf(payload, sizeof(payload), "{\"uid\":\"%s\",\"device\":\"%s\"}", uid, TALENT_DEVICE_ID);

  HTTPClient http;
  WiFiClientSecure scli;
  WiFiClient cli;
  beginHttp(http, scli, cli, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);
  const int code = http.POST((uint8_t *)payload, strlen(payload));
  http.end();
  Serial.printf("[미등록] %s 알림 (code=%d)\n", uid, code);
}

// ── 출석 쿨다운 ──────────────────────────────────────────────────
// 이 키링이 방금 출석했나 — ATTEND_COOLDOWN_MS 안이면 true(다시 주지 않는다).
static bool attendedRecently(const char *uid) {
  const uint32_t nowMs = millis();
  for (int i = 0; i < attendSeenCount; i++) {
    if (!strcmp(attendSeen[i].uid, uid) && (nowMs - attendSeen[i].at) < ATTEND_COOLDOWN_MS) {
      return true;
    }
  }
  return false;
}

// 출석한 키링을 기억한다. 자리가 차면 가장 오래된 것을 밀어낸다.
static void rememberAttend(const char *uid) {
  const uint32_t nowMs = millis();
  for (int i = 0; i < attendSeenCount; i++) {
    if (!strcmp(attendSeen[i].uid, uid)) {
      attendSeen[i].at = nowMs;
      return;
    }
  }
  int slot = attendSeenCount;
  if (attendSeenCount < ATTEND_RECENT_MAX) {
    attendSeenCount++;
  } else {
    slot = 0;  // 가장 오래 전에 온 것을 찾아 덮는다
    for (int i = 1; i < ATTEND_RECENT_MAX; i++) {
      if ((int32_t)(attendSeen[i].at - attendSeen[slot].at) < 0) slot = i;
    }
  }
  strlcpy(attendSeen[slot].uid, uid, sizeof(attendSeen[slot].uid));
  attendSeen[slot].at = nowMs;
}

static void listAttend() {
  Serial.printf("[출석] 최근 기억 %d/%d개 (쿨다운 %lu초)\n", attendSeenCount, ATTEND_RECENT_MAX,
                (unsigned long)(ATTEND_COOLDOWN_MS / 1000));
  const uint32_t nowMs = millis();
  for (int i = 0; i < attendSeenCount; i++) {
    const uint32_t ago = (nowMs - attendSeen[i].at) / 1000;
    Serial.printf("  %-16s %lu초 전%s\n", attendSeen[i].uid, (unsigned long)ago,
                  (nowMs - attendSeen[i].at) < ATTEND_COOLDOWN_MS ? " (쿨다운 중)" : "");
  }
}

// ── 태그 한 건 처리 ──────────────────────────────────────────────
// UID 하나로 (1) 출석모드면 지급하고 (2) 아니면 이름·잔액만 물어 (3) 화면에 띄운다.
// loop 에서 부른다 — 서버 왕복이 있어 콜백 안에서 하지 않는다.
static void processTag(const char *uid) {
  char name[40] = "";
  Mood mood;
  mood.line[0] = '\0';
  long points = 0, delta = 0;
  bool known = false;

  // 출석모드 — 카드를 고르지 않았으면 지급하지 않는다(관리자 화면이 경고로 알린다).
  if (cfgAttendMode && cfgAttendCard[0]) {
    if (attendedRecently(uid)) {
      if (lookupTalent(uid, name, sizeof(name), &points, &known, &mood)) {
        showNfc(uid, name, points, known, "이미 출석했어요", &mood);
      } else {
        showNfc(uid, "", 0, false, "이미 출석했어요");
      }
      return;
    }
    if (attendEarn(uid, name, sizeof(name), &points, &delta, &mood)) {
      rememberAttend(uid);
      char note[32];
      snprintf(note, sizeof(note), "출석 완료 +%ldP", delta);
      showNfc(uid, name, points, true, note, &mood);
      return;
    }
    // 지급이 안 됐다 — 조회라도 해서 잔액은 띄운다. 아무 반응이 없으면 아이는
    // 기기가 고장난 줄 알고 계속 카드를 댄다.
    Serial.println("[출석] 지급하지 못해 조회만 한다");
  }

  if (lookupTalent(uid, name, sizeof(name), &points, &known, &mood)) {
    if (!known) reportSeen(uid);  // 서버에 없는 키링 — '미등록카드' 에 뜨게 알린다
    showNfc(uid, name, points, known, known ? "" : "등록되지 않은 키링", &mood);
  } else {
    showNfc(uid, "", 0, false, "서버에 물어보지 못했어요");
  }
}

// ── 들어온 명령 풀기 ─────────────────────────────────────────────
// 글자 명령 하나 — 사진이 없으니 어느 쪽이든 대기 화면으로 간다.
static void handleWord(const char *s) {
  // 설정을 지금 다시 받아라 — 값은 실려 오지 않는다. 이 기기가 **제 기기 키로**
  // 다시 물어 가져간다(관리 화면이 지은 설정을 그대로 믿지 않는다).
  if (!strcmp(s, "config")) {
    cfgWanted = true;
    return;
  }
  if (!strcmp(s, "enable") || !strcmp(s, "disable") || !strcmp(s, "toggle") ||
      !strcmp(s, "next") || !strcmp(s, "prev") || !strcmp(s, "idle")) {
    goIdle();
  }
}

// JSON 에 "uid" 가 있으면 NFC 알림, "bulk" 면 일괄 화면, 아니면 화면 전환 명령으로 본다.
static void handleCommand(const char *text, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, text, len) == DeserializationError::Ok) {
    if (!doc["bulk"].isNull()) {  // 관리 화면의 일괄 지급·사용 — 명단을 한 번에
      showBulk(doc["bulk"]["headline"] | "", doc["bulk"]["names"]);
      return;
    }
    if (doc["uid"].is<const char *>()) {  // NFC 태그 — 보내는 쪽은 UID 만 보내면 된다
      const char *uid = doc["uid"] | "";
      if (!uid[0]) return;
      // 이름·잔액이 함께 온 옛 규약도 그대로 받는다. 그때는 이미 다 알고 있으니
      // 서버에 다시 묻지 않고 바로 띄운다.
      if (doc["points"].is<long>() || doc["points"].is<int>()) {
        // 서버가 밀어 주는 것은 조회 응답과 같은 모양이다(status: {icon,label,text}) —
        // 그쪽과 같은 함수로 푼다. 한 줄로 적어 보내는 옛 길(mood)도 그대로 받는다.
        Mood m;
        if (!doc["status"].isNull()) {
          moodFromJson(doc["status"], &m);
        } else {
          strlcpy(m.line, doc["mood"] | "", sizeof(m.line));
        }
        showNfc(uid, doc["name"] | "", doc["points"] | 0L, true, doc["note"] | "", &m);
        return;
      }
      strlcpy(pendingUid, uid, sizeof(pendingUid));  // 서버 왕복은 loop 에서
      return;
    }
    if (doc["enabled"].is<bool>()) {
      goIdle();
      return;
    }
    handleWord(doc["state"] | "");
    return;
  }
  // JSON 이 아니면 그냥 글자 명령(라이브러리가 payload 를 널로 끝맺는다)
  handleWord(text);
}

static void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] #%u 붙음 %s\n", num, ip.toString().c_str());
      sendState(num);  // 붙자마자 지금 상태를 알려 준다
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[WS] #%u 떨어짐\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("[WS] #%u 받음: %.*s\n", num, (int)length, (const char *)payload);
      handleCommand((const char *)payload, length);
      break;
    default:
      break;  // BIN·PING 등은 쓰지 않는다
  }
}

// ── 서버에서 온 것 (허브) ────────────────────────────────────────
// 받는 말은 WebSocket 으로 오는 것과 같아 handleCommand 하나로 간다. 붙는 순간 지금
// 화면을 한 번 알려 준다 — 서버는 그때까지 이 기기가 무엇을 띄우는지 알 길이 없다.
static void hubEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      hubUp = true;
      Serial.println("[허브] 서버에 붙음");
      char msg[640];   // `attend` 가 붙으며 여유가 줄었다 — 넘치면 snprintf 가 잘라 JSON 이 깨진다
      buildState(msg, sizeof(msg));
      hub.sendTXT(msg);
      break;
    }
    case WStype_DISCONNECTED:
      // 끊김은 흔한 일이다(와이파이가 흔들리거나 서버를 다시 띄웠다). 라이브러리가
      // HUB_RETRY_MS 마다 다시 붙으므로 여기서 할 일은 표시를 내리는 것뿐이다.
      if (hubUp) Serial.println("[허브] 서버와 끊김 — 다시 붙어 본다");
      hubUp = false;
      break;
    case WStype_TEXT:
      Serial.printf("[허브] 받음: %.*s\n", (int)length, (const char *)payload);
      handleCommand((const char *)payload, length);
      break;
    default:
      break;
  }
}

// 와이파이가 붙은 뒤 한 번. 그다음은 라이브러리가 스스로 다시 붙는다.
static void hubStart() {
#if TALENT_HUB_ENABLE
  if (hubBegun) return;
  char host[80];
  uint16_t port;
  bool ssl;
  if (!hubTarget(host, sizeof(host), &port, &ssl)) {
    Serial.println("[허브] TALENT_SERVER 에서 주소를 읽지 못했다");
    return;
  }
  // 기기 키로 인증한다 — HTTP 로 조회·지급할 때 싣는 그 값이고, 서버는 그 키로
  // 어느 교회인지까지 가른다.
  char headers[200];
  snprintf(headers, sizeof(headers), "x-talent-key: %s\r\nx-talent-device: %s", TALENT_DEVICE_KEY,
           TALENT_DEVICE_ID);
  hub.setExtraHeaders(headers);
  hub.setReconnectInterval(HUB_RETRY_MS);
  // 조용할 때 끊긴 줄 모르고 붙어 있는 일을 막는다. 느슨하게 잡은 까닭: 서버 왕복
  // 동안(수 초) loop 가 멈춰 있어, 빡빡하면 멀쩡한 연결을 제 손으로 끊는다.
  hub.enableHeartbeat(25000, 10000, 3);
  hub.onEvent(hubEvent);
  if (ssl) {
    hub.beginSSL(host, port, TALENT_HUB_PATH);
  } else {
    hub.begin(host, port, TALENT_HUB_PATH);
  }
  hubBegun = true;
  Serial.printf("[허브] %s://%s:%u%s 에 붙는다\n", ssl ? "wss" : "ws", host, port,
                TALENT_HUB_PATH);
#endif
}

// ── 와이파이 (우선순위 4개, config.h) ────────────────────────────
// 0→1→2→3 순서로 붙어 본다. 한 곳에 WIFI_TRY_MS 만큼 붙어 보고 안 되면 다음 순위로,
// 넷 다 안 되면 처음으로 돌아가 계속 반복한다. WiFi.begin() 은 기다리지 않고 바로
// 돌아오므로 붙는 동안에도 loop 가 멈추지 않는다 — 카드를 계속 읽어야 하기 때문이다.
struct WifiAp {
  const char *ssid;
  const char *pass;
};
static const WifiAp WIFI_APS[] = {
    {WIFI_SSID_0, WIFI_PASS_0},
    {WIFI_SSID_1, WIFI_PASS_1},
    {WIFI_SSID_2, WIFI_PASS_2},
    {WIFI_SSID_3, WIFI_PASS_3},
};
static const int WIFI_AP_COUNT = sizeof(WIFI_APS) / sizeof(WIFI_APS[0]);

// 한 순위에 이만큼 붙어 보고 안 되면 다음 순위로. 부팅 직후 첫 시도는 스캔부터 하느라
// 느려서 8초로는 붙을 수 있는 AP 도 놓쳤다 — 넉넉히 준다.
static const uint32_t WIFI_TRY_MS = 15000;
// disconnect() 는 STA 가 실제로 빠지기 전에 돌아온다. 곧바로 begin() 을 부르면
// "sta is connecting, cannot set config" 로 거부되어 그 순위는 시도조차 못 하고 넘어간다.
// 그래서 끊기를 건 뒤 이만큼 틈을 두고 begin() 한다(기다리는 동안에도 loop 는 돈다).
static const uint32_t WIFI_SETTLE_MS = 300;
static const int WIFI_SETTLE_MAX = 10;

static int wifiIdx = 0;
static uint32_t wifiTryAt = 0;
static bool wifiTried = false;
static bool wifiParked = false;
static uint32_t wifiParkAt = 0;
static int wifiParkTries = 0;

static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiParked = false;
    wifiParkTries = 0;
    return;
  }

  if (wifiParked) {  // 틈을 둔 뒤 실제로 건다
    if (millis() - wifiParkAt < WIFI_SETTLE_MS) return;
    if (WiFi.begin(WIFI_APS[wifiIdx].ssid, WIFI_APS[wifiIdx].pass) == WL_CONNECT_FAILED &&
        wifiParkTries < WIFI_SETTLE_MAX) {
      wifiParkTries++;  // 아직 안 빠졌다 — 틈을 더 주고 다시
      wifiParkAt = millis();
      return;
    }
    wifiParked = false;
    wifiParkTries = 0;
    wifiTryAt = millis();
    wifiTried = true;
    Serial.printf("[와이파이] %d순위 '%s' 에 붙어 보는 중\n", wifiIdx + 1, WIFI_APS[wifiIdx].ssid);
    return;
  }

  if (wifiTried && millis() - wifiTryAt < WIFI_TRY_MS) return;  // 붙는 중 — 더 기다린다

  if (wifiTried) wifiIdx = (wifiIdx + 1) % WIFI_AP_COUNT;  // 실패 → 다음 순위
  WiFi.disconnect();
  wifiParkAt = millis();
  wifiParked = true;
  wifiParkTries = 0;
}

// 와이파이가 붙으면 mDNS 와 WebSocket 서버를 켠다(한 번만).
static void startServers() {
  if (serversUp) return;
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("ws", "tcp", WS_PORT);
    Serial.printf("[mDNS] %s.local 준비됨\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[mDNS] 시작 실패");
  }
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  hubStart();  // 서버에 붙어 변화를 받아 온다(client 쪽)
  Serial.printf("[WS] 서버 열림 — ws://%s.local:%d/  (또는 ws://%s:%d/)\n", MDNS_HOSTNAME, WS_PORT,
                WiFi.localIP().toString().c_str(), WS_PORT);
  serversUp = true;
}

// ── 핀 진단 ──────────────────────────────────────────────────────
// 내부 풀업/풀다운(약 45k)을 번갈아 걸고 읽으면 미연결 / GND 단락 / 외부 풀업이 구분된다.
// GPIO3 고착을 이 방법으로 잡아냈다.
static void probePin(uint8_t pin, const char *name) {
  pinMode(pin, INPUT);
  delayMicroseconds(100);
  const bool hiZ = digitalRead(pin);
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(100);
  const bool pu = digitalRead(pin);
  pinMode(pin, INPUT_PULLDOWN);
  delayMicroseconds(100);
  const bool pd = digitalRead(pin);
  pinMode(pin, INPUT);

  const char *verdict;
  if (!pu && !pd) {
    verdict = "HELD LOW (short to GND or driven low)";
  } else if (pu && pd) {
    verdict = "HELD HIGH (ext pull-up or short to 3V3)";
  } else if (pu && !pd) {
    verdict = "floating / high-Z";
  } else {
    verdict = "unknown";
  }
  Serial.printf("  %-4s (GPIO%2d): hiZ=%c pullup=%c pulldown=%c  -> %s\n", name, pin,
                hiZ ? 'H' : 'L', pu ? 'H' : 'L', pd ? 'H' : 'L', verdict);
}

// ── PN532 읽기 ───────────────────────────────────────────────────
// 올려둔 동안 서버를 반복해 찌르지 않는다 — 같은 UID 가 NFC_SHOW_MS 안에 또 읽히면
// 새 태그로 보지 않는다.
static void nfcService() {
  uint8_t uid[7] = {0};
  uint8_t uidLen = 0;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, SCAN_TIMEOUT_MS)) return;
  if (uidLen == 0 || uidLen > sizeof(uid)) return;

  const bool sameCard = (uidLen == heldUidLen) && (memcmp(uid, heldUid, uidLen) == 0);
  const bool stillHeld = sameCard && (millis() - heldAt < NFC_SHOW_MS);

  memcpy(heldUid, uid, uidLen);
  heldUidLen = uidLen;
  heldAt = millis();
  if (stillHeld) return;

  readCount++;
  char hex[24] = "";
  for (uint8_t i = 0; i < uidLen; i++) {
    char b[4];
    snprintf(b, sizeof(b), "%02X", uid[i]);
    strlcat(hex, b, sizeof(hex));
  }
  Serial.printf("[리더] card #%lu  UID %s (%d bytes)\n", (unsigned long)readCount, hex, uidLen);
  strlcpy(pendingUid, hex, sizeof(pendingUid));  // 서버 왕복은 loop 에서
}

// ── 버튼 (터치 대신) ────────────────────────────────────────────
// 짧게: 대기 화면으로 · 길게: 설정 다시 받기
static void buttonService() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  static bool longFired = false;

  const bool down = (digitalRead(BTN_PIN) == LOW);
  if (down && !wasDown) {
    downAt = millis();
    longFired = false;
  }
  if (down && !longFired && (millis() - downAt) >= LONG_PRESS_MS) {
    longFired = true;  // 누르는 동안 한 번만
    Serial.println("[버튼] 길게 누름 — 설정 다시 받기");
    drawStatus("설정 다시 받는 중");
    fetchConfig();
    goIdle();
  }
  if (!down && wasDown && !longFired) {
    Serial.println("[버튼] 짧게 누름");
    goIdle();
  }
  wasDown = down;
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // USB CDC 가 올라올 때까지 대기

  Serial.println();
  Serial.println("=== LCDTest — NFC UID -> 이름·잔액·기분 ===");
  Serial.printf("Display: FSPI  SCK=%d MOSI=%d CS=%d DC=%d RST=%d BLK=%d @ %ld Hz\n", TFT_SCK,
                TFT_MOSI, TFT_CS, TFT_DC, TFT_RST, TFT_BLK, TFT_SPI_HZ);
  Serial.printf("PN532  : HSPI  SCK=%d MISO=%d MOSI=%d CS=%d\n", PN532_SCK, PN532_MISO, PN532_MOSI,
                PN532_CS);
  Serial.printf("Server : %s · device %s\n", TALENT_SERVER, TALENT_DEVICE_ID);

  gfxLock = xSemaphoreCreateMutex();
  pinMode(BTN_PIN, INPUT_PULLUP);

#if PHOTO_ENABLE
  // 이름표는 PSRAM 에 둔다(400칸 = 약 41KB). 내부 RAM 은 화면 버퍼·TLS·WebSocket 이
  // 나눠 쓰므로 이만한 덩이를 여기서 빼지 않는다.
  roster = (RosterRow *)ps_malloc(sizeof(RosterRow) * MAX_ROSTER);
  if (!roster) roster = (RosterRow *)malloc(sizeof(RosterRow) * MAX_ROSTER);
  Serial.printf("Roster : %d칸 %s (%u bytes)\n", MAX_ROSTER, roster ? "확보" : "실패",
                (unsigned)(sizeof(RosterRow) * MAX_ROSTER));
#if PHOTO_CACHE_MAX > 0
  // 칸은 필요할 때 한 장씩 잡는다 — 여기서는 얼마나 쓸 수 있는지만 알린다.
  Serial.printf("Photo  : 캐시 최대 %d장 x %u KB = %u KB · 지금 PSRAM %lu KB\n", PHOTO_CACHE_MAX,
                (unsigned)(PHOTO_BYTES / 1024),
                (unsigned)((size_t)PHOTO_CACHE_MAX * PHOTO_BYTES / 1024),
                (unsigned long)(ESP.getFreePsram() / 1024));
#endif
#endif

  // 백라이트는 꺼둔 채로 패널을 초기화한다 — 초기화 중의 깨진 화면이 안 보이게
  ledcAttach(TFT_BLK, BLK_FREQ, BLK_RES);
  ledcWrite(TFT_BLK, 0);

  if (!gfx->begin(TFT_SPI_HZ)) {
    Serial.println("ERROR: gfx->begin() 실패 — 디스플레이 배선을 확인하세요");
    while (true) {
      delay(1000);
    }
  }
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();
  backlightOn(cfgBacklight);

  // 고리를 돌릴 일꾼. 서버 왕복이 loop 를 붙잡고 있는 동안에도 돌아간다.
  xTaskCreate(spinnerTask, "spinner", 4096, nullptr, 1, nullptr);

  drawStatus("PN532 준비 중");

  Serial.println("PN532 pin probe:");
  probePin(PN532_SCK, "SCK");
  probePin(PN532_MISO, "MISO");
  probePin(PN532_MOSI, "MOSI");
  probePin(PN532_CS, "CS");

  // 핀을 직접 지정해 HSPI 를 먼저 연다. 라이브러리의 내부 SPI.begin() 은 이미
  // 초기화된 버스에 대해 아무 일도 하지 않으므로 이 설정이 유지된다.
  pnSpi.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_CS);

  if (!nfc.begin()) {
    Serial.println("ERROR: PN532 SPI 버스를 열 수 없습니다");
    drawFatal("PN532", "SPI 초기화 실패");
    while (true) {
      delay(1000);
    }
  }

  uint32_t ver = 0;
  for (int attempt = 1; attempt <= 3 && !ver; attempt++) {
    ver = nfc.getFirmwareVersion();
    if (!ver) {
      Serial.printf("  getFirmwareVersion 실패 (%d/3)\n", attempt);
      delay(200);
    }
  }
  if (!ver) {
    Serial.println("ERROR: PN532 를 찾을 수 없습니다");
    Serial.println("  - 모듈 모드 스위치가 SPI 인지 확인 (SEL0=OFF, SEL1=ON)");
    Serial.println("  - MISO/MOSI 가 서로 바뀌지 않았는지 확인");
    drawFatal("PN532", "찾을 수 없음");
    while (true) {
      delay(1000);
    }
  }
  Serial.printf("PN532 found: chip PN5%02X, firmware %d.%d\n", (ver >> 24) & 0xFF,
                (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);

  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(1);  // 카드가 없을 때 빨리 포기하도록

  // 와이파이가 NVS 에 쓰지 않게 막는다 — 플래시에 쓰는 동안 캐시가 꺼져 화면·SPI 가
  // 늘어진다(참고 기기에서는 RGB 패널 그림이 쓸렸다).
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  drawStatus("WiFi 붙는 중");

  Serial.printf("Ready. free heap %lu\n", (unsigned long)ESP.getFreeHeap());
}

void loop() {
  nfcService();   // 이 보드에 붙은 리더가 읽은 것
  wifiService();  // 와이파이 — 붙어 있으면 아무 일도 하지 않는다

  // 붙거나 끊기는 순간에만 다시 그린다(매번 그리면 화면이 깜빡인다)
  const bool nowOnline = (WiFi.status() == WL_CONNECTED);
  if (nowOnline != online) {
    online = nowOnline;
    if (online) {
      Serial.printf("[와이파이] 연결됨 %s (%s) · RSSI %d dBm\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      startServers();  // 붙은 뒤에 mDNS·WebSocket 을 켠다
      fetchConfig();   // 밝기·출석모드를 받아 온다(못 받으면 들고 있던 값으로 돈다)
      fetchRoster();   // UID→사진 이름. 사진 자체는 태깅할 때 한 장씩 받는다
    } else {
      Serial.println("[와이파이] 끊김");
    }
    if (mode == MODE_IDLE) drawIdle();  // 대기 화면일 때만 갱신(NFC 화면은 건드리지 않는다)
    sendState();
  }

  if (serversUp) webSocket.loop();  // 받은 것을 처리
  if (hubBegun) hub.loop();         // 다시 붙기·heartbeat 가 여기서 돈다

  // 넘어온 태그 — 조회·지급은 수백 ms 가 걸려 콜백 밖(여기)에서 한다
  if (pendingUid[0]) {
    char uid[32];
    strlcpy(uid, pendingUid, sizeof(uid));
    pendingUid[0] = '\0';  // 처리 중에 다음 태그가 들어와도 덮이지 않게 먼저 비운다
    processTag(uid);
  }

  // 설정 다시 받기 — 주기(5분)가 됐거나, 관리 화면이 "지금 받아라" 를 밀었을 때.
  // 민 쪽이 있으면 그 자리에서 받으므로 밝기·출석모드가 누른 즉시 바뀐다.
  if (online && (cfgWanted || (int32_t)(millis() - cfgNextAt) >= 0)) {
    cfgWanted = false;
    fetchConfig();
  }

  // 이름표 다시 받기 — 아이가 사진을 바꾸면 img 이름이 달라져 이때 반영된다.
  // NFC·일괄 화면을 띄우고 있을 때는 미룬다(받는 동안 loop 가 멈춰 화면이 굳는다).
  if (online && mode == MODE_IDLE && (int32_t)(millis() - rosterNextAt) >= 0) fetchRoster();

  buttonService();

  // 시리얼 명령 — 한 글자씩(줄바꿈은 무시)
  while (Serial.available()) {
    const int c = Serial.read();
    switch (c) {
      case 'h':
      case 'H':
      case 'n':
      case 'N':
        Serial.println("[명령] 대기 화면");
        goIdle();
        break;
      case 'c':
      case 'C':
        Serial.println("[명령] 설정 다시 받기");
        fetchConfig();
        break;
      case 'i':
      case 'I':
        Serial.printf("[상태] 기기 %s · 밝기 %d/16 · 출석모드 %s · 출석 카드 %s · 설정 %s\n",
                      TALENT_DEVICE_ID, cfgBacklight, cfgAttendMode ? "켜짐" : "꺼짐",
                      cfgAttendCard[0] ? cfgAttendCard : "없음",
                      cfgGotOnce ? "받음" : "못 받음(기본값으로 동작)");
        Serial.printf("[상태] 서버 연결 %s\n",
                      TALENT_HUB_ENABLE
                          ? (hubUp ? "붙어 있음" : (hubBegun ? "끊김(다시 붙는 중)" : "아직 안 켬"))
                          : "꺼 둠");
        Serial.printf("[상태] 이름표 %s · 사진 있는 아이 %d명 · px %d\n",
                      rosterGotOnce ? "받음" : "못 받음(사진 없이 글자만)", rosterCount,
                      TALENT_PX);
#if PHOTO_CACHE_MAX > 0
        photoCacheReport();
#endif
        Serial.printf("[상태] ws client %u곳 · 와이파이 %s · 읽은 카드 %lu장 · heap %lu\n",
                      serversUp ? webSocket.connectedClients() : 0,
                      online ? WiFi.localIP().toString().c_str() : "끊김",
                      (unsigned long)readCount, (unsigned long)ESP.getFreeHeap());
        break;
      case 'f':
      case 'F':
        Serial.println("[명령] 이름표 다시 받기");
        fetchRoster();
        for (int i = 0; i < rosterCount; i++) {
          Serial.printf("  %-16s %s\n", roster[i].uid, roster[i].img);
        }
        break;
      // 이름표의 아이를 차례로 하나 골라 **실제 서버로** 조회·사진 받기를 해 본다.
      // 카드 없이 사진까지 가는 길을 눈으로 보는 용도다('t' 는 서버를 치지 않는다).
      case 'd':
      case 'D':
        if (!rosterCount) {
          Serial.println("[시연] 이름표가 비어 있다 — 'f' 로 먼저 받으세요");
        } else {
          static int demoIdx = 0;
          demoIdx %= rosterCount;
          Serial.printf("[시연] %d/%d  %s\n", demoIdx + 1, rosterCount, roster[demoIdx].uid);
          processTag(roster[demoIdx].uid);
          nfcUntil = millis() + 30000;  // 들여다볼 시간을 준다
          demoIdx++;
        }
        break;
      case 'x':
      case 'X':
#if PHOTO_CACHE_MAX > 0
        Serial.println("[명령] 사진 캐시 비우기");
        photoCacheClear();
#else
        Serial.println("[명령] 사진 캐시를 꺼 두었습니다 (PHOTO_CACHE_MAX 0)");
#endif
        break;
      case 's':
      case 'S':
        listAttend();
        break;
      case 't':
      case 'T':
        showTestScreen();
        break;
      case 'g':
      case 'G':
        Serial.println("[명령] 불러오는 중 고리 3초");
        spinStart();
        delay(3000);  // delay 는 쉬는 것이라 그동안 일꾼이 돈다
        spinStop();
        goIdle();
        break;
      case 'w':
      case 'W':
        Serial.println("[명령] 와이파이 다시 붙기");
        wifiTried = false;
        WiFi.disconnect();
        break;
      case 'r':
      case 'R':
        Serial.println("[명령] 재부팅합니다...");
        Serial.flush();
        ESP.restart();
        break;
      case '?':
        Serial.println("[명령] h 대기 · c 설정 받기 · f 이름표 받기+목록 · d 실제 조회 시연 · "
                       "i 상태 · s 출석 목록 · x 사진 캐시 비우기 · t 시험 화면 · g 고리 · "
                       "w 와이파이 다시 · r 재부팅");
        break;
      default:
        break;
    }
  }

  // NFC·일괄 화면은 얼마 뒤 자동으로 대기 화면으로 돌아간다
  if ((mode == MODE_NFC || mode == MODE_BULK) && (int32_t)(millis() - nfcUntil) >= 0) goIdle();

  // 대기 화면은 고리가 돌아야 해서 계속 다시 그린다
  if (mode == MODE_IDLE) drawIdle();
}
