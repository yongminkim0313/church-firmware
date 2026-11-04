// singleTest — 원형 480 화면(LILYGO T-RGB · HL021TWV9009ANT)에 펀펀포인트 사진(48×48)을 바둑판으로 깐다.
// 한 사람은 한 칸뿐이다 — 키링이 여럿이라 목록에 같은 사진이 여러 번 와도 한 번만 받는다.
// 화면이 동그라미라 원 안에 통째로 드는 칸에만, 가운데부터 바깥쪽으로 놓는다.
// ★ 데이터는 **화면이 검은 동안에만** 받는다 — 켤 때, 그리고 대기 화면이 한 바퀴 돌 때마다.
//   이름표·사진·알림판을 다 받으면 홈부터 그린다. 와이파이는 붙인 채 둔다(2026-11-04).
//   (예전엔 끄고 그렸다 — 켜 두면 받는 순간이 아니어도 화면이 위로 10px 쯤 밀려 자글거렸다, 2026-11-03.)
//   서버(/ws/device)에는 붙지 않는다 — 사진·포인트가 바뀌면 다음 바퀴에 반영된다.
// 와이파이는 폰으로 넣을 수 있다 — 설정 모드(portal.h)에서 화면의 QR 을 찍어 붙으면 설정 페이지가
// 뜬다. 들어가는 길: 켜고 10초 안에 BOOT 단추 1초 · 아는 와이파이가 안 보일 때 저절로 · 시리얼 'w'.
// 알림 화면(이름 옆에 그 아이 사진(48) · 그 아래 지급/사용 · 몇 포인트)과 일괄 명단 화면은
// 시리얼 't' · 'b' 로만 띄워 본다.
// 대기 화면은 6초마다 넘어간다 — 홈(바둑판) → 오늘 출석 → 포인트 순위 → 이번 달 출석 많은 순 → 홈.
// 세 장의 자료는 서버의 GET /api/talent/signage 한 번으로 받는다 — 켤 때, 화면이 아직 검을 때.
// 화면 켜는 순서와 객체 설정은 nfcSingle(../single/src/main.cpp)에서 그대로 가져왔다:
//   1) XL9535 확장칩  2) RGB 패널  3) 초기화 표(st7701_hl021_init.h)  4) 백라이트(GPIO46)
#include <Arduino.h>
#include <algorithm>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LittleFS.h>               // 받은 사진을 두었다가 켤 때 다시 쓴다(spiffs 파티션)
#include <Arduino_GFX_Library.h>
#include <qrcode.h>               // 설정 모드 화면의 접속용 QR(ricmoo/QRCode)
#include <U8g2lib.h>             // 한글 굴림 16px 폰트(u8g2_font_gulim16_t_korean2)가 여기서 온다 — single 과 같다
#include "st7701_hl021_init.h"

#define PIN_I2C_SDA   8
#define PIN_I2C_SCL  48
#define PIN_BL       46          // 백라이트

#define COL_BG    0x0000         // 검정

// 사진 — 서버가 리틀엔디안 RGB565 로 준다. 그대로 화면 버퍼에 옮기면 된다.
// 와이파이와 기기 키는 secrets/ChurchSecrets.h 에서 온다(깃 제외 — single 과 같다).
// 기기 키는 TALENT_DEVICE_KEY 그대로, 와이파이는 그 파일의 WIFI_SSID_TEST 한 벌을 쓴다.
// 받아 간 뒤 한 번:  cp secrets/ChurchSecrets.example.h secrets/ChurchSecrets.h
#include <ChurchSecrets.h>
#define WIFI_SSID         WIFI_SSID_TEST
#define WIFI_PASS         WIFI_PASS_TEST
#define TALENT_HOST       "funfun.youthvision.co.kr"
#define TALENT_SERVER     "https://" TALENT_HOST
#define TALENT_DEVICE_ID  "single-test"     // 관리 화면의 '기기' 목록에 이 이름으로 뜬다
#define FETCH_RETRY_MS    5000UL            // 켤 때 이름표를 못 받으면 이만큼 뒤에 다시
#define BOARD_TRIES       3                 // 알림판은 이만큼 해 보고 안 되면 홈만 돌린다
#define REFRESH_JOIN_TRIES 2                // 한 바퀴 뒤 다시 받을 때 와이파이는 이만큼만 붙어 본다
#define REFRESH_MIN_MS    30000UL           // 다시 받기 사이 최소 간격(알림판이 없어 홈만 돌 때)
// 서버가 만들어 주는 크기(funfunServer talentPhoto.js 의 DEVICE_SIZES: 48·208·240·480) 중 가장 작은 것.
// 48 은 동그라미로 잘라서 준다.
#define PHOTO_PX      48
#define PHOTO_BYTES   (PHOTO_PX * PHOTO_PX * 2)   // 4608B
#define GRID          (480 / PHOTO_PX)            // 한 줄에 놓는 칸 수 — 48 이면 10×10
#define PHOTO_MAX     64                // 들고 있을 장 수(한 장 4.6KB 라 넉넉히 둔다)
#define IMG_MAX       64                // img 이름 길이(ph-<해시>-48.565)
// 서버가 원 바깥에 채우는 비침색(자홍) — funfunServer talent/rgb565.js 의 TRANSPARENT_KEY.
// 진짜 자홍 픽셀은 서버가 한 칸 비껴 주므로 이 값이면 틀림없이 바깥이다. 받자마자 바탕색으로 바꾼다.
#define TRANSPARENT_KEY 0xF81F

// ── 포인트 알림 화면 ──────────────────────────────────────────────
// 알림 때는 아무것도 받지 않는다 — 바둑판에 깔린 그 아이의 48 을 이름 옆에 그대로 놓는다.
// 그때마다 큰 사진(240)을 받아 오면 와이파이(플래시에서 도는 코드)와 화면(PSRAM 을 퍼내는 일)이
// 한 길을 다퉈, 알림이 잇따를 때 화면이 위로 10px 쯤 밀렸다 돌아왔다.
#define CARD_GAP      12                // 사진과 이름 사이
#define CARD_MS       6000              // 이만큼 띄우고 바둑판으로 돌아간다
#define BULK_MS       10000             // 일괄 명단을 이만큼 띄우고 바둑판으로 돌아간다
#define BULK_NAMES_MAX 40               // 서버가 자르는 수와 같다(funfunServer talentFeed 의 BULK_NAMES_MAX)
#define BULK_COL_W    118               // 명단 한 칸의 너비
#define CARD_MIN_MS   3000              // 알림이 줄지어 와도 한 장은 이만큼은 보인다
#define CARD_QUEUE    8                 // 기다리는 알림을 이만큼까지 쌓는다(넘치면 가장 오래된 것을 버린다)
#define ROW_MAX       160               // 이름표 줄(카드마다 한 줄 — 아이 수보다 조금 많다)
#define COL_GIVE      0x07E0            // 지급 — 초록
#define COL_USE       0xFD20            // 사용 — 주황
#define COL_UNDO      0xC618            // 되돌림 — 회색
#define COL_TEXT      0xFFFF
#define COL_GOLD      0xFEA0            // 1등
#define COL_DIM       0x9CF3            // 등수·안내 글

// ── 대기 화면 돌리기 ──────────────────────────────────────────────
#define PAGE_MS          6000           // 한 장을 이만큼 띄우고 다음 장으로
#define BOARD_TODAY_MAX  64             // 오늘 출석은 사진만 깐다 — 칸이 모자라면 남은 아이는 빠진다
#define BOARD_RANK_ROWS  20             // 순위는 위에서 스무 줄 — 표로 그려 위에서 아래로 굴린다

// 순위 표 — 원에 맞추지 않은 네모 표. 표 전체를 화면 밖 캔버스(PSRAM)에 한 번 그려 두고,
// 가운데 창(WIN)에 보이는 만큼만 옮겨 그리며 천천히 굴린다.
//   위에서 RANK_HOLD_MS 멈춤 → RANK_SPEED px/초로 아래까지 → 끝에서 RANK_HOLD_MS 멈춤
//   스무 줄이면 (1000-358+70)/60 ≈ 12초를 굴러 모두 17초쯤 뜬다.
// 창은 화면 맨 아래까지 연다 — 원 가장자리에 걸린 줄은 원이 자연스럽게 자른다. 다 굴렀을 때는
// 마지막 줄이 원에 먹히지 않게 RANK_TAIL 만큼 더 올려 그 아래를 비운다.
#define TABLE_W        360              // 표 너비 — y=400 까지는 통째로 원 안에 든다
#define TABLE_X        ((480 - TABLE_W) / 2)
#define ROW_H          50               // 한 줄 높이(사진 48 + 틈)
#define RANK_TITLE_CY  58
#define RANK_HEAD_CY   100              // 머리 줄('순위 · 이름 · 포인트')
#define WIN_Y          122              // 굴러가는 창 — 이 아래부터
#define WIN_H          (480 - WIN_Y)    // 화면 맨 아래까지
#define RANK_TAIL      70               // 다 굴렀을 때 마지막 줄 아래 빈 자리 — 마지막 줄이 y=410 에서 끝난다
#define RANK_HOLD_MS   2500
#define RANK_SPEED     60               // px/초
#define RANK_FRAME_MS  33               // 한 번 옮겨 그리는 간격(초당 약 30번)
#define COL_SILVER     0xC618
#define COL_BRONZE     0xCC27
#define COL_ZEBRA      0x10A2           // 짝수 줄 바탕 — 줄을 눈으로 따라가기 쉽게
#define COL_RULE       0x39E7           // 머리 줄 밑줄
#define TITLE_CY         70             // 제목 줄의 세로 가운데
#define FONT_KO       u8g2_font_gulim16_t_korean2
#define FONT_KO_PX    16

