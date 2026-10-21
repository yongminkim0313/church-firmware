// funfunFirmware — PN532 NFC 리더(SPI) + GC9A01 원형 디스플레이(SPI), 글자만으로 그리는 판
//
// 보드: ESP32-S3-N16R8 · GC9A01 240×240 원형 IPS(SPI) · PN532(SPI)
// nfcProject/master 의 기능을 이 하드웨어에 옮긴 것이다. 다른 점만 먼저 적는다:
//   · 사진이 없다 — 화면은 **글자만**으로 그린다(microSD 도 없어 사진을 둘 곳도 없다).
//   · 리더가 이 보드에 직접 붙어 있다 — 참고 기기는 리더가 딴 보드라 선(I2C)·ESP-NOW 로
//     받아 왔지만, 여기서는 PN532 를 직접 읽는다. 그 두 길은 넣지 않았다.
//   · 터치가 없다 — 보드의 BOOT 버튼으로 대신한다(짧게: 대기 화면 · 길게: 설정 받기).
//   · 소리가 난다 — DFPlayer Mini(YX5200)가 미리 구워 둔 토막을 이어 읽는다.
//     읽을 수 있는 말과 못 읽는 말, SD 짜임새는 sound.h 머리말에 있다.
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
//   s 최근 출석 목록 · v 소리 토막 하나씩 · r 재부팅 · ? 도움말
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
#include "sound.h"

// https 왕복·JSON·WebSocket 이 모두 loop 에서 돈다 — 기본 8KB 로는 빠듯하다.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ── 핀 ───────────────────────────────────────────────────────────
#define TFT_CS 4
#define TFT_DC 5
#define TFT_RST 6
#define TFT_BLK 7
#define TFT_SCK 15
#define TFT_MOSI 16

// 80MHz. 한 화면(240*240*2 = 115,200바이트)을 패널로 밀어 넣는 순수 SPI 시간이
// 40MHz 의 23.0ms 에서 11.5ms 로 준다 — 대기화면·고리·사진이 모두 이 길을 쓴다.
// ★ 배선이 길거나 꽂아 쓰는 점퍼선이면 이 속도에서 화면에 잡음이 섞일 수 있다.
//   그러면 40000000L 로 되돌리면 된다(그 밖에 영향은 없다).
#define TFT_SPI_HZ 80000000L
#define BLK_FREQ 5000
#define BLK_RES 8

#define PN532_SCK 9
#define PN532_MISO 10
#define PN532_MOSI 11
#define PN532_CS 12

#define SCAN_TIMEOUT_MS 60  // 한 번의 스캔에 쓰는 시간. 길면 화면이 느려진다

// ── 같은 카드를 두 번 세지 않기 ──────────────────────────────────
// 올려둔 카드는 스캔마다(약 30ms 간격) 계속 읽힌다. 그 줄줄이 읽히는 것을 한 번으로
// 묶으려면 "떠났다" 를 알아야 하는데, 떠난 것은 **안 읽히는 틈**으로만 알 수 있다.
// NFC_GONE_MS: 이만큼 안 보였으면 카드가 리더에서 떠났다고 본다. 스캔 간격(~30ms)의
//   여러 배로 둔다 — 올려둔 채로도 한두 번은 못 읽을 수 있어서(그때 떠난 것으로 세면
//   가만히 있는 카드가 자꾸 다시 떠 버린다). 손으로 떼었다 대는 데는 이보다 훨씬 걸린다.
// NFC_RETAG_MS: 센 뒤 적어도 이만큼은 같은 카드를 다시 세지 않는다. 리더 가장자리에
//   걸친 카드는 읽혔다 안 읽혔다 깜빡이는데, 그 깜빡임마다 서버를 치고 화면을 바꾸면
//   아이 앞에서 화면이 춤춘다. 일부러 다시 대는 데는 1초보다 더 걸리므로 막히지 않는다.
#define NFC_GONE_MS 250
#define NFC_RETAG_MS 1000

// 대기 화면을 다시 그리는 주기. 전에는 루프마다 그려(루프가 그만큼 길어졌다) 밖에서
// 보낸 것에 답하는 데까지 55ms 단위로 끊겼다. 고리는 1.5초에 한 바퀴라 이 정도로도
// 충분히 매끄럽다.
#define IDLE_FRAME_MS 100

// 들고 있는 연결을 살려 두는 주기. nginx 는 한참 조용하면 끊는다(기본 75초) — 끊기면
// 쉬었다 처음 태깅하는 아이가 핸드셰이크 값(~525ms)을 다시 치른다. 아이들은 띄엄띄엄
// 오니 그게 보통이 된다. 서버 설정이 바뀌어도 버티게 넉넉히 짧게 둔다.
#define HTTP_KEEPALIVE_MS 30000UL

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
// 사진을 미리 받아 둔 데까지. 이름표를 다시 받으면 0 으로 돌려 처음부터 훑는다.
static int photoPrefetchIdx = 0;

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
static char nfcNote[64] = "";   // 서버가 보내는 거절 안내가 들어온다(한글 스무 자쯤)
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
// ★ 이 칸은 **두 일꾼이 함께 본다** — NFC 일꾼(nfcTask)이 쓰고, 본 고리(와 그 안에서
// 도는 webSocket·hub 콜백)가 쓰고 비운다. 전에는 읽기도 loop 안에 있어 한 가닥이라
// 아무 보호가 없었다. 자물쇠 없이 두면 한쪽이 strlcpy 로 반쯤 적어 둔 것을 다른 쪽이
// 집어 가(잘린 UID = '등록되지 않은 키링') 어쩌다 한 번 태깅이 헛돈다.
// 아래 세 함수로만 손을 댄다.
static SemaphoreHandle_t uidLock;

// 리더가 읽은 것이면 찍을 줄이 함께 온다 — ★ 찍는 것은 **본 고리가** 한다.
// 일꾼에서 Serial.printf 를 부르면 그 줄이 **소리 없이 사라진다**: setTxTimeoutMs(0) 이
// HWCDC 의 tx 자물쇠를 "안 되면 그냥 포기" 로 만들어 두는데(그 줄의 주석대로, 안 그러면
// 기기가 2초씩 멈춘다), 본 고리가 한창 찍고 있는 참이면 일꾼 쪽 write 가 0 을 돌려주고
// 그 줄을 버린다. 실제로 그렇게 잃었다 — 카드를 3장 읽었는데(readCount 3) 로그에는
// [리더] 줄이 하나만 남아 있었다. 그래서 Serial 은 전처럼 한 가닥에서만 쓴다.
static char pendingLog[72] = "";

static void postPendingUid(const char *uid, const char *log = nullptr) {
  xSemaphoreTake(uidLock, portMAX_DELAY);
  strlcpy(pendingUid, uid, sizeof(pendingUid));
  strlcpy(pendingLog, log ? log : "", sizeof(pendingLog));
  xSemaphoreGive(uidLock);
}

// 담긴 것이 있으면 out 으로 옮기고 칸을 비운다(처리 중에 다음 태그가 들어와도 덮이지
// 않게 **먼저** 비우는 것은 전과 같다).
static bool takePendingUid(char *out, size_t n, char *log = nullptr, size_t logn = 0) {
  xSemaphoreTake(uidLock, portMAX_DELAY);
  const bool got = (pendingUid[0] != '\0');
  if (got) {
    strlcpy(out, pendingUid, n);
    pendingUid[0] = '\0';
    if (log && logn) strlcpy(log, pendingLog, logn);
    pendingLog[0] = '\0';
  }
  xSemaphoreGive(uidLock);
  return got;
}

static bool pendingUidWaiting() {
  xSemaphoreTake(uidLock, portMAX_DELAY);
  const bool got = (pendingUid[0] != '\0');
  xSemaphoreGive(uidLock);
  return got;
}

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
static uint8_t heldUid[10] = {0};   // ISO14443A UID 는 10바이트까지다(4·7·10)
static uint8_t heldUidLen = 0;
static uint32_t heldSeenAt = 0;   // 마지막으로 **읽힌** 때(올려둔 동안 계속 갱신된다)
static uint32_t heldTaggedAt = 0;  // 마지막으로 **센** 때(새 태그로 받아들인 때)
static uint32_t readCount = 0;
// 스캔만 도는 일꾼. 들고 있는 까닭은 'i' 에서 스택 여유를 보려는 것뿐이다.
static TaskHandle_t nfcTaskHandle = nullptr;

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

// 사진 받아 깔기는 서버와 말하는 쪽(httpSend·Spinner)이 아래에 있어 거기에 둔다.
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
    // 한 칸을 크게, 드물게 — 고리는 폭 5픽셀인데 flush() 는 늘 화면을 통째로
    // (115,200바이트) 밀어 넣는다(Arduino_Canvas 에 일부만 밀어 넣는 길이 없다).
    // 40ms 마다면 초당 2.88MB 로 SPI 를 절반 넘게 쓰는데, 바로 그때 사진을 내려받고
    // 있다 — 고리가 자기가 기다리는 다운로드를 늦추고 있었다. 각속도는 거의 그대로
    // 둔다(15도/40ms ≒ 36도/100ms).
    deg = (deg + 36) % 360;
    vTaskDelay(pdMS_TO_TICKS(100));
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
// on 이 false 면 아무것도 하지 않는다 — 사진을 미리 받아 둘 때처럼 화면에 보일 일이
// 없는 왕복에서는 고리를 돌리지 않는다(돌리면 대기 화면에 뜬금없이 뜬다).
struct Spinner {
  const bool on;
  explicit Spinner(bool on_ = true) : on(on_) {
    if (on) spinStart();
  }
  ~Spinner() {
    if (on) spinStop();
  }
};