Arduino_XL9535SWSPI *bus = new Arduino_XL9535SWSPI(
    PIN_I2C_SDA, PIN_I2C_SCL,
    2 /* 전원 인에이블 */, 3 /* CS */, 5 /* SCK */, 4 /* MOSI */);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    45 /* DE */, 41 /* VSYNC */, 47 /* HSYNC */, 42 /* PCLK */,
    21 /* R0 */, 18 /* R1 */, 17 /* R2 */, 16 /* R3 */, 15 /* R4 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    7  /* B0 */, 6  /* B1 */, 5  /* B2 */, 3  /* B3 */, 2  /* B4 */,
    1 /* hsync_polarity */, 60 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 60 /* hsync_back_porch */,
    1 /* vsync_polarity */, 20 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    1 /* pclk_active_neg */,
    17000000L /* 픽셀 클럭 — 데이터시트 최소(17~27, typ. 20) · 약 53Hz */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    480 * 20 /* 중계 버퍼(픽셀 수) */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0 /* 회전 */, true /* 그릴 때마다 바로 반영 */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_hl021_init_operations, sizeof(st7701_hl021_init_operations));

#include "creds.h"               // 아는 와이파이 목록 — 폰으로 넣은 것(NVS)이 앞, WIFI_SSID 가 뒤
#include "portal.h"              // 와이파이 설정 모드 — 일꾼이 돌리고 loop 가 그린다

// 큰 버퍼는 PSRAM 에 둔다. mbedTLS 는 내부 RAM 에서만 잡는데(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC),
// 아래 이름표·알림판들이 정적 배열로 내부 RAM 을 65KB 쯤 먹어 TLS 가 한 번에 하나밖에 안 들었다 —
// 허브가 붙어 있으면 https 가, https 를 쥐고 있으면 허브가 "SSL - Memory allocation failed" 로 못 붙었다.
// 이 빌드는 정적 변수를 PSRAM 에 둘 수 없어(SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 없음) 켤 때 잡아 참조로 묶는다.
// PSRAM 은 전역 생성자보다 먼저 힙에 들어온다.
template <typename T> static T &psram() { return *static_cast<T *>(ps_calloc(1, sizeof(T))); }
typedef char ImgNames[PHOTO_MAX][IMG_MAX];

// ── 사진 받기 ───────────────────────────────────────────────────────
// 와이파이·https·웹소켓은 기다림이 길어 따로 띄운 일꾼(0번 심)이 맡는다. 화면은 loop 만 만진다.
// 일꾼이 새 사진 묶음을 다 마련하면 next* 에 적고 swapReady 를 세운다 → loop 가 shown 으로
// 옮겨 그리고 내린다 → 일꾼이 그제야 안 쓰게 된 옛 버퍼를 푼다(그리는 중인 것을 풀지 않게).
static uint16_t *shown[PHOTO_MAX];          // loop 가 그리는 묶음
static int shownCount = 0;
static ImgNames &shownImg = psram<ImgNames>();   // shown 의 img 이름 — 순위·출석 화면이 사진을 찾는다
static uint16_t *nextPtrs[PHOTO_MAX];       // 일꾼이 건네는 묶음
static ImgNames &nextImg = psram<ImgNames>();
static int nextCount = 0;
static volatile bool swapReady = false;

// 알림판 — 오늘 출석 · 포인트 순위 · 이번 달 출석. 일꾼이 boardNext 를 채우고 boardReady 를
// 세우면 loop 가 boardShown 으로 옮긴다. 사진은 싣지 않고 img 이름만 — 이미 받아 둔 48 을 쓴다.
struct BoardRow { char name[32]; char img[IMG_MAX]; int rank; int value; };
struct Board {
  bool ok;                                  // 한 번이라도 받았다
  int  todayN, pointsN, monthN;
  BoardRow today[BOARD_TODAY_MAX];
  BoardRow points[BOARD_RANK_ROWS];
  BoardRow month[BOARD_RANK_ROWS];
  int  mon;                                 // 몇 월인가(제목에 쓴다)
};
static Board &boardNext = psram<Board>(), &boardShown = psram<Board>();
static volatile bool boardReady  = false;

// 켤 때 받을 것을 다 받고 와이파이를 껐다 — loop 는 이것이 선 뒤에야 첫 홈을 그린다.
// 받는 틈을 골라 보기도 했지만(홈 앞 '받는 중' 화면에서만 받기) 와이파이가 켜져 있는 한 바둑판이
// 여전히 밀려 보였다(2026-11-03). 그래서 화면이 검은 동안 다 받고 끈다.
static volatile bool netDone = false;
static volatile bool netWant = false;       // loop → 일꾼: 한 바퀴 돌았다, 다시 받아라

// 알림 한 장. 일꾼이 마련해 cardReady 를 세우면 loop 가 그리고 내린다.
struct Card {
  char name[48];
  int  delta;                               // +면 지급, -면 사용
  bool undo;                                // 되돌림(관리자가 내역을 지웠다)
  bool hasPhoto;
  char why[96];                             // '분류 · 사유' — 관리 화면에서 고른 분류와 적은 사유
};
static Card cardNext;
// 그 아이의 48 을 베껴 둔다 — 바둑판의 버퍼는 이름표를 다시 받을 때 풀릴 수 있다.
static uint16_t (&cardPhoto)[PHOTO_PX * PHOTO_PX] = psram<uint16_t[PHOTO_PX * PHOTO_PX]>();
static volatile bool cardReady = false;

static volatile uint32_t cardFreeAt = 0;    // 이 시각부터 다음 알림을 건네도 된다(loop 가 정한다)

// 일괄 지급·사용 한 장 — 시리얼 'b' 가 bulkPend 에 받아 두고, 일꾼이 차례가 되면 bulkNext 로 건넨다.
struct Bulk {
  int  delta;
  char why[96];                             // '분류 · 사유'
  int  n;
  char names[BULK_NAMES_MAX][32];
};
static Bulk bulkPend, bulkNext;
static bool bulkPendHas = false;            // 일꾼만 만진다
static volatile bool bulkReady = false;

// 켜고 PORTAL_BOOT_WINDOW_MS 안에 BOOT 단추를 PORTAL_BOOT_HOLD_MS 누르고 있었나(portal.h 머리말).
// 일꾼이 기다리는 자리마다 부른다 — 와이파이에 붙는 동안에도 손이 빗나가지 않게.
static bool bootHeld() {
  static uint32_t downAt = 0;
  if (millis() > PORTAL_BOOT_WINDOW_MS) return false;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!downAt) downAt = millis();
    else if (millis() - downAt >= PORTAL_BOOT_HOLD_MS) { downAt = 0; return true; }
  } else {
    downAt = 0;
  }
  return false;
}

static bool wifiEverUp     = false;   // 켠 뒤 한 번이라도 붙었나 — 붙었던 뒤에는 저절로 설정 모드에 안 간다
static int  wifiNoApRounds = 0;       // 아는 곳이 하나도 안 보인 바퀴가 몇 번 이어졌나
static const char *portalWanted = nullptr;   // 설정 모드로 가야 할 까닭(BOOT 단추)