// ── 화면들 ───────────────────────────────────────────────────────
// 연결 상태 한 줄 — 와이파이·허브·출석모드를 한눈에
static void statusLine(char *out, size_t cap) {
  snprintf(out, cap, "%s %s %s", online ? "wifi" : "WIFI-", hubUp ? "hub" : "hub-",
           cfgAttendMode ? (cfgAttendCard[0] ? "attend" : "attend?") : "");
}

// 다시 그릴 시각. drawIdle() 을 어디서 불러도 여기서 갱신되므로 부르는 쪽마다 따로
// 챙기지 않아도 된다(loop 는 이 값만 본다).
static uint32_t idleNextAt = 0;

static void drawIdle() {
  idleNextAt = millis() + IDLE_FRAME_MS;
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
#if SOUND_SAY_IDLE
  // 들어올 때만 부른다 — drawIdle() 은 1.5초마다 다시 그리므로 거기 두면 쉬지 않고 떠든다.
  if (mode != MODE_IDLE) soundSay(SND_IDLE_PROMPT);
#endif
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
  // 그리기보다 **먼저** 읽힌다 — drawNfc() 는 사진을 받느라 1초쯤 loop 를 붙잡는데,
  // 모듈은 한 번 틀어 주면 혼자 읽으므로 그동안 첫 토막이 흐른다.
  // 세 갈래(리더·ws·허브)가 모두 이 함수를 지나므로 소리를 거는 자리도 여기 하나다.
  soundSayNote(nfcNote, nfcPoints);
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
// 연결 **하나를 맺어 계속 쓴다**(keep-alive). 전에는 요청마다 WiFiClientSecure 를
// 부르는 함수의 스택에 두어, 함수가 끝나면 연결이 소멸하고 다음 요청이 또 처음부터
// 맺었다. 그 고정비(DNS+TCP+TLS)를 재 보니 **본문 크기와 무관하게 늘 ~525ms** 였다
// (조회·이름표·사진이 모두 같았다. 같은 공유기에서 맥으로 재면 37ms 다 — 서버가 느린
// 것이 아니라 핸드셰이크 값이다). 태깅 한 번에 조회와 사진으로 두 번 치러 1.33초 중
// 1초가 여기였다. 치는 곳은 TALENT_SERVER 하나뿐이라 연결을 들고 있으면 핸드셰이크는
// 처음 한 번뿐이다.
//
// ★ 객체가 하나뿐이라 **겹쳐 쓸 수 없다** — 한 요청을 http.end() 로 끝맺은 뒤에 다음을
//   부른다. 지금 서버를 부르는 자리는 모두 차례로 돈다(조회 → 사진, 설정, 이름표, 지급).
static WiFiClientSecure httpsCli;
static WiFiClient httpPlainCli;
static HTTPClient http;
static bool httpInsecureSet = false;
static uint32_t httpWarmAt = 0;

static bool talentSsl() { return strncmp(TALENT_SERVER, "https", 5) == 0; }

// 들고 있던 연결을 정말 끊는다. setReuse(false) 라야 end() 가 소켓을 닫는다.
static void httpDrop() {
  http.setReuse(false);
  http.end();
  http.setReuse(true);
  httpsCli.stop();
  httpPlainCli.stop();
}

// 요청 하나를 보낸다. body 가 nullptr 이면 GET, 아니면 JSON POST(method 로 따로 적으면
// 그것으로 보낸다). 돌려주는 값은 HTTP 코드이고 음수면 보내지 못한 것이다.
// 본문·스트림은 부른 쪽에서 읽고 **부른 쪽이 http.end() 를 부른다** — 여기서 끝맺으면
// 아직 읽지 않은 스트림이 닫혀 버린다.
//
// retry: 들고 있는 연결을 서버가 조용히 닫는 일이 있다(keep-alive 시간이 지나면 끊는다).
//   그러면 보내기는 되지만 답이 오지 않는다 — 그때 한 번만 새로 맺어 다시 물어본다.
//   이 되살리기가 없으면 한참 조용했다가 태깅할 때 첫 요청이 늘 실패한다.
//   ★ 다만 **또 보내면 안 되는 요청에는 쓰지 않는다**(지급 POST). 서버가 받아서 처리한
//     뒤에 답만 못 온 경우를 가릴 길이 없어, 다시 보내면 포인트를 두 번 줄 수 있다.
static int httpSend(const char *url, const char *body, const char *method, bool retry) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (talentSsl()) {
      if (!httpInsecureSet) {  // 인증서 검사는 건너뛴다(기기 키로 가린다) — 한 번만 켜면 된다
        httpsCli.setInsecure();
        httpInsecureSet = true;
      }
      http.begin(httpsCli, url);
    } else {
      http.begin(httpPlainCli, url);
    }
    http.setReuse(true);  // ★ 이것이 end() 에서 소켓을 살려 두게 한다
    http.setTimeout(HTTP_TIMEOUT_MS);
    // begin() 이 요청 헤더를 비우므로 헤더는 늘 여기서 다시 얹는다(겹쳐 쌓이지 않는다).
    http.addHeader("x-talent-key", TALENT_DEVICE_KEY);
    if (body) http.addHeader("Content-Type", "application/json");

    const char *verb = method ? method : (body ? "POST" : "GET");
    const int code = http.sendRequest(verb, (uint8_t *)body, body ? strlen(body) : 0);
    if (code > 0) {
      httpWarmAt = millis() + HTTP_KEEPALIVE_MS;  // 방금 썼으니 한동안 찌를 필요가 없다
      return code;
    }

    http.end();
    if (attempt == 0 && retry) {
      Serial.printf("[서버] 들고 있던 연결이 닫혀 있었다(code=%d) — 새로 맺는다\n", code);
      httpDrop();
      continue;
    }
    return code;
  }
  return -1;
}