// 둘러보고 아는 곳 중 **목록에서 가장 앞선 것**에 붙는다(creds.h 머리말). 붙으면 true.
// 끊은 직후 begin() 은 "sta is connecting" 으로 거부되므로 틈을 두고 다시 건다(single 과 같다).
// 부팅 직후 첫 시도는 스캔부터 하느라 느려서 15초를 준다.
static bool wifiJoin() {
  const int n = WiFi.scanNetworks();
  int pick = -1;
  for (int i = 0; i < wifiApCount && pick < 0; i++)
    for (int j = 0; j < n; j++)
      if (WiFi.SSID(j) == wifiAps[i].ssid) { pick = i; break; }
  WiFi.scanDelete();
  if (pick < 0) {
    wifiNoApRounds++;
    Serial.printf("[와이파이] 아는 곳이 안 보인다 — %d바퀴째 (보이는 곳 %d)\n", wifiNoApRounds, n);
    return false;
  }
  wifiNoApRounds = 0;

  const WifiAp &ap = wifiAps[pick];
  Serial.printf("[와이파이] %d순위 %s 에 붙어 본다\n", pick + 1, ap.ssid);
  wifiStaBusy = true;
  for (int k = 0; k < 10 && WiFi.begin(ap.ssid, ap.pass) == WL_CONNECT_FAILED; k++)
    vTaskDelay(pdMS_TO_TICKS(300));
  for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) {
    if (bootHeld()) { portalWanted = "켤 때 BOOT 단추를 누르고 있었다"; break; }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiEverUp = true;
    Serial.printf("[와이파이] 붙었다 %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.printf("[와이파이] 실패 status=%d\n", (int)WiFi.status());
  WiFi.disconnect();
  return false;
}

// 사진 한 장을 dst(PSRAM) 에 바로 받는다. 크기가 꼭 맞아야 true.
static bool fetchPhoto(HTTPClient &http, WiFiClientSecure &scli, const char *img, uint16_t *dst) {
  const size_t need = PHOTO_BYTES;
  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, img);
  http.begin(scli, url);
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[사진] %s 실패 code=%d\n", img, code); return false; }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t *p = (uint8_t *)dst;
  size_t got = 0;
  uint32_t idleAt = millis();
  while (http.connected() && got < need) {
    const size_t avail = stream->available();
    if (avail) {
      const size_t want = min(avail, need - got);
      const int n = stream->readBytes(p + got, want);
      if (n > 0) { got += n; idleAt = millis(); }
    } else {
      if (millis() - idleAt > 5000) break;          // 5초간 아무것도 안 오면 포기
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
  http.end();
  if (got != need) Serial.printf("[사진] %s 크기 안 맞음 %u/%u\n", img, (unsigned)got, (unsigned)need);
  if (got != need) return false;
  for (int i = 0; i < PHOTO_PX * PHOTO_PX; i++)
    if (dst[i] == TRANSPARENT_KEY) dst[i] = COL_BG;
  return true;
}

// ── 사진 보관(플래시) ─────────────────────────────────────────────
// 받은 48 을 /p/<img> 로 두었다가 켤 때 거기서 읽는다 — 다시 켤 때마다 64장을 새로 받지 않는다.
// img 이름에 사진 해시가 들어 있어 이름이 같으면 같은 사진이다(바뀌면 이름이 달라 새로 받는다).
// ★ 플래시는 읽든 쓰든 그동안 캐시가 꺼져 화면이 밀린다(fetchTask 머리말과 같은 까닭). 사진은
//   켤 때 한 번, **화면에 처음 그리기 전**(아직 검은 화면)에만 받으므로 그때만 만진다.
static bool fsOk = false;

static bool cachePath(const char *img, char *out, size_t cap) {
  return snprintf(out, cap, "/p/%s", img) < (int)cap;
}

static bool cacheLoad(const char *img, uint16_t *dst) {
  char path[80];
  if (!cachePath(img, path, sizeof(path)) || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const bool ok = f.size() == PHOTO_BYTES && f.read((uint8_t *)dst, PHOTO_BYTES) == PHOTO_BYTES;
  f.close();
  return ok;
}

static void cacheSave(const char *img, const uint16_t *src) {
  char path[80];
  if (!cachePath(img, path, sizeof(path))) return;
  File f = LittleFS.open(path, "w");
  if (!f) return;
  const bool ok = f.write((const uint8_t *)src, PHOTO_BYTES) == PHOTO_BYTES;
  f.close();
  if (!ok) LittleFS.remove(path);           // 반쪽짜리를 남기지 않는다(자리가 모자랐다)
}

// 이름표에서 빠진 사진을 지운다 — 사진이 바뀔 때마다 옛것이 쌓이지 않게. 지운 장 수.
static int cachePrune(const char imgs[][IMG_MAX], int n) {
  static char drop[16][IMG_MAX];
  int total = 0;
  for (;;) {                                // 훑는 중에는 지우지 않는다 — 모아서 지우고 다시 훑는다
    int k = 0;
    File dir = LittleFS.open("/p");
    for (File f = dir.openNextFile(); f && k < 16; f = dir.openNextFile()) {
      const char *nm = f.name();
      bool keep = false;
      for (int i = 0; i < n && !keep; i++) keep = !strcmp(imgs[i], nm);
      if (!keep) strlcpy(drop[k++], nm, IMG_MAX);
      f.close();
      vTaskDelay(1);                        // 0번 심의 IDLE 에 틈을 준다 — 안 주면 사진이 많을 때 워치독에 걸린다
    }
    dir.close();
    int gone = 0;
    for (int i = 0; i < k; i++) {
      char path[80];
      if (cachePath(drop[i], path, sizeof(path)) && LittleFS.remove(path)) gone++;
    }
    total += gone;
    if (k < 16 || !gone) return total;      // 다 훑었다 — 또는 지우지 못해 같은 것만 다시 나온다
  }
}

// 일꾼이 들고 있는 지금 묶음 — 사진마다 img 이름과 버퍼. 다음에 받을 때 이름이 같으면 버퍼를
// 그대로 다시 쓴다(img 에 사진 해시가 들어 있어 이름이 같으면 같은 사진이다).
static ImgNames &curImg = psram<ImgNames>();
static uint16_t *curPtr[PHOTO_MAX];
static int       curCount = 0;

static WiFiClientSecure scli;               // 연결 하나를 들고 쓴다 — 요청마다 TLS 를 새로 맺으면 0.5초 넘게 더 든다
static HTTPClient http;

// 이름표 줄 — 알림의 uid 로 그 아이의 사진을 찾는다. 서버는 카드가 있으면 NFC UID 를,
// 없으면 학번을 uid 칸에 실어 보내므로(talentFeed 의 showLog) 둘 다 들고 있는다.
struct RosterRow { char uid[24]; char sid[16]; char img[IMG_MAX]; };
static RosterRow (&rows)[ROW_MAX] = psram<RosterRow[ROW_MAX]>();
static int rowCount = 0;

// 기다리는 알림(시리얼 't' 가 쌓고 일꾼 고리에서 푼다 — 같은 일꾼이라 잠금이 필요 없다)
struct Pending { char uid[24]; char name[48]; char note[48]; char category[32]; char reason[128]; };
static Pending pend[CARD_QUEUE];
static int pendHead = 0, pendCount = 0;

// 사진 목록을 받아 겹친 것을 뺀 img 이름들을 imgs 에 담는다. 못 받으면 -1.
// ?all=1 — 카드를 아직 안 받은 아이까지 명단 전부(그런 줄은 uid 가 비어 온다).
// 사진이 없는 아이는 서버가 이름 그림(nm-…)을 준다.
static int fetchRoster(char imgs[][IMG_MAX]) {
  char url[96];
  snprintf(url, sizeof(url), "%s/api/talent/roster?px=%d&all=1", TALENT_SERVER, PHOTO_PX);
  http.begin(scli, url);
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[목록] 실패 code=%d\n", code); return -1; }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) { Serial.printf("[목록] 해석 실패: %s\n", err.c_str()); return -1; }

  int n = 0;
  rowCount = 0;
  for (JsonObject r : doc["data"].as<JsonArray>()) {
    const char *img = r["img"] | "";
    if (rowCount < ROW_MAX && strlen(img) < IMG_MAX) {
      RosterRow &w = rows[rowCount++];
      strlcpy(w.uid, r["uid"] | "", sizeof(w.uid));
      strlcpy(w.sid, r["sid"] | "", sizeof(w.sid));
      strlcpy(w.img, img, sizeof(w.img));
    }
    if (!img[0] || strlen(img) >= IMG_MAX || n >= PHOTO_MAX) continue;
    // 같은 사진(= 같은 사람의 다른 키링)은 건너뛴다
    bool dup = false;
    for (int k = 0; k < n && !dup; k++) dup = !strcmp(imgs[k], img);
    if (!dup) strcpy(imgs[n++], img);
  }
  return n;
}

// 이름표를 다시 받아 바뀐 사진만 새로 받고, 달라졌으면 loop 에 새 묶음을 건넨다. 목록을 못 받으면 false.
// 순서는 이름표 차례 그대로라, 한 아이의 사진이 바뀌면 그 아이의 칸에서만 갈린다.
static bool refreshPhotos() {
  static ImgNames &imgs = psram<ImgNames>();
  const int n = fetchRoster(imgs);
  if (n < 0) return false;

  static ImgNames &nxtImg = psram<ImgNames>();
  static uint16_t *nxtPtr[PHOTO_MAX];
  const bool useFlash = fsOk;               // 켤 때 한 번, 아직 검은 화면이다 — 위 '사진 보관' 머리말
  int m = 0, fresh = 0, kept = 0;
  for (int i = 0; i < n; i++) {
    uint16_t *buf = nullptr;
    for (int k = 0; k < curCount && !buf; k++)
      if (!strcmp(curImg[k], imgs[i])) buf = curPtr[k];          // 이미 들고 있다 — 받지 않는다
    if (!buf) {
      vTaskDelay(1);                                               // 위 cachePrune 과 같은 까닭
      buf = (uint16_t *)ps_malloc(PHOTO_BYTES);
      if (!buf) { Serial.println("[사진] PSRAM 이 모자라 여기까지만 받는다"); break; }
      if (useFlash && cacheLoad(imgs[i], buf)) {
        kept++;                                                    // 두었던 것 — 받지 않는다
      } else {
        if (!fetchPhoto(http, scli, imgs[i], buf)) { free(buf); continue; }
        fresh++;
        Serial.printf("[사진] 새로 받음 %s\n", imgs[i]);
        if (useFlash) cacheSave(imgs[i], buf);
      }
    }
    strcpy(nxtImg[m], imgs[i]);
    nxtPtr[m++] = buf;
  }
  if (useFlash) {
    const int dropped = cachePrune(imgs, n);
    Serial.printf("[보관] 플래시에서 %d장 · 새로 둔 것 %d장 · 지운 것 %d장\n", kept, fresh, dropped);
  }

  bool same = (m == curCount);
  for (int i = 0; i < m && same; i++) same = (nxtPtr[i] == curPtr[i]);
  if (same) { Serial.printf("[목록] %d장 — 바뀐 것 없음\n", m); return true; }

  // loop 에 건넨다 → 옮겨 갈 때까지 기다린다
  memcpy(nextPtrs, nxtPtr, sizeof(nxtPtr[0]) * m);
  memcpy(nextImg, nxtImg, sizeof(nxtImg[0]) * m);
  nextCount = m;
  swapReady = true;
  while (swapReady) vTaskDelay(pdMS_TO_TICKS(5));

  // 이제 화면에서 안 쓰는 옛 버퍼를 푼다
  int gone = 0;
  for (int k = 0; k < curCount; k++) {
    bool kept = false;
    for (int i = 0; i < m && !kept; i++) kept = (curPtr[k] == nxtPtr[i]);
    if (!kept) { free(curPtr[k]); gone++; }
  }
  memcpy(curImg, nxtImg, sizeof(nxtImg[0]) * m);
  memcpy(curPtr, nxtPtr, sizeof(nxtPtr[0]) * m);
  curCount = m;
  Serial.printf("[목록] %d장 — 새로 %d장 · 빠짐 %d장\n", m, fresh, gone);
  return true;
}

// 기다리는 알림 하나를 꺼내 loop 에 건넨다.
// note 는 서버가 짓는 한 줄이다 — '+5P 출석' · '-3P 간식' · '+5P 되돌림'(talentFeed 의 showLog).
// 몇 포인트인지는 그 맨 앞 숫자에서 읽는다(points 칸은 바뀐 뒤의 잔액이다).
static void prepareCard() {
  const Pending q = pend[pendHead];
  pendHead = (pendHead + 1) % CARD_QUEUE;
  pendCount--;

  Card c = {};
  strlcpy(c.name, q.name, sizeof(c.name));
  // 사유 줄 — 분류와 사유를 '·' 로 잇는다. 둘 중 빈 것은 뺀다.
  // 옛 서버(분류·사유 칸이 없다)면 note 의 '+5P ' 뒤 글자(사유 8자)를 대신 쓴다.
  if (q.category[0] || q.reason[0])
    snprintf(c.why, sizeof(c.why), "%s%s%s", q.category, (q.category[0] && q.reason[0]) ? " · " : "", q.reason);
  else if (const char *sp = strchr(q.note, ' '))
    strlcpy(c.why, sp + 1, sizeof(c.why));
  c.delta = atoi(q.note);                                   // '+5P …' → 5, '-3P …' → -3
  c.undo  = strstr(q.note, "되돌림") != nullptr;
  if (!c.delta && !c.undo) return;                          // 숫자가 없는 알림은 띄울 것이 없다

  // 사진 — 이름표에서 그 아이를 찾아, 바둑판에 깔려 있는 48 을 쓴다.
  // 받아 둔 64장에 못 든 아이는 사진 없이 이름·포인트만 띄운다.
  const RosterRow *r = nullptr;
  for (int i = 0; i < rowCount && !r; i++)
    if (q.uid[0] && (!strcmp(rows[i].uid, q.uid) || !strcmp(rows[i].sid, q.uid))) r = &rows[i];
  const uint16_t *small = nullptr;
  for (int k = 0; r && k < curCount && !small; k++)
    if (!strcmp(curImg[k], r->img)) small = curPtr[k];

  // loop 가 앞 알림을 다 그리고 최소 시간이 지날 때까지 기다린다 — 그리는 중인 사진을 덮지 않게
  while (cardReady || (int32_t)(millis() - cardFreeAt) < 0) vTaskDelay(pdMS_TO_TICKS(10));

  c.hasPhoto = small != nullptr;
  if (c.hasPhoto) memcpy(cardPhoto, small, PHOTO_BYTES);
  cardNext = c;
  cardReady = true;
  Serial.printf("[알림] %s %+dP%s — 사진 %s\n", c.name, c.delta, c.undo ? " 되돌림" : "",
                c.hasPhoto ? "있음" : (r ? "받아 둔 것에 없음" : "이름표에 없음"));
}

// 알림판을 받아 loop 에 건넨다. 켤 때 한 번, 화면이 아직 검을 때 부른다. 받으면 true.
static bool fetchBoard() {
  char url[96];
  snprintf(url, sizeof(url), "%s/api/talent/signage?px=%d", TALENT_SERVER, PHOTO_PX);
  http.begin(scli, url);
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[알림판] 실패 code=%d\n", code); return false; }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) { Serial.printf("[알림판] 해석 실패: %s\n", err.c_str()); return false; }
  // 이 창구를 모르는 옛 서버는 'signage' 를 학번으로 읽어(GET /:sid) 200 을 준다 — date 가 없으면 아니다
  if (!doc["date"].is<const char *>()) { Serial.println("[알림판] 서버에 이 창구가 없다(배포 전?)"); return false; }

  while (boardReady) vTaskDelay(pdMS_TO_TICKS(5));      // 앞엣것을 loop 가 아직 안 가져갔다
  Board &b = boardNext;
  auto fill = [](JsonArray arr, BoardRow *out, int cap, const char *valueKey) {
    int n = 0;
    for (JsonObject r : arr) {
      if (n >= cap) break;
      BoardRow &w = out[n++];
      strlcpy(w.name, r["name"] | "", sizeof(w.name));
      strlcpy(w.img,  r["img"]  | "", sizeof(w.img));
      w.rank  = r["rank"] | 0;
      w.value = valueKey ? (r[valueKey] | 0) : 0;
    }
    return n;
  };
  b.todayN  = fill(doc["today"].as<JsonArray>(),  b.today,  BOARD_TODAY_MAX, nullptr);
  b.pointsN = fill(doc["points"].as<JsonArray>(), b.points, BOARD_RANK_ROWS, "balance");
  b.monthN  = fill(doc["month"].as<JsonArray>(),  b.month,  BOARD_RANK_ROWS, "count");
  const char *date = doc["date"] | "";                  // 'YYYY-MM-DD'
  b.mon = strlen(date) >= 7 ? atoi(date + 5) : 0;
  b.ok = true;
  boardReady = true;
  Serial.printf("[알림판] 오늘 %d명 · 포인트 %d줄 · %d월 출석 %d줄\n", b.todayN, b.pointsN, b.mon, b.monthN);
  return true;
}

// 다 받았다 — 와이파이는 붙여 둔 채, TLS 연결과 사진 보관(LittleFS)만 닫는다.
// TLS 를 쥐고 있으면 내부 RAM 을 40KB 쯤 계속 먹고, 다음 바퀴까지 40초 가까이 놀다 서버가 끊는다.
// 다음 화면은 플래시·네트워크가 가라앉은 뒤에 그린다 — 받자마자 그리면 24MHz 에서 첫 홈만 밀렸다
// (다음 장부터는 멀쩡했다, 2026-11-04).
static void fetchSettle() {
  http.end();
  scli.stop();
  if (fsOk) LittleFS.end();
  vTaskDelay(pdMS_TO_TICKS(500));
  Serial.println("[와이파이] 다 받았다 — 붙인 채 둔다");
}

// 한 바퀴 돈 뒤 다시 받기 — 이미 들고 있는 사진은 받지 않고(refreshPhotos), 바뀐 것만 받는다.
// 와이파이가 그사이 끊겼으면 다시 붙어 본다.
static void refreshCycle() {
  const uint32_t t0 = millis();
  fsOk = LittleFS.begin(false) && LittleFS.exists("/p");
  bool up = WiFi.status() == WL_CONNECTED;
  for (int i = 0; i < REFRESH_JOIN_TRIES && !up; i++) {
    Serial.println("[와이파이] 끊겨 있다 — 다시 붙는다");
    if (!(up = wifiJoin())) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  if (up) {
    if (!refreshPhotos()) Serial.println("[목록] 못 받았다 — 들고 있던 것으로 한 바퀴 더");
    for (int i = 0; i < BOARD_TRIES && !fetchBoard(); i++) vTaskDelay(pdMS_TO_TICKS(1000));
  } else {
    Serial.println("[와이파이] 못 붙었다 — 들고 있던 것으로 한 바퀴 더");
  }
  fetchSettle();
  Serial.printf("[다시 받기] %lums\n", (unsigned long)(millis() - t0));
}

static void fetchTask(void *) {
  // 와이파이 설정을 플래시에 적지 않는다 — 적는 동안 캐시가 꺼져 화면이 한 줄씩 밀린다(single 과 같은 까닭)
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  // 끊긴 까닭을 적어 둔다 — 설정 모드가 '비밀번호가 맞지 않는 것 같습니다' 처럼 사람 말로 바꾼다
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiLastReason = info.wifi_sta_disconnected.reason;
    wifiStaBusy    = false;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  credsLoad();
  // 처음이면 포맷한다 — 화면이 아직 검을 때라 플래시를 만져도 괜찮다
  fsOk = LittleFS.begin(true) && (LittleFS.exists("/p") || LittleFS.mkdir("/p"));
  if (!fsOk) Serial.println("[보관] LittleFS 를 못 열었다 — 켤 때마다 사진을 다 받는다");

  for (;;) {
    if (portalWanted) portalRun(portalWanted);        // 돌아오지 않는다(다시 세운다)
    if (wifiJoin()) break;
    if (portalWanted) continue;
    if (!wifiEverUp && wifiNoApRounds >= PORTAL_AUTO_ROUNDS) portalRun("아는 와이파이가 보이지 않는다");
    // 5초 쉬었다 다시 — 그 사이에도 BOOT 단추 · 시리얼 'w' 를 듣는다
    for (int i = 0; i < 50 && !portalWanted; i++) {
      if (bootHeld()) portalWanted = "켤 때 BOOT 단추를 누르고 있었다";
      if (Serial.available() && Serial.read() == 'w') portalWanted = "시리얼 명령";
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  scli.setInsecure();
  http.setReuse(true);

  // 켤 때 — 이름표·사진, 그다음 알림판. 그동안 화면은 검다(loop 는 netDone 을 기다린다).
  // 이름표는 받을 때까지 다시 해 본다(보여 줄 것이 없다).
  while (!refreshPhotos()) {
    Serial.println("[목록] 못 받았다 — 잠시 뒤 다시");
    vTaskDelay(pdMS_TO_TICKS(FETCH_RETRY_MS));
  }
  for (int i = 0; i < BOARD_TRIES && !fetchBoard(); i++) vTaskDelay(pdMS_TO_TICKS(1000));
  fetchSettle();
  netDone = true;

  for (;;) {
    // 한 바퀴(홈 → 오늘 → 포인트 → 이번 달)를 다 돌면 loop 가 netWant 를 세운다 — 다시 붙어 받고 끈다.
    // 그동안 loop 는 마지막 장을 그대로 둔 채 아무것도 그리지 않는다. 못 붙거나 못 받으면 들고 있던
    // 것으로 한 바퀴 더 돈다(켤 때와 달리 붙을 때까지 매달리지 않는다).
    if (netWant) {
      netWant = false;
      refreshCycle();
      netDone = true;
    }
    // 시리얼 't' — 알림 화면을 본다. 이름표의 아이를 차례로 지급·사용 번갈아 띄운다.
    // 시리얼 'b' — 일괄 명단 화면을 본다(이름 열다섯 · 마흔을 번갈아 — 넘치면 '외 N명').
    const int ch = Serial.available() ? Serial.read() : -1;
    // 설정 모드 — 시리얼 'w' 또는 켠 지 얼마 안 돼 BOOT 단추(돌아오지 않는다).
    if (ch == 'w' || bootHeld()) portalRun(ch == 'w' ? "시리얼 명령" : "켤 때 BOOT 단추를 누르고 있었다");
    if (ch == 'W') credsClear();                // 폰으로 넣은 와이파이 지우기 — 다시 세우면 반영된다
    if (ch == 'b') {
      static int round = 0;
      Bulk &k = bulkPend;
      k.delta = (round % 2) ? -10 : 5;
      strlcpy(k.why, (round % 2) ? "간식 교환 · 수동 조정" : "특별 미션 · 여름성경학교 참석", sizeof(k.why));
      static const char *SAMPLE[] = { "강윤비", "임하준", "길도형", "최빛나", "방홀리", "박승태", "심아영", "임선경" };
      k.n = (round % 2) ? BULK_NAMES_MAX : 15;
      for (int i = 0; i < k.n; i++) strlcpy(k.names[i], SAMPLE[i % 8], sizeof(k.names[0]));
      bulkPendHas = true;
      round++;
    }
    if (ch == 't' && rowCount && pendCount < CARD_QUEUE) {
      static int k = 0;
      const RosterRow &r = rows[k % rowCount];
      Pending &q = pend[(pendHead + pendCount++) % CARD_QUEUE];
      strlcpy(q.uid, r.uid[0] ? r.uid : r.sid, sizeof(q.uid));
      strlcpy(q.name, "시험", sizeof(q.name));
      strlcpy(q.note, (k % 2) ? "-3P 시험" : "+5P 시험", sizeof(q.note));
      strlcpy(q.category, (k % 2) ? "간식" : "말씀 활동", sizeof(q.category));
      strlcpy(q.reason, (k % 2) ? "과자 하나" : "요절 암송을 끝까지 다 해냈어요", sizeof(q.reason));
      k++;
    }
    if (pendCount) prepareCard();
    if (bulkPendHas && !cardReady && !bulkReady && (int32_t)(millis() - cardFreeAt) >= 0) {
      memcpy(&bulkNext, &bulkPend, sizeof(Bulk));     // 차례가 됐다 — loop 에 건넨다
      bulkPendHas = false;
      bulkReady = true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 칸이 원형 화면 안에 통째로 드나 — 가운데에서 가장 먼 모서리가 반지름(240) 안이면 된다
static bool cellInside(int x0, int y0) {
  const int dx = max(abs(x0 - 240), abs(x0 + PHOTO_PX - 240));
  const int dy = max(abs(y0 - 240), abs(y0 + PHOTO_PX - 240));
  return dx * dx + dy * dy <= 240 * 240;
}

// 원 안의 칸을 놓을 차례대로 늘어놓은 것(칸 번호 k = 줄 × GRID + 칸). 처음 그릴 때 한 번 만든다.
// 칸 가운데가 화면 가운데에 가까운 것부터 — 거리가 같은 칸끼리는 12시에서 시계 방향으로.
// 10×10 은 화면 가운데가 칸 경계라 가운데 네 칸이 첫 고리가 된다.
static int cellOrder[GRID * GRID];
static int cellCount = 0;

static void buildCellOrder() {
  for (int k = 0; k < GRID * GRID; k++)
    if (cellInside((k % GRID) * PHOTO_PX, (k / GRID) * PHOTO_PX)) cellOrder[cellCount++] = k;

  auto dist2 = [](int k) {
    const int dx = (k % GRID) * PHOTO_PX + PHOTO_PX / 2 - 240;
    const int dy = (k / GRID) * PHOTO_PX + PHOTO_PX / 2 - 240;
    return dx * dx + dy * dy;
  };
  auto angle = [](int k) {                       // 12시가 0, 시계 방향으로 커진다(화면은 y 가 아래로 는다)
    const float dx = (k % GRID) * PHOTO_PX + PHOTO_PX / 2 - 240;
    const float dy = (k / GRID) * PHOTO_PX + PHOTO_PX / 2 - 240;
    const float a = atan2f(dx, -dy);
    return a < 0 ? a + 2 * PI : a;
  };
  std::sort(cellOrder, cellOrder + cellCount, [&](int a, int b) {
    const int da = dist2(a), db = dist2(b);
    return da != db ? da < db : angle(a) < angle(b);
  });
}

// ── 나눠 그리기 ───────────────────────────────────────────────────
// 화면은 PSRAM 의 그림을 쉬지 않고 퍼내 패널로 보낸다(초당 약 40MB). 그 사이 화면 전체를 한 번에
// 지우면(460KB), 와이파이로 사진을 받는 중일 때 PSRAM 이 모자라
// 패널로 가는 줄이 늦어 화면이 자글거렸다(알림을 연달아 띄울 때). 그래서 큰 쓰기를 띠 몇 줄씩
// 끊어 쓰고 사이마다 1ms 쉰다 — 다 그리는 데 몇십 ms 늘 뿐 눈에는 한 번에 바뀐 것처럼 보인다.
#define PACE_ROWS 32

static void clearPaced() {
  for (int y = 0; y < 480; y += PACE_ROWS) {
    gfx->fillRect(0, y, 480, PACE_ROWS, COL_BG);
    delay(1);
  }
}

// 바둑판 — 가운데 칸부터 바깥쪽으로 사진을 놓는다. 칸이 모자라면 남은 사진은 놓지 않는다.
// 화면을 지우는 일까지 함께 한다(사진이 없는 자리는 바탕색).
// ★ 칸마다 그리지 않고 **한 줄(480px)씩** 내부 RAM 에서 맞춰 차례로 쓴다. 칸마다 그리면 한 장이 960B 씩
//   건너뛴 48줄로 흩어져, 64장이면 흩어진 쓰기가 3천 번 넘게 몰리고 지우기(460KB)도 따로 한다 —
//   네트워크가 쉬고 있는데도 홈을 그리는 순간 화면이 위로 10px 쯤 튀었다가 돌아왔다(2026-11-03).
//   이어진 줄로 쓰면 쓰는 양은 절반, 쓰기는 차례대로다(순위 표를 덩어리로 옮기는 것은 괜찮았다).
static void drawGrid(int count) {
  if (!cellCount) buildCellOrder();
  static int8_t   slot[GRID * GRID];             // 칸마다 놓을 사진 번호(-1 = 비움)
  static uint16_t line[GRID * PHOTO_PX];         // 한 줄 — 내부 RAM
  memset(slot, -1, sizeof(slot));
  for (int j = 0; j < cellCount && j < count; j++) slot[cellOrder[j]] = j;
  for (int y = 0; y < GRID * PHOTO_PX; y++) {
    const int8_t *s = slot + (y / PHOTO_PX) * GRID;
    const int py = y % PHOTO_PX;
    for (int c = 0; c < GRID; c++) {
      uint16_t *dst = line + c * PHOTO_PX;
      if (s[c] >= 0) memcpy(dst, shown[s[c]] + py * PHOTO_PX, PHOTO_PX * 2);
      else           std::fill(dst, dst + PHOTO_PX, (uint16_t)COL_BG);
    }
    gfx->draw16bitRGBBitmap(0, y, line, GRID * PHOTO_PX, 1);
    if (y % PACE_ROWS == PACE_ROWS - 1) delay(1);   // 위 '나눠 그리기'
  }
}

// 굴림 16px 을 scale 배로 키워 둔다. 글자 폭을 재거나 그리기 전에 부른다.
static void setTextFont(int scale) {
  gfx->setFont((const uint8_t *)FONT_KO);
  gfx->setUTF8Print(true);
  gfx->setTextSize(scale);
}

// 글자 한 줄을 왼쪽 x 부터, 세로 가운데 cy 에 그린다. g 를 주면 화면 대신 그 캔버스에.
static void drawTextAt(const char *s, int x, int cy, int scale, uint16_t color, Arduino_GFX *g = nullptr) {
  setTextFont(scale);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);       // 재는 것은 화면 쪽으로 — 폰트가 같다
  if (!g) g = gfx;
  g->setFont((const uint8_t *)FONT_KO);
  g->setUTF8Print(true);
  g->setTextSize(scale);
  g->setTextColor(color);
  g->setCursor(x - x1, cy - (int)h / 2 - y1);
  g->print(s);
}

static int textWidth(const char *s, int scale) {
  setTextFont(scale);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// 원형 화면에서 y0..y1 줄의 왼쪽·오른쪽 끝(가장 좁은 줄 기준).
static int halfWidthAt(int y0, int y1) {
  const int dy = max(abs(y0 - 240), abs(y1 - 240));
  return dy >= 240 ? 0 : (int)sqrtf((float)(240 * 240 - dy * dy));
}

// s 를 room 안에 들게 맞춘다 — 두 배 글자로 놓고, 안 들어가면 한 배로, 그래도 길면 뒤를
// 잘라 '..' 를 붙인다(UTF-8 글자 단위로 뗀다). 고른 배수를 돌려준다.
static int fitText(char *s, size_t cap, int room) {
  const int scale = textWidth(s, 2) <= room ? 2 : 1;
  while (textWidth(s, scale) > room) {
    size_t n = strlen(s);
    if (n >= 2 && !strcmp(s + n - 2, "..")) n -= 2;     // 붙여 둔 '..' 를 떼고
    if (!n) break;
    do { n--; } while (n && (s[n] & 0xC0) == 0x80);     // 글자 하나를 통째로 뗀다
    s[n] = '\0';
    strlcat(s, "..", cap);
  }
  return scale;
}

// 알림 한 장 — 윗줄에 [사진 48] 이름, 아랫줄에 '지급 +5P' / '사용 -3P'. 줄마다 가운데에 놓는다.
// 글자를 3배(48px)로 키워 사진과 높이가 같다.
static void drawCard(const Card &c) {
  clearPaced();

  const char *name = c.name[0] ? c.name : "(이름 없음)";
  const int nameW = textWidth(name, 3);
  const int rowW  = (c.hasPhoto ? PHOTO_PX + CARD_GAP : 0) + nameW;
  int x = 240 - rowW / 2;
  const int nameCy = 200;
  if (c.hasPhoto) {
    gfx->draw16bitRGBBitmap(x, nameCy - PHOTO_PX / 2, cardPhoto, PHOTO_PX, PHOTO_PX);
    x += PHOTO_PX + CARD_GAP;
  }
  drawTextAt(name, x, nameCy, 3, COL_TEXT);

  char line[32];
  const char *what = c.undo ? "되돌림" : (c.delta > 0 ? "지급" : "사용");
  snprintf(line, sizeof(line), "%s %+dP", what, c.delta);
  drawTextAt(line, 240 - textWidth(line, 3) / 2, 290, 3, c.undo ? COL_UNDO : (c.delta > 0 ? COL_GIVE : COL_USE));

  // 사유 — 원 안에 들어가게 줄여 가운데에
  if (c.why[0]) {
    const int cy = 365;
    char s[sizeof(c.why) + 4];
    strlcpy(s, c.why, sizeof(s));
    const int scale = fitText(s, sizeof(s), 2 * halfWidthAt(cy - 16, cy + 16) - 40);
    drawTextAt(s, 240 - textWidth(s, scale) / 2, cy, scale, COL_DIM);
  }
}

static void drawTitle(const char *s, int cy = TITLE_CY);   // 아래 '대기 화면의 세 장' 에 있다

// 일괄 명단 — 위에 '일괄 지급 +5P · 12명', 그 아래 사유, 그 아래로 이름을 줄마다 원 폭에 맞춰
// 두세 칸씩 깐다. 칸이 모자라면 마지막 칸에 '외 N명'.
static void drawBulk(const Bulk &k) {
  clearPaced();
  char t[64];
  snprintf(t, sizeof(t), "일괄 %s %+dP · %d명", k.delta > 0 ? "지급" : "사용", k.delta, k.n);
  int scale = fitText(t, sizeof(t), 2 * halfWidthAt(TITLE_CY - 16, TITLE_CY + 16) - 30);
  drawTextAt(t, 240 - textWidth(t, scale) / 2, TITLE_CY, scale, k.delta > 0 ? COL_GIVE : COL_USE);
  if (k.why[0]) {
    char w[sizeof(k.why) + 4];
    strlcpy(w, k.why, sizeof(w));
    scale = fitText(w, sizeof(w), 2 * halfWidthAt(118 - 16, 118 + 16) - 40);
    drawTextAt(w, 240 - textWidth(w, scale) / 2, 118, scale, COL_DIM);
  }

  // 칸을 먼저 세어 본다 — 다 못 들면 마지막 칸을 '외 N명' 에 내준다
  struct Row { int cy, cols; };
  Row rowsAt[12]; int nRows = 0, cells = 0;
  for (int cy = 170; cy + 16 <= 440 && nRows < 12; cy += 40) {
    const int cols = min(3, (2 * halfWidthAt(cy - 16, cy + 16) - 30) / BULK_COL_W);
    if (cols <= 0) break;
    rowsAt[nRows++] = { cy, cols };
    cells += cols;
  }
  const int shownN = k.n <= cells ? k.n : cells - 1;
  int idx = 0;
  for (int r = 0; r < nRows && idx < k.n; r++) {
    const int x0 = 240 - rowsAt[r].cols * BULK_COL_W / 2;
    for (int c = 0; c < rowsAt[r].cols && idx < k.n; c++) {
      char nm[40];
      if (idx < shownN) strlcpy(nm, k.names[idx], sizeof(nm));
      else              snprintf(nm, sizeof(nm), "외 %d명", k.n - shownN);
      const bool more = idx >= shownN;
      const int sc = fitText(nm, sizeof(nm), BULK_COL_W - 8);
      drawTextAt(nm, x0 + c * BULK_COL_W + (BULK_COL_W - textWidth(nm, sc)) / 2, rowsAt[r].cy, sc, more ? COL_DIM : COL_TEXT);
      idx = more ? k.n : idx + 1;
    }
    delay(1);
  }
}

// ── 대기 화면의 세 장 ─────────────────────────────────────────────
// 받아 둔 48 가운데 img 이름이 같은 것. 없으면 nullptr(64장에 못 든 아이) — 사진 자리를 비워 둔다.
static const uint16_t *photoOf(const char *img) {
  if (!img || !img[0]) return nullptr;
  for (int i = 0; i < shownCount; i++)
    if (!strcmp(shownImg[i], img)) return shown[i];
  return nullptr;
}

static void drawTitle(const char *s, int cy) {
  drawTextAt(s, 240 - textWidth(s, 2) / 2, cy, 2, COL_TEXT);
}

// 오늘 출석 — 제목 아래로 사진을 가운데부터 깐다(홈과 같은 차례). 제목 줄에 걸리는 칸은 건너뛴다.
static void drawTodayPage(const Board &b) {
  clearPaced();
  char title[40];
  snprintf(title, sizeof(title), "오늘 출석 %d명", b.todayN);
  drawTitle(title);
  if (!b.todayN) {
    const char *msg = "아직 아무도 없어요";
    drawTextAt(msg, 240 - textWidth(msg, 2) / 2, 260, 2, COL_DIM);
    return;
  }
  if (!cellCount) buildCellOrder();
  int j = 0;
  for (int i = 0; i < cellCount && j < b.todayN; i++) {
    const int k = cellOrder[i];
    const int x = (k % GRID) * PHOTO_PX, y = (k / GRID) * PHOTO_PX;
    if (y < TITLE_CY + 26) continue;                     // 제목 줄과 겹친다
    const uint16_t *px = photoOf(b.today[j++].img);
    if (px) gfx->draw16bitRGBBitmap(x, y, (uint16_t *)px, PHOTO_PX, PHOTO_PX);
    if (j % 4 == 0) delay(1);                            // 나눠 그리기 — 위 머리말
  }
}

// ── 순위 표 ───────────────────────────────────────────────────────
static Arduino_Canvas *rankCanvas = nullptr;   // TABLE_W × (스무 줄) — 처음 쓸 때 PSRAM 에 잡는다
static int rankContentH = 0;                   // 이번 표의 실제 높이(줄 수 × ROW_H)
static int rankScrolled = -1;                  // 지금 창에 보이는 표의 위치(px)

static void blitRank(int off) {
  const int h = min(WIN_H, rankContentH - off);
  if (h <= 0) return;
  gfx->draw16bitRGBBitmap(TABLE_X, WIN_Y, rankCanvas->getFramebuffer() + off * TABLE_W, TABLE_W, h);
  if (h < WIN_H) gfx->fillRect(TABLE_X, WIN_Y + h, TABLE_W, WIN_H - h, COL_BG);   // 표 끝 아래(RANK_TAIL)를 비운다
  rankScrolled = off;
}

// 굴릴 거리 — 마지막 줄이 원 안(y=480-RANK_TAIL)까지 올라올 만큼. 0 이하면 굴릴 것이 없다.
static int rankTravel() {
  return rankContentH - WIN_H + RANK_TAIL;
}

// 이 장을 띄우는 시간 — 굴릴 것이 없으면(줄이 적다) 다른 장과 같다.
static uint32_t rankPageMs() {
  const int travel = rankTravel();
  if (travel <= 0) return PAGE_MS;
  return 2 * RANK_HOLD_MS + (uint32_t)travel * 1000 / RANK_SPEED;
}

// 굴리기 한 걸음 — loop 가 RANK_FRAME_MS 마다 부른다. 자리가 바뀌었을 때만 옮겨 그린다.
static void scrollRank(uint32_t elapsed) {
  const int travel = rankTravel();
  if (travel <= 0 || !rankCanvas) return;
  int off = 0;
  if (elapsed > RANK_HOLD_MS) off = min(travel, (int)((elapsed - RANK_HOLD_MS) * RANK_SPEED / 1000));
  if (off != rankScrolled) blitRank(off);
}

// 순위 한 장 — 표 머리('순위 · 이름 · 포인트')는 화면에 박아 두고, 줄들은 캔버스에 그려 굴린다.
// 줄: [등수] [사진] 이름 ······ 값. 1·2·3등은 금·은·동.
static void drawRankPage(const char *title, const BoardRow *rows, int n, const char *unit,
                         const char *valueHead, uint16_t valueColor) {
  clearPaced();
  drawTitle(title, RANK_TITLE_CY);
  if (!n) {
    const char *msg = "아직 기록이 없어요";
    drawTextAt(msg, 240 - textWidth(msg, 2) / 2, 260, 2, COL_DIM);
    rankContentH = 0;
    return;
  }
  if (!rankCanvas) {
    rankCanvas = new Arduino_Canvas(TABLE_W, BOARD_RANK_ROWS * ROW_H, nullptr);
    if (!rankCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) { delete rankCanvas; rankCanvas = nullptr; }
  }
  if (!rankCanvas) { Serial.println("[순위] 캔버스를 못 잡았다(PSRAM)"); return; }

  // 머리 줄
  const int colName = 104, colValueR = TABLE_W - 10;
  drawTextAt("순위", TABLE_X + 4, RANK_HEAD_CY, 1, COL_DIM);
  drawTextAt("이름", TABLE_X + colName, RANK_HEAD_CY, 1, COL_DIM);
  drawTextAt(valueHead, TABLE_X + colValueR - textWidth(valueHead, 1), RANK_HEAD_CY, 1, COL_DIM);
  gfx->drawFastHLine(TABLE_X, WIN_Y - 6, TABLE_W, COL_RULE);

  // 줄들 — 캔버스에
  Arduino_Canvas *c = rankCanvas;
  c->fillScreen(COL_BG);
  for (int i = 0; i < n; i++) {
    const BoardRow &r = rows[i];
    const int y = i * ROW_H, cy = y + ROW_H / 2;
    if (i % 2) c->fillRect(0, y, TABLE_W, ROW_H, COL_ZEBRA);
    const uint16_t rc = r.rank == 1 ? COL_GOLD : r.rank == 2 ? COL_SILVER : r.rank == 3 ? COL_BRONZE : COL_DIM;

    char rank[8];
    snprintf(rank, sizeof(rank), "%d", r.rank);
    drawTextAt(rank, 24 - textWidth(rank, 2) / 2, cy, 2, rc, c);
    const uint16_t *px = photoOf(r.img);
    if (px) c->draw16bitRGBBitmap(46, y + (ROW_H - PHOTO_PX) / 2, (uint16_t *)px, PHOTO_PX, PHOTO_PX);

    char val[16];
    snprintf(val, sizeof(val), "%d%s", r.value, unit);
    const int valW = textWidth(val, 2);
    drawTextAt(val, colValueR - valW, cy, 2, valueColor, c);

    char nm[sizeof(r.name) + 4];
    strlcpy(nm, r.name, sizeof(nm));
    const int sc = fitText(nm, sizeof(nm), colValueR - valW - 10 - colName);
    drawTextAt(nm, colName, cy, sc, r.rank <= 3 ? rc : COL_TEXT, c);
  }
  rankContentH = n * ROW_H;
  rankScrolled = -1;
  blitRank(0);
}

// ── 설정 모드 화면 ─────────────────────────────────────────────────
// QR 을 그린다. 바깥에 흰 여백(quiet zone) 네 조각을 꼭 둔다 — 없으면 카메라가 못 읽는다.
// 글자 수에 맞는 가장 작은 판을 고른다(작을수록 조각이 커서 멀리서도 읽힌다).
// ★ 라이브러리가 넘치는 것을 막아 주지 않는다(qrcode.c 에 자리 검사가 없다) — 판마다 담을 수
//   있는 글자 수를 우리가 먼저 재서 고른다(master portal.h 와 같다).
static bool drawQr(const char *text, int cy, int maxPx) {
  #define QR_VMAX 6
  #define QR_SIDE (QR_VMAX * 4 + 17)
  static uint8_t buf[(QR_SIDE * QR_SIDE + 7) / 8];
  static const uint8_t QR_CAP[QR_VMAX + 1] = { 0, 17, 32, 53, 78, 106, 134 };   // ECC_LOW · 바이트

  const size_t len = strlen(text);
  uint8_t ver = 0;
  for (uint8_t v = 2; v <= QR_VMAX; v++) if (len <= QR_CAP[v]) { ver = v; break; }
  if (!ver) { Serial.printf("[설정] QR 에 넣을 글자가 너무 깁니다(%u자)\n", (unsigned)len); return false; }

  QRCode qr;
  if (qrcode_initText(&qr, buf, ver, ECC_LOW, text) != 0) return false;
  const int n = qr.size + 8;                       // 여백 네 조각씩
  const int px = maxPx / n;
  if (px < 2) return false;
  const int side = n * px;
  const int x0 = 240 - side / 2, y0 = cy - side / 2;
  gfx->fillRect(x0, y0, side, side, 0xFFFF);       // 흰 바탕(여백까지)
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) gfx->fillRect(x0 + (x + 4) * px, y0 + (y + 4) * px, px, px, 0x0000);
  return true;
}

// 원 안에 맞춰 가운데에 한 줄
static void drawFitCentered(const char *s, int cy, uint16_t color) {
  char t[96];
  strlcpy(t, s, sizeof(t));
  const int sc = fitText(t, sizeof(t), 2 * halfWidthAt(cy - 16, cy + 16) - 30);
  drawTextAt(t, 240 - textWidth(t, sc) / 2, cy, sc, color);
}

// 제목 · QR · AP 이름 · 비밀번호 · 지금 사정
static void drawPortal() {
  gfx->fillScreen(COL_BG);
  drawFitCentered("와이파이 설정", 50, COL_GOLD);
  if (portalAp[0]) {
    char qr[180];
    portalQrText(qr, sizeof(qr));
    drawQr(qr, 200, 230);
    drawFitCentered(portalAp, 340, COL_TEXT);
    char pw[80];
    if (PORTAL_AP_PASS[0]) snprintf(pw, sizeof(pw), "비밀번호 %s", PORTAL_AP_PASS);
    else                   snprintf(pw, sizeof(pw), "비밀번호 없음");
    drawFitCentered(pw, 378, COL_TEXT);
  }
  drawFitCentered(portalStatus[0] ? portalStatus : "192.168.4.1", 416, COL_GOLD);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== singleTest — 사진 바둑판 ===");

  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000)) {
    Serial.println("[I2C] 버스 초기화 실패");
    return;
  }
  if (!gfx->begin()) {
    Serial.println("화면 시작 실패 — PSRAM(OPI) 설정을 확인하세요");
    return;
  }

  gfx->fillScreen(COL_BG);

  // 백라이트는 다 그린 뒤에 — 가장 밝은 단계(흔들지 않고 HIGH 만)
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  pinMode(PIN_BOOT, INPUT_PULLUP);          // BOOT 단추 — 켜고 10초 안에 1초 누르면 설정 모드
  xTaskCreatePinnedToCore(fetchTask, "fetch", 16384, nullptr, 1, nullptr, 0);   // https 와 wss 를 함께 든다
}

enum Page { PAGE_HOME, PAGE_TODAY, PAGE_POINTS, PAGE_MONTH, PAGE_COUNT };

static void showHome() {
  drawGrid(shownCount);                     // 지우기도 함께 한다
}

static void showPage(Page p) {
  const Board &b = boardShown;
  char title[32];
  switch (p) {
    case PAGE_HOME:   showHome(); break;
    case PAGE_TODAY:  drawTodayPage(b); break;
    case PAGE_POINTS: drawRankPage("포인트 순위", b.points, b.pointsN, "P", "포인트", COL_GIVE); break;
    case PAGE_MONTH:
      snprintf(title, sizeof(title), "%d월 출석 순위", b.mon);
      drawRankPage(title, b.month, b.monthN, "회", "출석", 0x07FF);
      break;
    default: break;
  }
}

// 남은 메모리를 10초마다 한 줄 — 내부 RAM 은 와이파이·TLS·DMA 가 나눠 쓰는 병목이다.
// TLS 가 붙는지는 '남은 양'이 아니라 '가장 큰 덩어리'가 정한다(송수신 버퍼를 한 번에 잡는다).
static void logHeap(uint32_t now) {
  static uint32_t at = 0;
  if (at && now - at < 10000) return;
  at = now;
  Serial.printf("[메모리] 내부 남음 %u · 최저 %u · 큰덩이 %u | PSRAM 남음 %u · 큰덩이 %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

void loop() {
  logHeap(millis());
  // 설정 모드 — 평소 화면은 멈추고, 일꾼이 '다시 그려라' 할 때만 설정 화면을 그린다
  if (portalOn) {
    if (portalDrawWant) { portalDrawWant = false; drawPortal(); }
    delay(10);
    return;
  }
  static bool     carding = false;          // 알림(또는 일괄 명단)이 떠 있다
  static bool     bulking = false;          // 지금 떠 있는 것이 일괄 명단이다
  static uint32_t cardAt  = 0;
  static Page     page    = PAGE_HOME;
  static uint32_t pageAt  = 0;
  static bool     waitNet = true;           // 받는 중 — 화면은 검은 채로 두고, 다 받고 와이파이를 끄면 홈부터
  static uint32_t netAt   = 0;              // 마지막으로 다 받은 때
  const uint32_t now = millis();

  // 일꾼이 사진 묶음을 건넸으면 옮겨 받기만 한다 — 그리는 것은 다 받고 와이파이를 끈 뒤에.
  if (swapReady) {
    memcpy(shown, nextPtrs, sizeof(nextPtrs[0]) * nextCount);
    memcpy(shownImg, nextImg, sizeof(nextImg[0]) * nextCount);
    shownCount = nextCount;
    swapReady = false;
  }

  if (boardReady) {                         // 알림판 — 켤 때 한 번 온다
    memcpy(&boardShown, &boardNext, sizeof(Board));
    boardReady = false;
  }

  if (waitNet) {
    if (!netDone) return;                   // 아직 받는 중 — 화면은 검은 채로 둔다
    waitNet = false;
    netAt = now;
    page = PAGE_HOME;
    pageAt = now;
    showHome();
    return;
  }

  if (cardReady) {                          // 새 알림은 곧바로 뜬다
    drawCard(cardNext);
    carding = true;
    bulking = false;
    cardAt = now;
    cardFreeAt = now + CARD_MIN_MS;
    cardReady = false;
  }

  if (bulkReady) {                          // 일괄 명단 — 알림과 같은 자리를 쓴다
    drawBulk(bulkNext);
    carding = true;
    bulking = true;
    cardAt = now;
    cardFreeAt = now + CARD_MIN_MS;
    bulkReady = false;
  }

  if (carding) {
    if (now - cardAt < (bulking ? BULK_MS : CARD_MS)) return;
    carding = false;                        // 홈부터 다시 돈다
    bulking = false;
    page = PAGE_HOME;
    pageAt = now;
    showHome();
    return;
  }

  // 순위 장은 굴리고, 그 장이 다 굴러야 넘어간다
  const bool ranking = (page == PAGE_POINTS || page == PAGE_MONTH);
  if (ranking) {
    static uint32_t frameAt = 0;
    if (now - frameAt >= RANK_FRAME_MS) { frameAt = now; scrollRank(now - pageAt); }
  }
  if (now - pageAt < (ranking ? rankPageMs() : PAGE_MS)) return;
  // 알림판을 못 받았으면 홈에 머문다.
  const Page next = boardShown.ok ? (Page)((page + 1) % PAGE_COUNT) : PAGE_HOME;
  pageAt = now;
  // 한 바퀴를 다 돌았다 — 화면을 검게 지우고 다시 받는다. 켤 때와 같은 차례다: 검은 화면에서
  // 다 받은 뒤 홈부터 그린다(받는 동안 그림이 있으면 밀려 보였다).
  if (next == PAGE_HOME && now - netAt >= REFRESH_MIN_MS) {
    clearPaced();
    netDone = false;
    netWant = true;
    waitNet = true;
    return;
  }
  if (next == page) return;                 // 홈에 머문다 — 다시 그리지 않는다
  page = next;
  showPage(page);
}