static int httpGet(const char *url) { return httpSend(url, nullptr, nullptr, true); }
static int httpPost(const char *url, const char *body, bool retry) {
  return httpSend(url, body, nullptr, retry);
}

// 연결을 살려 두려고 **본문 없는 HEAD** 를 한 번 보낸다. 답이 무엇이든 상관없다 —
// 목적은 소켓을 깨워 두는 것뿐이다(실패하면 다음 요청이 새로 맺는다).
static void httpWarm() {
  httpWarmAt = millis() + HTTP_KEEPALIVE_MS;
  if (WiFi.status() != WL_CONNECTED) return;
  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/config/v2?device=%s", TALENT_SERVER, TALENT_DEVICE_ID);
  httpSend(url, nullptr, "HEAD", true);
  http.end();
}

// ── 사진 한 장 받아 화면 버퍼에 깔기 ────────────────────────────
// 원본은 TALENT_PX 정사각이고 원 밖은 비침색이다 — 그 점은 건너뛴다(바탕이 그대로 남는다).
// 줄일 때는 **이웃 점 고르기**(평균 아님)를 쓴다. 평균을 내면 원 테두리에서 비침색(자홍)이
// 진짜 색과 섞여 얼굴 둘레에 자홍 테가 생긴다.
//
// 한 줄씩 흘려 읽는다 — 480 짜리 한 장은 460KB 라 통째로 받아 둘 자리가 없다(heap 은
// 250KB 남짓이다). 한 줄(960바이트)만 들고 바로 화면 버퍼에 옮긴다.
// draw 가 false 면 **화면에 깔지 않고 캐시에만 담는다** — 미리 받아 두는 길(loop 의
// 선캐싱)이 이것을 쓴다. 그때는 고리도 돌리지 않는다.
static bool loadPhoto(const char *img, bool draw) {
#if !PHOTO_ENABLE
  return false;
#else
  if (!img || !img[0]) return false;
  uint16_t *fb = draw ? gfx->getFramebuffer() : nullptr;
  if (draw && !fb) return false;

#if PHOTO_CACHE_MAX > 0
  // 1) 들고 있는 사진이면 내려받지 않는다 — 와이파이가 끊겨 있어도 뜬다.
  const uint16_t *hit = photoCacheGet(img);
  if (hit) {
    photoHit++;
    if (draw) blitPhoto(hit, 240);
    return true;
  }
  photoMiss++;
#endif

  if (WiFi.status() != WL_CONNECTED) return false;

  Spinner spin(draw);  // 받는 동안 테두리에 금색 고리가 돈다(미리 받을 때는 돌리지 않는다)

  char url[220];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, img);

  const uint32_t t0 = millis();
  const int code = httpGet(url);
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
  // 미리 받아 두는 길은 담을 칸이 있어야 뜻이 있다 — 칸을 못 얻었으면 접는다
  // (화면에 깔 일이 없으니 받아도 버릴 뿐이다).
  if (!draw && !slot) {
    http.end();
    return false;
  }

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
        if (draw) blitPhoto(slot, rows);
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
    if (draw) blitPhoto(slot, rows);
  }
  Serial.printf("[사진] %s %s (%d줄, %d바이트, %lums)\n", img, draw ? "깔았다" : "미리 받았다",
                rows, expect, (unsigned long)(millis() - t0));
  return rows > 0;
#endif
}

static bool drawPhoto(const char *img) { return loadPhoto(img, true); }

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

  const int code = httpGet(url);
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

  const uint32_t t0 = millis();
  const int code = httpGet(url);
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
  photoPrefetchIdx = 0;  // 이름표가 바뀌었으니 사진을 처음부터 다시 훑는다
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

  const uint32_t t0 = millis();
  const int code = httpGet(url);
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
                       Mood *mood = nullptr, char *deny = nullptr, size_t denyCap = 0) {
  Spinner spin;
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/earn", TALENT_SERVER);

  char payload[192];
  snprintf(payload, sizeof(payload), "{\"uid\":\"%s\",\"card\":\"%s\",\"device\":\"%s\"}", uid,
           cfgAttendCard, TALENT_DEVICE_ID);

  // ★ retry 를 끈다 — 서버가 받아 포인트를 준 뒤에 답만 못 온 경우를 가릴 길이 없어,
  //   다시 보내면 두 번 줄 수 있다. 실패하면 processTag 가 조회로 넘어가고(그때 연결이
  //   되살아난다) 아이가 한 번 더 대면 된다.
  const int code = httpPost(url, payload, false);
  String body = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[출석] 지급 실패 code=%d %s\n", code, body.c_str());
    // 403 은 **고장이 아니라 때가 아닌 것**이다 — 출석 가능 시간대(요일·시간) 밖이거나
    // 포인트가 없는 시간대다. 서버가 아이에게 할 말로 짧게 돌려주므로(서버의
    // talentConfigV2.openAt) 그대로 화면에 띄운다. 400(설정이 빠졌다)은 관리자에게
    // 하는 말이라 띄우지 않는다 — 줄 선 아이에게 보일 글이 아니다.
    if (code == 403 && deny && denyCap) {
      JsonDocument err;
      if (!deserializeJson(err, body)) copyJsonSafe(deny, denyCap, err["message"] | "");
    }
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

  // 같은 UID 를 또 알려도 '미등록카드' 에 한 줄로 남을 뿐이라 다시 보내도 괜찮다.
  const int code = httpPost(url, payload, true);
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
  // 서버가 출석을 거절하며 보낸 까닭(출석 시간이 아니다 / 포인트 없는 시간대다).
  // 비어 있으면 거절이 아니었다는 뜻이다.
  char deny[64] = "";

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
    if (attendEarn(uid, name, sizeof(name), &points, &delta, &mood, deny, sizeof(deny))) {
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
    // 거절당했으면 그 까닭을 띄운다. 사진·잔액은 그대로 보여 준다 — 아무 반응이
    // 없으면 아이는 기기가 고장난 줄 알고 계속 카드를 댄다.
    showNfc(uid, name, points, known, deny[0] ? deny : (known ? "" : "등록되지 않은 키링"), &mood);
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
      postPendingUid(uid);  // 서버 왕복은 loop 에서
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
// ★ 이 함수와 nfc(Adafruit_PN532) · heldUid/heldUidLen/heldSeenAt/heldTaggedAt 은
//   **nfcTask 만** 만진다.
//   라이브러리가 thread-safe 가 아니라, 다른 곳에서 nfc 를 부르면 같은 SPI 주고받기가
//   서로 섞여 엉뚱한 응답을 읽는다.
static void nfcScanOnce() {
  // ★ 버퍼를 넉넉히 잡는다. 라이브러리는 **응답이 말하는 길이만큼 먼저 복사하고 나서**
  //   그 길이를 돌려준다(Adafruit_PN532::readDetectedPassiveTargetID 의 복사 고리는
  //   우리가 준 버퍼 크기를 모른다). 7바이트짜리를 주면 10바이트 UID 카드 한 장,
  //   어긋난 프레임 한 번에 스택이 넘친다 — 받고 나서 거르려면 그 전에 들어갈 자리가
  //   있어야 한다. nfcProject 의 리더 판도 같은 까닭으로 255를 쓴다.
  uint8_t uid[255] = {0};
  uint8_t uidLen = 0;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, SCAN_TIMEOUT_MS)) return;
  // ISO14443A UID 는 4·7·10 바이트뿐이다 —
  //   4  교통카드·휴대폰(휴대폰은 댈 때마다 다른 UID 를 낸다)
  //   7  우리 키링(NTAG213)
  //   10 세 겹 UID
  // 그 밖의 길이는 프레임이 어긋난 것이라 버린다.
  // ★ 전에는 `uidLen > sizeof(uid)` 로 걸렀는데 버퍼가 7바이트여서, **10바이트 UID 는
  //   스택을 넘긴 뒤에 조용히 버려졌다**(넘기는 쪽이 먼저다 — 위 ★). 이제 길이로 가른다.
  if (uidLen != 4 && uidLen != 7 && uidLen != 10) return;

  const uint32_t now = millis();
  const bool sameCard = (uidLen == heldUidLen) && (memcmp(uid, heldUid, uidLen) == 0);
  // 그 사이 안 읽힌 틈이 있었나 — 있었으면 떼었다 다시 댄 것이다.
  const bool wasGone = (now - heldSeenAt) >= NFC_GONE_MS;
  // 올려둔 채로 줄줄이 읽히는 것(틈이 없다)과, 센 지 얼마 안 된 깜빡임은 세지 않는다.
  // ★ 전에는 '같은 카드이고 마지막으로 **읽힌** 지 6초 안이면 넘긴다' 였는데, 읽힐
  //   때마다 그 6초가 다시 시작돼서(heldAt 을 늘 갱신한다) **5초마다 다시 대면 처음
  //   한 번 말고는 영원히 세지 않았다**. 실제로 스무 번을 댔는데 세 번만 셌다.
  //   "읽힌 지" 가 아니라 "떠났다 왔나" 로 보는 것이 묶으려던 바로 그것이다.
  const bool stillHeld = sameCard && (!wasGone || (now - heldTaggedAt) < NFC_RETAG_MS);

  memcpy(heldUid, uid, uidLen);
  heldUidLen = uidLen;
  heldSeenAt = now;
  if (stillHeld) return;
  heldTaggedAt = now;

  readCount++;
  char hex[24] = "";
  for (uint8_t i = 0; i < uidLen; i++) {
    char b[4];
    snprintf(b, sizeof(b), "%02X", uid[i]);
    strlcat(hex, b, sizeof(hex));
  }
  // 찍는 것은 본 고리에 맡긴다(postPendingUid 의 주석 — 여기서 찍으면 줄이 사라진다).
  char log[72];
  snprintf(log, sizeof(log), "[리더] card #%lu  UID %s (%d bytes)", (unsigned long)readCount, hex,
           uidLen);
  postPendingUid(hex, log);  // 찍기·서버 왕복은 loop 에서
}

// 카드 스캔만 도는 일꾼. 전에는 이것을 loop 맨 앞에서 불렀는데, 읽기는 **멈춰 서서
// 기다리는 일**이라 카드가 없어도 한 바퀴에 SCAN_TIMEOUT_MS(60ms)를 거기서 썼다 —
// 그래서 밖에서 ws 로 보낸 것에 답하는 시간이 그 60ms 단위로 끊겼다(실측 median 58ms ·
// 최소 50ms). 시간을 줄이면 카드를 놓치므로(상수 주석대로 일부러 둔 값이다) 줄이는
// 대신 일을 옆으로 뺐다. 디스플레이는 FSPI · PN532 는 HSPI 로 버스가 갈려 있어 이
// 일꾼과 본 고리가 SPI 를 다투지 않는다.
// Core 0 에 둔다 — Arduino 의 loop 는 Core 1 이라 둘이 서로 밀지 않는다.
static void nfcTask(void *) {
  for (;;) {
    nfcScanOnce();
    // 읽기 안에 delay(10) 가 있어 이미 자리를 내주지만, 한 바퀴에 적어도 한 번은
    // 반드시 내주게 둔다 — Core 0 의 idle 이 못 돌면 태스크 워치독이 짖는다.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
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
    soundSay(SND_CONFIG);
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
  // ★ 시리얼 쓰기를 기다리지 않게 한다 — 이것이 없으면 **기기가 2초씩 멈춘다**.
  // 이 보드는 내장 USB-Serial/JTAG 로 Serial 을 낸다(ARDUINO_USB_CDC_ON_BOOT=1).
  // USB 가 컴퓨터에 꽂혀 있는데 그 글자를 읽어 주는 프로그램이 없으면(모니터를 띄우지
  // 않고 전원만 쓰려고 꽂아 둔 경우) 호스트 쪽 버퍼가 차고, 그 뒤로는 Serial.printf 가
  // 한 줄마다 멈춰 선다 — HWCDC 는 tx_timeout_ms(기본 100ms) 를 스무 번까지 기다려
  // **한 줄에 약 2초**다. 태깅 한 번에 여러 줄을 찍으니 기기가 쓸 수 없게 된다.
  // (ws 왕복으로 재서 찾았다: 시리얼을 읽는 동안은 58ms, 안 읽으면 25번 중 18번이 2초.)
  // 0 으로 두면 기다리지 않고 그 줄을 버린다 — 모니터를 띄워 두면 호스트가 계속
  // 비워 주므로 버려지는 줄은 없다. 벽돌 충전기만 꽂은 경우는 원래 영향이 없다.
  Serial.setTxTimeoutMs(0);
  // 소리 모듈을 **여기서** 깨운다 — 전원이 들어온 뒤 스스로 서는 데 1.5초쯤 걸리는데,
  // 바로 아래 USB CDC 를 기다리는 2초 동안 그것이 지나간다(따로 기다리지 않는다).
  soundBegin();
  delay(2000);  // USB CDC 가 올라올 때까지 대기

  Serial.println();
  Serial.println("=== funfunFirmware — NFC UID -> 이름·잔액·기분 ===");
  Serial.printf("Display: FSPI  SCK=%d MOSI=%d CS=%d DC=%d RST=%d BLK=%d @ %ld Hz\n", TFT_SCK,
                TFT_MOSI, TFT_CS, TFT_DC, TFT_RST, TFT_BLK, TFT_SPI_HZ);
  Serial.printf("PN532  : HSPI  SCK=%d MISO=%d MOSI=%d CS=%d\n", PN532_SCK, PN532_MISO, PN532_MOSI,
                PN532_CS);
  Serial.printf("Server : %s · device %s\n", TALENT_SERVER, TALENT_DEVICE_ID);
#if SOUND_ENABLE
  Serial.printf("Sound  : DFPlayer UART1 TX=%d(모듈 RX) RX=%d(모듈 TX) · 음량 %d/30\n", DF_TX_PIN,
                DF_RX_PIN, SOUND_VOLUME);
#else
  Serial.println("Sound  : 꺼 둠 (SOUND_ENABLE 0)");
#endif

  gfxLock = xSemaphoreCreateMutex();
  uidLock = xSemaphoreCreateMutex();
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
#if SOUND_SAY_BOOT
  soundSay(SND_READER_INIT);
#endif

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
    soundSay(SND_READER_FAIL);
    // 여기서 기기가 선다 — loop 가 안 도니 소리는 이쪽에서 돌려 줘야 읽힌다.
    while (true) {
      soundPump(1000);
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
    soundSay(SND_READER_FAIL);
    while (true) {
      soundPump(1000);
    }
  }
  Serial.printf("PN532 found: chip PN5%02X, firmware %d.%d\n", (ver >> 24) & 0xFF,
                (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);

  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(1);  // 카드가 없을 때 빨리 포기하도록

  // 카드 스캔은 전용 일꾼에게 맡긴다(위 nfcTask 주석). 반드시 PN532 가 선 뒤에 올린다.
  xTaskCreatePinnedToCore(nfcTask, "nfc", 4096, nullptr, 1, &nfcTaskHandle, 0);

  // 와이파이가 NVS 에 쓰지 않게 막는다 — 플래시에 쓰는 동안 캐시가 꺼져 화면·SPI 가
  // 늘어진다(참고 기기에서는 RGB 패널 그림이 쓸렸다).
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  // 모뎀 슬립을 끈다 — 이 기기는 벽 전원이고, 밖에서 들어오는 것을 기다리는 쪽이다.
  // ESP32 기본값은 WIFI_PS_MIN_MODEM 이라 조용할 때 라디오를 껐다 켠다. 그동안 들어온
  // 패킷은 AP 가 버퍼에 들고 있다가 다음 DTIM 비컨에야 내려 주므로, **한동안 조용했다
  // 들어온 첫 패킷**이 그만큼 늦는다. 촘촘히 주고받는 동안은 깨어 있어 안 보이고 뜸할
  // 때만 나타나기 때문에, 찌르는 **간격을 바꿔 가며** 재지 않으면 찾지 못한다.
  //
  // 같은 기기·같은 AP 에서 이 한 줄만 넣고 뺀 A/B:
  //   들어오는 쪽만 (보낸 시각 vs 기기가 받아 찍은 줄의 시각)
  //     간격 1초 → 켜짐 median 98ms(min 72) · 끔 median 50ms(min 11)
  //     간격 3초 → 켜짐 median 71ms(min 39) · 끔 median 43ms(min  5)
  //   ws 왕복({"enabled":true} 보내고 RTT — goIdle() 의 화면 다시 그리기 28ms 를 품는다)
  //     간격 40ms → 켜짐 39ms · 끔 37ms   (촘촘하면 차이가 안 보인다)
  //     간격 300ms → 켜짐 126ms · 끔 41ms
  //     간격 1초   → 켜짐 129ms · 끔 39ms
  // 바닥(min)이 72ms → 11ms 로 주저앉는 것이 비컨을 기다리지 않게 된 자리다.
  // 현장에서 걸리는 데가 바로 이 "뜸한" 쪽이다. 관리 화면에서 포인트를 고치면 서버가
  // wss(hub)로 밀어 주는데, 그 길은 몇 분에 한 번 조용히 있다가 오므로 늘 이것을 물고
  // 들어왔다. 운영 기기(nfcProject)도 같은 이유로 꺼 두고 있다.
  //
  // 값은 치른다: 라디오 수신부가 늘 켜져 있어 **가만히 있을 때 칩 온도가 43.4℃ → 56.4℃**
  // 로 올라갔다(각각 10분 이상 두고 평평해진 값, 통풍되는 맨 보드). 벽 전원이라 전력은
  // 문제가 아니고 ESP32-S3 한계에는 한참 못 미치지만, 케이스에 넣을 때는 기억해 둘 것.
  //
  // 딴 것 하나: 2초 간격 ws 왕복만 유독 ~95ms 가 나오는데 이것은 모뎀 슬립이 **아니다**
  // (슬립을 꺼도 남고, 같은 2초 간격 ping 은 7ms 다). 기기가 **답하는** 쪽에 있는 별개의
  // 건이라 hub 로 밀려 들어오는 길에는 영향이 없다. 2초 간격으로 재다가 헷갈리지 말 것.
  WiFi.setSleep(false);
  drawStatus("WiFi 붙는 중");
#if SOUND_SAY_BOOT
  soundSay(SND_WIFI);
#endif

  Serial.printf("Ready. free heap %lu\n", (unsigned long)ESP.getFreeHeap());
}

void loop() {
  // 카드 스캔은 여기 없다 — 전용 일꾼(nfcTask)이 돈다. 읽은 UID 는 pendingUid 로 넘어온다.
  soundLoop();    // 다 읽은 토막 다음 것을 밀어 넣는다(기다리지 않는다)
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
  char uid[32];
  char uidLog[72];
  if (takePendingUid(uid, sizeof(uid), uidLog, sizeof(uidLog))) {
    if (uidLog[0]) Serial.println(uidLog);  // 리더가 읽은 줄 — 찍는 자리는 여기뿐이다
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
        soundReport();
        // 일꾼 스택에서 **한 번도 안 쓰고 남은** 바이트. 0 에 가까우면 넓혀야 한다.
        Serial.printf("[상태] nfc 일꾼 스택 여유 %u bytes (잡은 것 4096)\n",
                      nfcTaskHandle ? (unsigned)(uxTaskGetStackHighWaterMark(nfcTaskHandle) *
                                                 sizeof(StackType_t))
                                    : 0u);
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
      // 스피커·SD·번호를 눈이 아니라 귀로 확인한다. 한 번 누를 때마다 다음 토막.
      case 'v':
      case 'V':
        soundTestNext();
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
                       "v 소리 토막 · w 와이파이 다시 · r 재부팅");
        break;
      default:
        break;
    }
  }

  // NFC·일괄 화면은 얼마 뒤 자동으로 대기 화면으로 돌아간다
  if ((mode == MODE_NFC || mode == MODE_BULK) && (int32_t)(millis() - nfcUntil) >= 0) goIdle();

  // 들고 있는 연결이 녹슬지 않게 조용히 한 번 찌른다 — 대기 화면일 때만.
  if (online && mode == MODE_IDLE && (int32_t)(millis() - httpWarmAt) >= 0) httpWarm();

#if PHOTO_CACHE_MAX > 0
  // 사진 미리 받아 두기 — 이름표를 받은 뒤 한 장씩 캐시에 채운다. 태깅할 때 받으면 그
  // 아이가 사진을 기다리지만(재 보니 ~800ms), 미리 받아 두면 캐시에서 바로 깔린다.
  // 아이가 열 명인데 캐시가 60칸이라 전원이 들어간다.
  // ★ 대기 화면일 때 한 번에 한 장만 — 받는 동안 loop 가 멈추므로 길게 붙잡지 않는다.
  if (online && mode == MODE_IDLE && !pendingUidWaiting() && photoPrefetchIdx < rosterCount) {
    const int i = photoPrefetchIdx++;
    if (!photoCacheGet(roster[i].img)) loadPhoto(roster[i].img, false);
    if (photoPrefetchIdx >= rosterCount) Serial.printf("[사진] 미리 받기 끝 (%d명)\n", rosterCount);
  }
#endif

  // 대기 화면은 고리가 돌아야 해서 다시 그린다 — 다만 **프레임을 제한한다**.
  // 전에는 루프마다 그렸다. drawIdle() 은 화면을 통째로 다시 그리고 115,200바이트를
  // 패널로 밀어 넣어 그만큼 루프가 길어졌다(그래서 밖에서 보낸 것에 답하는 데까지
  // 55ms 단위로 끊겼다).
  if (mode == MODE_IDLE && (int32_t)(millis() - idleNextAt) >= 0) drawIdle();
}
