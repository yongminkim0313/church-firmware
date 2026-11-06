// funfunFirmware — PN532 NFC 리더(SPI) + GC9A01 원형 디스플레이(SPI), 글자만으로 그리는 판
//
// 보드: ESP32-S3-N16R8 · GC9A01 240×240 원형 IPS(SPI) · PN532(SPI)
// nfcProject/master 의 기능을 이 하드웨어에 옮긴 것이다. 다른 점만 먼저 적는다:
//   · 사진은 96 동그라미 하나다(서버의 240 을 줄인다) — microSD 가 없어 PSRAM 에 미리 받아 둔다.
//   · 리더가 이 보드에 직접 붙어 있다 — 참고 기기는 리더가 딴 보드라 선(I2C)·ESP-NOW 로
//     받아 왔지만, 여기서는 PN532 를 직접 읽는다. 그 두 길은 넣지 않았다.
//   · 화면 터치가 없다 — 보드의 BOOT 버튼으로 대신한다(짧게: 대기 화면·다음 장 · 길게: 설정 받기).
//     남는 GPIO 하나를 정전식 터치 자리로 쓴다 — 손을 대면 BOOT 짧게와 같다(touchService).
//   · 소리가 난다 — DFPlayer Mini(YX5200)가 미리 구워 둔 토막을 이어 읽는다.
//     읽을 수 있는 말과 못 읽는 말, SD 짜임새는 sound.h 머리말에 있다.
//   · 상태 불이 있다 — 보드에 달린 RGB LED 한 알을 빨강·노랑·초록으로 쓴다. 화면은
//     아이 쪽을 보고 있어, 지나가는 교사에게 '지금 쓸 수 있나' 를 알리는 자리가 없었다.
//     색이 무엇을 뜻하는지는 led.h 머리말에 있다.
//
// ── 화면 네 가지 ──────────────────────────────────────────────────
//   대기   : 홈(home.h — 등록 인원 · 총 포인트 · 오늘 +/-) 뒤로 네 장이 돌아간다(pages.h) —
//            최근 내역 · 오늘 출석 · 이번 달 출석 랭킹 · 포인트 랭킹. 위 띠의 점이 몇째 장인가.
//   NFC    : 한 장 — 96 사진 · 이름 · '지급 +5P'/'사용 -3P' · 분류·사유 · 잔액.
//            포인트가 바뀌지 않은 조회면 가운데에 잔액, 그 아래 안내문·기분. NFC_SHOW_MS 뒤 대기로.
//   일괄   : 관리 화면의 일괄 지급·사용 — 제목 + 받은 아이 명단을 한 화면에.
//   상태   : 한 줄 알림(동기화·오류 등).
//
// ── 받기와 그리기를 겹치지 않는다 ─────────────────────────────────
//   태그 → 와이파이로 주고받기(지급·조회·사진) → **다 끝난 뒤** 화면 한 장. 받는 동안에는
//   화면을 건드리지 않고 상태 불만 노랗게 켠다. 전에는 받는 동안 테두리 고리를 따로 띄운
//   일꾼이 돌며 100ms 마다 화면을 통째로(115KB) 밀어 넣었다 — 그 일꾼을 걷었다.
//   이제 화면을 그리는 것은 본 고리(loop) 하나뿐이고, 서버 왕복도 같은 고리에서 차례로 돌아
//   둘이 같은 순간에 일어날 수 없다. 허브로 밀려온 알림도 그 자리에서 그리지 않고
//   (콜백 안이다) 깃발만 세워 loop 의 같은 자리(presentNfc)에서 받기 → 그리기로 간다.
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
//                                (BLK 선이 없으면 아예 받지 않는다 — TFT_BLK_ENABLE)
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
//     ★ BLK 가 없는 모듈이면 그 선만 빼고 config.h 의 TFT_BLK_ENABLE 을 0 으로 —
//       화면은 그대로 나온다(백라이트가 VCC 에 묶여 늘 최대 밝기다).
//   PN532 — HSPI(SPI3), 디스플레이와 별도 버스
//     VCC→3V3  GND→GND  SCK→GPIO9  MISO→GPIO10  MOSI→GPIO11  SS/CS→GPIO12
//     IRQ·RSTO 미연결 · 모듈 모드 스위치를 SPI 로 (보통 SEL0=OFF, SEL1=ON)
//   ※ GPIO3 은 이 보드에서 LOW 로 고착되어 쓰지 않는다.
//   DFPlayer Mini(YX5200) — UART1
//     VCC→5V  GND→GND  RX←GPIO17(1kΩ 직렬)  TX→GPIO18
//     SPK_1/SPK_2 에 3W 꼬마 스피커(또는 DAC_L/DAC_R 를 앰프로) · BUSY 미연결
//     ★ TX·RX 가 교차다. 그리고 RX 선(GPIO18)을 빠뜨리면 '다 읽었다' 알림이 오지
//       않아 토막마다 5초를 기다려 말이 뚝뚝 끊긴다 — 소리는 나므로 찾기 어렵다.
//     ★ 모듈 VCC 옆에 100µF 를 하나. 없으면 큰 소리에서 보드가 리셋된다.
//     SD 는 FAT32 · 폴더는 두 자리('01') — 토막 표는 src/sound.h, 파일은 voices/.
//   상태 불 — 보드에 이미 달려 있다(WS2812 한 알). 배선할 것이 없다.
//     v1.1 은 GPIO48, v1.0 은 GPIO38 — config.h 의 LED_PIN.
//
// ── 시리얼 명령 (115200) ──────────────────────────────────────────
//   ★ 보드의 USB-C 두 구멍 가운데 **'USB'(네이티브)** 쪽에 꽂아야 글자가 보인다.
//     'UART'(CH343) 쪽은 굽기만 되고 조용하다 — 까닭은 platformio.ini 에.
//   h 대기 화면 · g 대기 다음 장 · u 터치 값 · c 설정 받기 · i 상태 · t 시험 화면 · w 와이파이 다시
//   {…} 알림 JSON 한 줄(ws·허브와 같은 글자) · m 메모리 전부 · s 최근 출석 목록 · v 소리 토막 하나씩 · l 상태 불 색 하나씩 · r 재부팅 · ? 도움말
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
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>  // esp_ptr_external_ram — 화면 버퍼가 PSRAM 에 있나

#include "config.h"
#include "emoji.h"       // '지금 기분' 이모티콘 그림(열 개) — tools/mkemoji.py 가 만든다
#include "characters.h"  // '지금 기분' 캐릭터 그림(마흔 개) — tools/mkchars.py 가 만든다
#include "creds.h"  // 아는 와이파이 목록(폰으로 넣은 것 [+ 구울 때 넣은 것])
#include "led.h"
#include "sound.h"

// https 왕복·JSON·WebSocket 이 모두 loop 에서 돈다 — 기본 8KB 로는 빠듯하다.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ── 핀 ───────────────────────────────────────────────────────────
#define TFT_CS 4
#define TFT_DC 5
#define TFT_RST 6
#define TFT_BLK 7  // BLK 선이 없는 모듈이면 config.h 의 TFT_BLK_ENABLE 0 — 안 건드린다
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
#define IDLE_FRAME_MS 1000  // 홈의 서명을 견주는 주기(그리는 것은 바뀔 때만 — drawIdle)

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
#define COL_GIVE RGB565(90, 220, 120)   // 지급
#define COL_USE RGB565(255, 140, 60)    // 사용
#define COL_UNDO RGB565(150, 150, 160)  // 되돌림(관리자가 내역을 지웠다)
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
// 서버가 주는 그림은 TALENT_PX(240) 정사각 RGB565(리틀엔디안)이고, 동그라미 밖은 비침색이다.
// ESP32 도 리틀엔디안이라 받은 바이트를 그대로 읽으면 된다. 받으면서 PHOTO_PX(96)로 줄여 담는다.
#define TRANSPARENT_KEY 0xF81F   // 서버 talentArt.js 의 TRANSPARENT_KEY 와 같은 값(자홍)

// NFC 화면(한 장) 자리 — 위에서 아래로 사진 · 이름 · 큰 줄 · 작은 줄 둘 · UID.
// 둥근 화면이라 아래로 갈수록 좁다 — 긴 줄은 drawUTF8 이 배수를 줄이고 fitEllipsis 가 자른다.
//   큰 줄   포인트가 바뀌었으면 '지급 +5P' · '사용 -3P' · '되돌림 -5P', 아니면 잔액 '1,205 P'
//   작은 줄 바뀌었으면 '분류 · 사유' 와 '잔액 1,205 P', 아니면 안내문과 기분
// ★ 사진을 96 으로 키우며 UID 줄을 뺐다 — 원 아래쪽(y 226 언저리)은 폭이 80px 남짓이라
//   잔액 줄까지가 한계다. 미등록 키링은 이름 자리에 UID 가 뜬다(showNfc).
#define CARD_PHOTO_CY 60   // 96 사진의 가운데 (y 12..108) — 큰 원 안에 통째로 든다(50+48 ≤ 120)
#define CARD_NAME_CY 128
#define CARD_MAIN_CY 164
#define CARD_SUB_CY 194
#define CARD_SUB2_CY 216

// ── 이름표 (UID → 사진 파일 이름) ────────────────────────────────
// 사진 이름은 /feed 응답에 없고 /roster 에만 있다(서버 쪽 규칙 — /feed·/roster 응답을
// 키우지 않는다). 그래서 이름표를 한 번 받아 UID→img 를 들고 있다가, 태깅할 때
// 그 img 로 사진 한 장만 받는다. 이름표는 ROSTER_TTL_MS 마다 다시 받는다 —
// 아이가 사진을 바꾸면 img 이름이 달라져 그때 새 사진을 받게 된다.
#define ROSTER_UID_LEN 24
#define ROSTER_IMG_LEN 80  // "nm-<base64url(이름) 최대 64자>-240.565" 가 들어갈 크기
struct RosterRow {
  char uid[ROSTER_UID_LEN];  // NFC UID — 카드가 없는 아이는 빈 값
  char sid[ROSTER_UID_LEN];  // 학번 — 허브는 카드 없는 아이를 학번으로 보낸다(talentFeed.showLog)
  char img[ROSTER_IMG_LEN];
};
static RosterRow *roster = nullptr;  // PSRAM 에 잡는다(400칸 = 약 51KB)
static int rosterCount = 0;
static bool rosterGotOnce = false;
static uint32_t rosterNextAt = 0;
// 관리 화면에서 사진·이름이 바뀌면 서버가 {"state":"roster"} 를 민다 — 주기(10분)를
// 기다리지 않고 다음 대기 때 받는다. 받는 콜백 위에서 https 를 돌리지 않는 것은 cfgWanted 와 같다.
static volatile bool rosterWanted = false;
// 홈 숫자(GET /api/talent/summary) — 주기와 '포인트가 바뀌었다' 깃발
static uint32_t summaryNextAt = 0;
static bool summaryWanted = false;
// 대기 화면 네 장의 알림판(GET /api/talent/signage + /feed) — 주기와 '포인트가 바뀌었다' 깃발
static uint32_t boardNextAt = 0;
static bool boardWanted = false;
// 사진을 미리 받아 둔 데까지. 이름표를 다시 받으면 0 으로 돌려 처음부터 훑는다.
static int photoPrefetchIdx = 0;

// ── 사진 캐시 (PSRAM, 가장 오래 안 쓴 것부터 밀어낸다) ───────────
// 줄여 담은 한 장이 96*96*2 = 18,432바이트다. 칸은 필요할 때 한 장씩 잡는다.
#define PHOTO_BYTES ((size_t)PHOTO_PX * PHOTO_PX * 2)
#define SRC_BYTES ((size_t)TALENT_PX * TALENT_PX * 2)  // 서버가 주는 원본(240 → 115,200바이트)
#if PHOTO_CACHE_MAX > 0
struct PhotoCacheEntry {
  char img[ROSTER_IMG_LEN];  // 열쇠 — 사진 파일 이름(해시가 들어 있어 '있다 = 최신')
  uint16_t *px;              // PSRAM 의 96x96 RGB565 (비침색 그대로 담는다)
  uint32_t usedAt;           // 마지막으로 쓴 시각 — 밀어낼 칸을 고르는 데 쓴다
  bool valid;                // 끝까지 받은 것만 true. 받다 만 것은 내보내지 않는다
};
// 칸 표 자체도 PSRAM 에 둔다(setup 에서 잡는다) — 400칸이면 36KB 라, 내부 RAM 에 두면
// TLS 가 쓸 자리를 그만큼 빼앗는다(mbedTLS 는 내부 RAM 만 쓴다).
static PhotoCacheEntry *photoCache = nullptr;
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
static char nfcMoodEmoji[32] = "";  // 그 기분의 이모티콘 · 캐릭터(":hamster-surprise:" 18자) — 그림이 있으면 사진에 스티커로 붙인다
static char nfcMoodText[128] = "";  // 아이가 적은 말만(이모티콘 이름 없이)
// 이 화면에 깔 사진의 파일 이름(이름표에서 찾은 것). 비어 있으면 글자만 그린다.
static char nfcImg[ROSTER_IMG_LEN] = "";
// 이번 태그로 바뀐 포인트 — +면 지급, -면 사용, 0 이면 바뀌지 않은 조회다.
static long nfcDelta = 0;
static bool nfcUndo = false;    // 되돌림(관리자가 내역을 지웠다) — 허브로만 온다
static char nfcWhy[64] = "";    // '분류 · 사유'. 비어 있으면 '포인트 지급' 처럼 대신 적는다
// 화면 상태는 정해 두었고 **아직 그리지 않았다** — loop 의 presentNfc 가 받을 것(사진)을
// 마저 받은 뒤 그린다. 허브 콜백 안에서 서버를 부르지 않으려는 것이기도 하다.
static bool nfcPresentWanted = false;
// 그린 뒤 이만큼 띄운다. 시험('t' · 'd')은 들여다보라고 늘린다.
static uint32_t nfcHoldMs = NFC_SHOW_MS;

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
  char line[160] = "";  // label + text 를 이어 둔 것(상태 알림·로그 — 이모티콘 그림이 없을 때 화면에도)
  char emoji[32] = "";  // 서버가 보낸 이모티콘 · 캐릭터 그대로(status.emoji · 가장 긴 것 18자) — 그림(emoji.h)을 찾는 열쇠
  char text[128] = "";  // 아이가 적은 말만(status.text) — 그림을 띄울 때 아래 줄에는 이것만
};

// ── 화면은 본 고리 하나만 그린다 ─────────────────────────────────
// 전에는 서버를 기다리는 동안 테두리 고리를 돌리는 일꾼이 따로 있어 자물쇠(gfxLock)로
// 번갈아 그렸다. 그 일꾼이 받는 동안 화면을 밀어 넣던 것이라 걷었고(머리말 '받기와 그리기를
// 겹치지 않는다'), 그리는 쪽이 하나뿐이니 자물쇠도 함께 걷었다.
// ★ 다른 일꾼(nfcTask 등)에서 gfx 를 부르지 말 것 — 다시 자물쇠가 필요해진다.

// 서버를 부르는 동안 상태 불을 노랗게 — 겹쳐 불러도 맨 바깥이 끝날 때 끈다(본 고리에서만 센다).
static int busyDepth = 0;

// ── 백라이트 ─────────────────────────────────────────────────────
// 서버가 주는 밝기는 1~16 이다. PWM 듀티로 옮긴다(1 이어도 보이게 바닥을 둔다).
#if TFT_BLK_ENABLE
static void backlightBegin() {
  ledcAttach(TFT_BLK, BLK_FREQ, BLK_RES);
  ledcWrite(TFT_BLK, 0);  // 패널을 초기화하는 동안은 내려 둔다
}
static void backlightOn(uint8_t level) {
  if (level < 1) level = 1;
  if (level > 16) level = 16;
  const uint8_t duty = (uint8_t)(16 + (uint16_t)(level - 1) * (255 - 16) / 15);
  ledcWrite(TFT_BLK, duty);
}
#else  // TFT_BLK_ENABLE 0 — BLK 선이 없는 모듈. 부르는 자리를 그대로 두고 아무 일도 안 한다.
// 백라이트는 VCC 에 묶여 늘 최대 밝기다. 핀을 건드리지 않는다(PWM 도 잡지 않는다).
static void backlightBegin() {}
static void backlightOn(uint8_t) {}
#endif

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

// 이름표에서 이 UID 의 사진 이름을 찾는다. 서버는 UID 를 대문자로 다듬어 두지만
// (normUid), 들고 있는 값이 어떻든 맞도록 대소문자를 가리지 않고 견준다.
// 카드가 없는 아이는 허브가 UID 자리에 학번을 보내므로 학번으로도 찾는다.
static const char *rosterImgFor(const char *uid) {
  if (!roster || !uid || !uid[0]) return "";
  for (int i = 0; i < rosterCount; i++) {
    if (!strcasecmp(roster[i].uid, uid)) return roster[i].img;
  }
  for (int i = 0; i < rosterCount; i++) {
    if (!strcasecmp(roster[i].sid, uid)) return roster[i].img;
  }
  return "";
}

// 사진(PHOTO_PX)을 화면 버퍼의 (x0, y0) 에 깐다. 비침색인 점은 건너뛴다 — 그 자리는 바탕이
// 그대로 남아 동그라미 밖이 네모로 지저분해지지 않는다.
static void blitPhoto(const uint16_t *src, int x0, int y0) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb || !src) return;
  for (int y = 0; y < PHOTO_PX; y++) {
    const int fy = y0 + y;
    if (fy < 0 || fy >= 240) continue;
    const uint16_t *row = src + y * PHOTO_PX;
    uint16_t *out = fb + (int32_t)fy * 240;
    for (int x = 0; x < PHOTO_PX; x++) {
      const int fx = x0 + x;
      if (fx < 0 || fx >= 240) continue;
      if (row[x] != TRANSPARENT_KEY) out[fx] = row[x];
    }
  }
}

#if PHOTO_CACHE_MAX > 0
// 이 사진이 캐시에 온전히 들어 있나 — 있으면 쓴 시각을 갱신해 돌려준다.
static uint16_t *photoCacheGet(const char *img) {
  if (!photoCache) return nullptr;
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
  if (!photoCache) return nullptr;  // 칸 표를 못 잡았다(setup) — 캐시 없이 돈다
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
  if (live > 20) return;  // 이름표 전원을 들고 있어 수백 줄이 된다 — 적을 때만 늘어놓는다
  const uint32_t nowMs = millis();
  for (int i = 0; i < photoCacheSlots; i++) {
    if (!photoCache[i].valid) continue;
    Serial.printf("  %-40s %lu초 전\n", photoCache[i].img,
                  (unsigned long)((nowMs - photoCache[i].usedAt) / 1000));
  }
}
#endif

static void drawRing(uint16_t color) {
  gfx->drawCircle(CX, CY, R - 1, color);
  gfx->drawCircle(CX, CY, R - 2, color);
}

// ── 서버를 부르는 동안 ───────────────────────────────────────────
// 서버를 부르는 함수들이 들고 다니는 표시. 함수가 어디로 돌아가도 반드시 끈다.
// 화면에는 아무것도 그리지 않는다 — 상태 불만 노랗게 켠다(머리말 '받기와 그리기를
// 겹치지 않는다'). 아이에게는 리더에 대자마자 불이 노래지는 것이 '읽었다' 는 신호다.
// on 이 false 면 아무것도 하지 않는다 — 사진을 미리 받아 둘 때처럼 아이가 기다리지 않는
// 왕복에서는 불을 켜지 않는다(켜면 대기 중에 불이 뜬금없이 깜빡인다).
struct Busy {
  const bool on;
  explicit Busy(bool on_ = true) : on(on_) {
    if (on && busyDepth++ == 0) ledBusy(true);
  }
  ~Busy() {
    if (on && busyDepth > 0 && --busyDepth == 0) ledBusy(false);
  }
};

// ── 화면들 ───────────────────────────────────────────────────────
#include "home.h"   // 홈(대기) 화면 — 시안을 옮긴 그림은 거기에
#include "pages.h"  // 홈 뒤로 돌아가는 네 장 — 최근 내역 · 오늘 출석 · 출석 랭킹 · 포인트 랭킹

// 대기 화면에 지금 띄운 장과, 다음 장으로 넘길 시각
static IdlePage idlePage = PAGE_HOME;
static uint32_t pageNextAt = 0;

// 다시 볼 시각. drawIdle() 을 어디서 불러도 여기서 갱신되므로 부르는 쪽마다 따로
// 챙기지 않아도 된다(loop 는 이 값만 본다).
static uint32_t idleNextAt = 0;
static uint32_t homeSig = 0;  // 마지막으로 그린 홈의 서명 — 같으면 다시 그리지 않는다

// 홈(대기) 화면. force 가 아니면 **바뀐 것이 있을 때만** 그린다 — 분 · 와이파이 · 숫자.
// 예전 대기 화면은 고리를 돌리느라 100ms 마다 화면 전체(115KB)를 밀어 넣었다. 이제 화면이
// 거의 멈춰 있으니 1초에 한 번 서명만 견주고, 그리는 것은 대개 1분에 한 번이다.
static void drawIdle(bool force = true) {
  idleNextAt = millis() + IDLE_FRAME_MS;

  char t[8] = "--:--";
  struct tm now;
  const bool haveTime = getLocalTime(&now, 0);  // NTP 를 아직 못 받았으면 기다리지 않고 false
  if (haveTime) snprintf(t, sizeof(t), "%d:%02d", now.tm_hour, now.tm_min);

  uint32_t sig = 2166136261u;  // FNV-1a — 홈에 보이는 값을 다 섞는다
  auto mix = [&](uint32_t v) { sig = (sig ^ v) * 16777619u; };
  mix(haveTime ? (uint32_t)(now.tm_hour * 60 + now.tm_min) : 0xFFFF);
  mix(online);
  mix(homeData.ok);
  mix((uint32_t)homeData.members);
  mix((uint32_t)homeData.total);
  mix((uint32_t)homeData.earn);
  mix((uint32_t)homeData.spend);
  mix((uint32_t)rosterMembers);
#if PAGES_ENABLE
  mix(idlePage);
  if (idlePage != PAGE_HOME) {  // 그 장의 숫자 — 받을 때마다 통째로 memset 하고 채워 바이트째 섞어도 된다
    const uint8_t *b = (const uint8_t *)&pagesData;
    for (size_t i = 0; i < sizeof(pagesData); i++) mix(b[i]);
  }
#endif
  if (!force && sig == homeSig) return;
  homeSig = sig;

#if PAGES_ENABLE
  pageDraw(idlePage, t, online);
#else
  homeDraw(t, online);
#endif
  gfx->flush();
}

// 작은 줄 하나 — 원 폭에 안 들어가면 뒤를 '...' 로 자른다.
static void drawSmallLine(const char *s, int cy, uint16_t color) {
  if (!s || !s[0]) return;
  char buf[192];
  strlcpy(buf, s, sizeof(buf));
  fitEllipsis(buf, sizeof(buf), cy);
  drawUTF8(buf, cy, PX_NOTE, color);
}

// '지금 기분' 이모티콘 스티커 — 흰 동그라미 위에 32 그림. 그림의 가장자리는 흰 바탕에 섞어
// 구웠다(emoji.h). 비침색은 건너뛴다 — 동그라미 밖으로 네모가 나오지 않게.
static void drawMoodBadge(const uint16_t *art, int cx, int cy) {
  gfx->fillCircle(cx, cy, EMOJI_PX / 2 + 3, RGB565_WHITE);
  gfx->drawCircle(cx, cy, EMOJI_PX / 2 + 3, RGB565_DARKGREY);
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return;
  const int x0 = cx - EMOJI_PX / 2, y0 = cy - EMOJI_PX / 2;
  for (int y = 0; y < EMOJI_PX; y++) {
    const int fy = y0 + y;
    if (fy < 0 || fy >= 240) continue;
    for (int x = 0; x < EMOJI_PX; x++) {
      const int fx = x0 + x;
      if (fx < 0 || fx >= 240) continue;
      const uint16_t c = art[y * EMOJI_PX + x];  // ESP32 는 플래시가 주소에 비쳐 그대로 읽힌다
      if (c != TRANSPARENT_KEY) fb[fy * 240 + fx] = c;
    }
  }
}

// NFC 화면 — 한 장. 사진(96) · 이름 · 지급/사용 포인트 · 사유 · 잔액.
// ★ 여기서는 **서버를 부르지 않는다.** 사진은 presentNfc 가 미리 손에 쥐여 준다(photo).
//   받기가 다 끝난 뒤에 화면 버퍼를 채우고 한 번에 밀어 넣는다.
static void drawNfc(const uint16_t *photo) {
  const bool changed = nfcDelta != 0 || nfcUndo;
  const uint16_t tone = nfcUndo ? COL_UNDO : (nfcDelta > 0 ? COL_GIVE : COL_USE);

  gfx->fillScreen(RGB565_BLACK);
  drawRing(changed ? tone : (nfcKnown ? RGB565_FORESTGREEN : COL_GOLD));

  // 사진 — 없으면(미등록 키링·이름표를 못 받았다) 빈 동그라미 테만 둔다
  const int px0 = CX - PHOTO_PX / 2, py0 = CARD_PHOTO_CY - PHOTO_PX / 2;
  if (photo) {
    blitPhoto(photo, px0, py0);
  } else {
    gfx->drawCircle(CX, CARD_PHOTO_CY, PHOTO_PX / 2 - 1, RGB565_DARKGREY);
  }
  gfx->drawCircle(CX, CARD_PHOTO_CY, PHOTO_PX / 2 + 1, changed ? tone : RGB565_DARKSLATEGREY);
  gfx->drawCircle(CX, CARD_PHOTO_CY, PHOTO_PX / 2 + 2, changed ? tone : RGB565_DARKSLATEGREY);

  // 지금 기분 — 그림이 있는 이모티콘이면 사진 오른쪽 아래에 스티커로(앱의 사진 위 기분과 같은 자리).
  // 포인트가 바뀐 화면에서도 보인다. 이모티콘도 캐릭터(':bear-love:')도 아니면(목록 밖)
  // 아래 줄에 이름으로 쓴다.
  const uint16_t *moodArt = emojiArtOf(nfcMoodEmoji);
  if (!moodArt) moodArt = characterArtOf(nfcMoodEmoji);
  if (moodArt) drawMoodBadge(moodArt, CX + 38, CARD_PHOTO_CY + 30);

  drawUTF8(nfcName[0] ? nfcName : nfcUid, CARD_NAME_CY, PX_NAME, RGB565_WHITE);

  char pts[24], line[64];
  commafy(nfcPoints, pts, sizeof(pts));

  if (changed) {
    // '지급 +5P' · '사용 -3P' · '되돌림 -5P'
    char amt[24];
    commafy(nfcDelta < 0 ? -nfcDelta : nfcDelta, amt, sizeof(amt));
    snprintf(line, sizeof(line), "%s %s%sP", nfcUndo ? "되돌림" : (nfcDelta > 0 ? "지급" : "사용"),
             nfcDelta < 0 ? "-" : "+", amt);
    drawUTF8(line, CARD_MAIN_CY, PX_POINTS, tone);

    // 사유 — 서버가 분류·사유를 비워 보냈으면 무엇이 일어났는지라도 적는다
    const char *why = nfcWhy[0] ? nfcWhy : (nfcUndo ? "내역을 되돌렸어요" : (nfcDelta > 0 ? "포인트 지급" : "포인트 사용"));
    drawSmallLine(why, CARD_SUB_CY, RGB565_WHITE);

    snprintf(line, sizeof(line), "잔액 %s P", pts);
    drawUTF8(line, CARD_SUB2_CY, PX_NOTE, COL_GOLD);
  } else {
    // 바뀐 것이 없는 조회 — 가운데에 잔액, 그 아래 안내문('이미 출석했어요' · 거절 까닭 ·
    // '등록되지 않은 키링')과 지금 기분
    snprintf(line, sizeof(line), "%s P", pts);
    drawUTF8(line, CARD_MAIN_CY, PX_POINTS, COL_GOLD);
    drawSmallLine(nfcNote[0] ? nfcNote : "잔여 포인트", CARD_SUB_CY,
                  nfcNote[0] ? RGB565_CYAN : COL_DIM);
    // 그림을 붙였으면 이름('생각')은 빼고 아이가 적은 말만
    drawSmallLine(moodArt ? nfcMoodText : nfcMood, CARD_SUB2_CY, COL_GOLD);
  }

  gfx->flush();
}

// 일괄 화면 — 제목 + 명단. 240 에는 몇 줄밖에 안 들어가니 나머지는 "외 N명" 으로.
static void drawBulk() {
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
}

// 한 줄 알림 (동기화·오류 등)
static void drawStatus(const char *msg) {
  gfx->fillScreen(RGB565_BLACK);
  drawRing(COL_DIM);
  char line[96];
  strlcpy(line, msg, sizeof(line));
  fitEllipsis(line, sizeof(line), CY);
  drawUTF8(line, CY, PX_NOTE, RGB565_WHITE);
  gfx->flush();
}

static void drawFatal(const char *line1, const char *line2) {
  gfx->fillScreen(RGB565_BLACK);
  drawRing(RGB565_RED);
  drawUTF8(line1, 100, 24, RGB565_RED);
  drawUTF8(line2, 132, PX_NOTE, COL_DIM);
  gfx->flush();
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

#if PAGES_ENABLE
// ── 목록 굴리기 — 때 (그리는 길은 pages.h '목록 굴리기') ───────────
// 최근 내역 · 출석 랭킹 · 포인트 랭킹이 원 안에 다 안 들면, 장에 들어와 위에서 PAGE_SCROLL_HOLD_MS
// 멈춰 있다가 PAGE_SCROLL_PX_S 로 아래 끝까지 굴리고, 끝에서 PAGE_SCROLL_HOLD_MS 멈춘 뒤 다음 장으로
// 간다. 다 들어가면 굴리지 않는다(PAGE_MS).
// ★ 굴리는 동안(위에서 멈춘 때부터 아래 끝에 닿을 때까지) **서버 받기를 쉰다** — 받기 한 번이
//   0.3~1초 고리를 붙잡아 그동안 화면이 멈칫거린다. 아래 끝에서 멈춘 동안 · 다른 장에서 받는다.
//   태깅은 그대로 받는다(아이를 세워 두지 않는다 — 태깅하면 이 장을 떠난다).
static uint32_t listAt0 = 0;  // 이 장에 들어온 때
// 재기 — 한 번 굴릴 때마다 한 줄 찍는다(프레임 수 · 그리기+보내기 시간 · 프레임 사이 가장 긴 틈)
static uint32_t scrollLastAt = 0, scrollFrames = 0, scrollUsSum = 0, scrollUsMax = 0, scrollGapMax = 0;
static bool scrollReported = false;

static uint32_t listScrollMs() {
  const int m = listScrollMax(idlePage);
  return m ? (uint32_t)m * 1000UL / PAGE_SCROLL_PX_S : 0;
}
static uint32_t listPageMs() {
  const uint32_t s = listScrollMs();
  return s ? PAGE_SCROLL_HOLD_MS + s + PAGE_SCROLL_HOLD_MS : PAGE_MS;
}
static void listScrollBegin() {
  listAt0 = millis();
  listOff = 0;
  scrollLastAt = scrollFrames = scrollUsSum = scrollUsMax = scrollGapMax = 0;
  scrollReported = false;
}
// 받기를 쉬어야 하나 — 굴릴 장이고 아래 끝에 닿기 전이다(위에서 멈춰 있는 동안도 쉰다 —
// 그때 시작한 받기가 굴리기 첫머리를 붙잡지 않게).
static bool listScrollQuiet() {
  if (mode != MODE_IDLE || !pageScrolls(idlePage)) return false;
  const uint32_t s = listScrollMs();
  return s && millis() - listAt0 < PAGE_SCROLL_HOLD_MS + s;
}
// 고리마다. 굴린 만큼이 바뀌었을 때만 보이는 칸을 다시 깔아 그 칸만 패널로 보낸다
// (화면 전체 115KB 가 아니라 240 × 130 = 62KB).
static void listScrollTick() {
  if (mode != MODE_IDLE || !pageScrolls(idlePage) || !listBoard) return;
  const uint32_t s = listScrollMs();
  if (!s) return;
  const uint32_t e = millis() - listAt0;
  const int m = listScrollMax(idlePage);
  int off = e <= PAGE_SCROLL_HOLD_MS ? 0 : (int)((e - PAGE_SCROLL_HOLD_MS) * PAGE_SCROLL_PX_S / 1000UL);
  if (off > m) off = m;
  if (off == listOff) {
    if (!scrollReported && off == m && scrollFrames) {
      scrollReported = true;
      static const char *const NAMES[] = {"홈", "최근 내역", "오늘 출석", "출석 랭킹", "포인트 랭킹"};
      Serial.printf("[목록] %s 굴리기 한 번 — %d줄 · %dpx · 프레임 %lu · 그리기+보내기 평균 %.1fms "
                    "최대 %.1fms · 프레임 사이 가장 긴 틈 %lums (보통 %lums)\n",
                    NAMES[idlePage], listRows(idlePage), m, (unsigned long)scrollFrames,
                    scrollUsSum / 1000.0f / scrollFrames, scrollUsMax / 1000.0f,
                    (unsigned long)scrollGapMax, 1000UL / PAGE_SCROLL_PX_S);
    }
    return;
  }
  const uint32_t now = millis(), t0 = micros();
  listOff = off;
  listCompose(idlePage, off);
  panel->draw16bitRGBBitmap(0, PAGE_VIEW_TOP, gfx->getFramebuffer() + PAGE_VIEW_TOP * 240, 240,
                            PAGE_VIEW_H);
  const uint32_t us = micros() - t0;
  scrollFrames++;
  scrollUsSum += us;
  if (us > scrollUsMax) scrollUsMax = us;
  if (scrollLastAt && now - scrollLastAt > scrollGapMax) scrollGapMax = now - scrollLastAt;
  scrollLastAt = now;
}
#else
static bool listScrollQuiet() { return false; }
#endif

// 대기 화면의 다음 장으로 — 받은 것이 없는 장은 건너뛴다(홈은 늘 있다).
static void pageAdvance() {
#if PAGES_ENABLE
  int p = idlePage;
  for (int k = 0; k < PAGE_COUNT; k++) {
    p = (p + 1) % PAGE_COUNT;
    if (pageHasData((IdlePage)p)) break;
  }
  idlePage = (IdlePage)p;
  if (pageScrolls(idlePage)) listScrollBegin();
  // 넘길 장이 홈뿐이면(알림판을 아직 못 받았다) 다시 그리지 않는다 — 서명이 같다.
  // 굴리는 장이면 여기서 그림판을 만든다 — 굴릴 길이(listPageMs)는 그 뒤에 안다.
  drawIdle(false);
  pageNextAt = millis() + (idlePage == PAGE_HOME ? PAGE_HOME_MS : listPageMs());
#endif
}

static void goIdle(bool announce = true) {
#if SOUND_SAY_IDLE
  // 들어올 때만 부른다 — drawIdle() 은 1.5초마다 다시 그리므로 거기 두면 쉬지 않고 떠든다.
  if (mode != MODE_IDLE) soundSay(SND_IDLE_PROMPT);
#endif
  mode = MODE_IDLE;
  nfcPresentWanted = false;  // 그리려던 NFC 화면이 있었으면 접는다(단추로 대기로 돌렸다)
  idlePage = PAGE_HOME;      // 대기로 돌아오면 홈부터 — 방금 바뀐 포인트가 홈에 먼저 보인다
  pageNextAt = millis() + PAGE_HOME_MS;
  drawIdle();
  if (announce) sendState();
}

// 사진 한 장을 손에 쥔다(캐시에 없으면 받는다) — 서버와 말하는 쪽이 아래에 있어 거기 둔다.
static const uint16_t *photoGet(const char *img, bool forTag);
static void memLine();  // 메모리 한 줄 — 아래 '메모리 기록'

// 이번 화면의 포인트 변화 — 지급·사용이 아니면 delta 0 으로 부른다.
struct Change {
  long delta = 0;
  bool undo = false;
  const char *why = "";  // '분류 · 사유'
};

// 화면 상태를 정하고 소리·불을 건다. **그리는 것은 presentNfc** 다(loop 가 곧 부른다).
// 세 갈래(리더·ws·허브)가 모두 이 함수를 지난다 — 허브·ws 는 받는 콜백 안이라 여기서
// 서버(사진)를 부를 수 없고, 리더 쪽도 받기를 다 끝낸 뒤 그리도록 한 자리로 모은다.
static void showNfc(const char *uid, const char *name, long points, bool known,
                    const char *note = "", const Mood *mood = nullptr,
                    const Change &chg = Change()) {
  copyJsonSafe(nfcUid, sizeof(nfcUid), uid);
  // 이름·안내문도 copyJsonSafe 로 받는다 — 관리 화면(브라우저)이 직접 보내 올 수 있어
  // 이름에 따옴표가 섞이면 상태 알림의 JSON 이 그 자리에서 깨진다. 글자 경계에서
  // 자르는 것도 함께 얻는다(strncpy 는 한글 한 자를 반만 베낀다).
  copyJsonSafe(nfcName, sizeof(nfcName), (name && name[0]) ? name : uid);
  copyJsonSafe(nfcNote, sizeof(nfcNote), note);
  copyJsonSafe(nfcMood, sizeof(nfcMood), mood ? mood->line : "");
  strlcpy(nfcMoodEmoji, mood ? mood->emoji : "", sizeof(nfcMoodEmoji));
  strlcpy(nfcMoodText, mood ? mood->text : "", sizeof(nfcMoodText));
  nfcPoints = points;
  nfcKnown = known;
  nfcDelta = chg.delta;
  nfcUndo = chg.undo;
  copyJsonSafe(nfcWhy, sizeof(nfcWhy), chg.why);
  // 이 아이의 사진 이름. 이름표에 없으면(미등록 카드이거나 이름표를 못 받았다) 빈 값이고
  // 그때는 사진 자리에 빈 테만 둔다.
  strlcpy(nfcImg, rosterImgFor(nfcUid), sizeof(nfcImg));

  // 대기 화면 다시 그리기를 여기서 멈춘다(mode) — 그리는 것은 presentNfc.
  mode = MODE_NFC;
  nfcHoldMs = NFC_SHOW_MS;
  nfcUntil = millis() + NFC_SHOW_MS + HTTP_TIMEOUT_MS;  // presentNfc 가 그린 때부터 다시 잰다
  nfcPresentWanted = true;
  // 소리는 그리기보다 **먼저** 건다 — 모듈은 한 번 틀어 주면 혼자 읽으므로(UART 몇 바이트)
  // 사진을 마저 받는 동안 첫 토막이 흐른다. 화면과 겹칠 것이 없다.
  soundSayNote(nfcNote, nfcPoints);
  // 불도 여기 하나에서 고른다. 화면이 접히는 때와 같은 시각에 바탕색으로 내려온다.
  ledTag(nfcNote, nfcKnown, NFC_SHOW_MS);
}

// 정해 둔 NFC 화면을 그린다 — **받을 것을 다 받은 뒤에.**
// 사진은 대개 미리 받아 둔 것이라(loop 의 미리 받기) 서버를 치지 않는다. 캐시에 없을
// 때만(막 등록한 아이·이름표를 막 다시 받았다) 여기서 한 장(4.6KB)을 받고 나서 그린다.
static void presentNfc() {
  nfcPresentWanted = false;
  const uint32_t t0 = millis();
  const uint16_t *photo = photoGet(nfcImg, true);  // 받기 — 여기까지가 와이파이
  drawNfc(photo);                                   // 그리기 — 여기부터는 화면뿐
  if (nfcDelta || nfcUndo) summaryWanted = true;     // 홈의 총 포인트·오늘 +/- 가 바뀌었다
  nfcUntil = millis() + nfcHoldMs;
  Serial.printf("[화면] %s %+ldP%s · 사진 %s (%lums)\n", nfcName, nfcDelta, nfcUndo ? " 되돌림" : "",
                photo ? "있음" : (nfcImg[0] ? "못 받음" : "이름표에 없음"),
                (unsigned long)(millis() - t0));
  memLine();  // 태깅 한 번(서버 왕복 + 그리기) 뒤의 메모리
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
  nfcPresentWanted = false;  // 같은 바퀴에 먼저 온 개별 알림이 있었으면 일괄이 덮는다
  summaryWanted = true;      // 홈 숫자가 바뀌었다
  uint32_t dur = BULK_SHOW_BASE_MS + (uint32_t)bulkCount * BULK_SHOW_PER_NAME_MS;
  if (dur > BULK_SHOW_MAX_MS) dur = BULK_SHOW_MAX_MS;
  nfcUntil = millis() + dur;
  ledFlash(LED_OK, dur);
  drawBulk();
  sendState();
}

// 시험 화면 — 카드 없이 NFC 화면을 띄운다(글자 크기·자리를 눈으로 보려고).
// 시리얼 't' 를 누를 때마다 다음 보기로 넘어가고, 한 번 띄우면 1분 동안 그대로 둔다.
// 사진은 이름표의 아이 것을 차례로 빌려 쓴다(받아 둔 것이 없으면 그 자리에서 한 장 받는다).
static void showTestScreen() {
  static const struct {
    const char *name;
    long points;
    const char *note;
    const char *mood;
    long delta;
    bool undo;
    const char *why;
  } SAMPLES[] = {
      {"홍길동", 1205, "출석 완료 +5P", "기쁨 오늘 소풍 가요", 5, false, "출석"},
      {"김영민", 340, "-30P 간식", "", -30, false, "간식, 컵라면 하나랑 음료수 두 개 샀어요"},
      {"박하늘별님구름햇님보다사랑스러우리", 12345, "+1000P", "", 1000, false, ""},
      {"최유나", 95, "+5P 되돌림", "", -5, true, ""},
      {"이서준", 50, "이미 출석했어요", "생각 어제 늦게 잤어요 그래서 조금 졸려요", 0, false, ""},
      {"", 0, "등록되지 않은 키링", "", 0, false, ""},
  };
  static const int N = (int)(sizeof(SAMPLES) / sizeof(SAMPLES[0]));
  static int idx = 0;

  const auto &s = SAMPLES[idx];
  Serial.printf("[시험] %d/%d  %s · %ld P · %+ldP · %s · %s\n", idx + 1, N, s.name, s.points,
                s.delta, s.why, s.note);
  Mood m;
  strlcpy(m.line, s.mood, sizeof(m.line));
  // 기분이 있는 보기는 그 첫 낱말(이름)에 맞는 이모티콘을 붙여 본다 — '기쁨 오늘 소풍 가요' → 😀
  for (const EmojiArt &e : EMOJI_ARTS) {
    const size_t n = strlen(e.label);
    if (!strncmp(s.mood, e.label, n) && (s.mood[n] == ' ' || !s.mood[n])) {
      strlcpy(m.emoji, e.utf8, sizeof(m.emoji));
      strlcpy(m.text, s.mood[n] ? s.mood + n + 1 : "", sizeof(m.text));
      break;
    }
  }
  Change chg;
  chg.delta = s.delta;
  chg.undo = s.undo;
  chg.why = s.why;
  showNfc("TEST0001", s.name, s.points, s.name[0] != '\0', s.note, &m, chg);
  // 등록된 보기에만 사진을 빌려 단다(미등록 보기는 빈 테 그대로)
  strlcpy(nfcImg, (s.name[0] && rosterCount) ? roster[idx % rosterCount].img : "", sizeof(nfcImg));
  nfcHoldMs = 60000;  // 6초면 들여다볼 새가 없다
  idx = (idx + 1) % N;
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

// ── 사진 한 장 손에 쥐기 (서버 240 → 화면 96) ─────────────────────
// 캐시에 있으면 그것을, 없으면 받아서 캐시에 담고 돌려준다. **화면 버퍼는 건드리지 않는다**
// — 받는 일과 그리는 일을 떼어 두려는 것이다(머리말 '받기와 그리기를 겹치지 않는다').
// 원본은 240 정사각 RGB565(115,200바이트)이고 동그라미 밖은 비침색이다. **한 줄씩 흘려 읽으며**
// 96 으로 줄여 담는다 — 원본을 통째로 들 자리는 필요 없다(한 줄 480바이트).
// 줄일 때는 **이웃 점 고르기**(평균 아님)다. 평균을 내면 동그라미 테두리에서 비침색(자홍)이
// 진짜 색과 섞여 얼굴 둘레에 자홍 테가 생긴다. 캐시에는 비침색까지 그대로 담고 깔 때 건너뛴다.
//
// forTag 가 true 면 아이가 리더 앞에서 기다리는 길이다:
//   · 받는 동안 상태 불을 노랗게 켠다.
//   · 캐시 칸을 못 얻어도(PSRAM 이 모자라다) 임시 칸에 받아 이번 한 번은 띄운다.
// false 면 미리 받아 두는 길(loop) — 불을 켜지 않고, 칸이 없으면 받지 않는다.
static const uint16_t *photoGet(const char *img, bool forTag) {
#if !PHOTO_ENABLE
  return nullptr;
#else
  if (!img || !img[0]) return nullptr;

#if PHOTO_CACHE_MAX > 0
  // 1) 들고 있는 사진이면 내려받지 않는다 — 와이파이가 끊겨 있어도 뜬다.
  const uint16_t *hit = photoCacheGet(img);
  if (hit) {
    if (forTag) photoHit++;
    return hit;
  }
  if (forTag) photoMiss++;
#endif

  if (WiFi.status() != WL_CONNECTED) return nullptr;

  // 2) 받을 자리. 캐시 칸이 없으면 태그 길만 임시 칸(PSRAM 한 장)을 쓴다.
  uint16_t *slot = nullptr;
#if PHOTO_CACHE_MAX > 0
  slot = photoCacheClaim(img);
#endif
  const bool cached = slot != nullptr;
  if (!slot && forTag) {
    static uint16_t *tmp = nullptr;
    if (!tmp) tmp = (uint16_t *)ps_malloc(PHOTO_BYTES);
    slot = tmp;
  }
  if (!slot) return nullptr;

  Busy busy(forTag);

  char url[220];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, img);

  const uint32_t t0 = millis();
  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
#if PHOTO_CACHE_MAX > 0
    if (cached) photoCacheDrop(slot);
#endif
    Serial.printf("[사진] 실패 code=%d (%s)\n", code, img);
    return nullptr;
  }

  // 받을 길이가 240 정사각과 안 맞으면 바로 접는다 — 서버가 다른 크기를 이름표에 실었다
  // (예: ?px= 를 빼먹어 서버 기본값 48 이 왔다). 그대로 읽으면 화면에 잡음만 깔린다.
  const int expect = http.getSize();  // Content-Length (모르면 -1)
  if (expect > 0 && expect != (int)SRC_BYTES) {
    http.end();
#if PHOTO_CACHE_MAX > 0
    if (cached) photoCacheDrop(slot);
#endif
    Serial.printf("[사진] 크기가 다르다 — 받을 길이 %d바이트, 기대 %u바이트(px %d)\n", expect,
                  (unsigned)SRC_BYTES, TALENT_PX);
    return nullptr;
  }

  // 다 모을 때까지 기다려 가며 읽는다.
  // ★ readBytes() 의 시간 끊기에만 맡기면 안 된다 — TLS 버퍼가 잠깐 비는 순간
  //   0 이 돌아와 "끊겼다" 로 잘못 읽는다(240 사진 때 15KB 쯤에서 멈추는 것을 실제로 봤다).
  //   available() 이 0 이어도 연결이 살아 있으면 아직 오는 중이다.
  WiFiClient *st = http.getStreamPtr();
  auto fill = [&](uint8_t *dst, size_t need) -> bool {
    size_t n0 = 0;
    uint32_t idleFrom = millis();
    while (n0 < need) {
      if (st->available() > 0) {
        const int n = st->read(dst + n0, need - n0);
        if (n > 0) {
          n0 += (size_t)n;
          idleFrom = millis();
          continue;
        }
      }
      if (!st->connected() && st->available() == 0) return false;  // 정말 끝났다
      if (millis() - idleFrom > HTTP_TIMEOUT_MS) return false;     // 너무 오래 조용하다
      delay(1);
    }
    return true;
  };
  // 줄일 때 고를 원본 자리 — 칸의 가운데에 가장 가까운 점((2i+1)·240 / (2·96))
  auto srcOf = [](int i) { return (2 * i + 1) * TALENT_PX / (2 * PHOTO_PX); };
  static uint16_t rowBuf[TALENT_PX];  // 원본 한 줄 (480바이트)
  size_t got = 0;
  int dy = 0;
  bool whole = true;
  for (int sy = 0; sy < TALENT_PX; sy++) {
    if (!fill((uint8_t *)rowBuf, sizeof(rowBuf))) {
      whole = false;
      break;
    }
    got += sizeof(rowBuf);
    while (dy < PHOTO_PX && srcOf(dy) == sy) {  // 이 원본 줄을 고르는 화면 줄(대개 0 또는 1줄)
      uint16_t *out = slot + dy * PHOTO_PX;
      for (int dx = 0; dx < PHOTO_PX; dx++) out[dx] = rowBuf[srcOf(dx)];
      dy++;
    }
  }
  http.end();

  if (!whole) {
    // 반쪽 사진은 들고 있지 않는다 — 다음 태깅에 그대로 내보내면 그 아이는 계속 반쪽만 본다.
#if PHOTO_CACHE_MAX > 0
    if (cached) photoCacheDrop(slot);
#endif
    Serial.printf("[사진] 중간에 끊김 (%u/%u바이트, %lums)\n", (unsigned)got,
                  (unsigned)SRC_BYTES, (unsigned long)(millis() - t0));
    return nullptr;
  }
#if PHOTO_CACHE_MAX > 0
  if (cached) photoCacheCommit(slot);
#endif
  // 미리 받기는 수백 장이 이어지므로 한 줄씩 찍지 않는다(끝에 한 줄 — loop)
  if (forTag) {
    Serial.printf("[사진] %s 받았다 (%lums%s)\n", img, (unsigned long)(millis() - t0),
                  cached ? "" : " · 캐시 칸이 없어 임시로");
  }
  return slot;
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

// '분류, 사유' 로 잇는다. 한쪽이 비면 다른 쪽만, 둘 다 비면 빈 문자열.
// ★ 가운뎃점(' · ')을 쓰지 않는다 — U+00B7 이 굴림16 에 없어, 그 한 자 때문에 줄 전체가
//   quan7(8px)로 되돌려 그려져 사유가 깨알만 해졌다(fitEllipsis 머리말과 같은 까닭).
static void joinWhy(char *out, size_t cap, const char *category, const char *reason) {
  category = category ? category : "";
  reason = reason ? reason : "";
  snprintf(out, cap, "%s%s%s", category, (category[0] && reason[0]) ? ", " : "", reason);
}

// 응답의 status 는 { emoji, label, text }. **label 만 쓴다** — emoji 는 😀 같은 날글자라
// 이 폰트에 글리프가 없어 네모(두부)로 뜨고, label 이 그것의 한국어 이름이다.
// 둘을 '기쁨 오늘 소풍 가요' 처럼 한 줄로 잇는다. status 가 없으면 빈 문자열.
static void moodFromJson(JsonVariantConst status, Mood *m) {
  m->line[0] = m->emoji[0] = m->text[0] = '\0';
  if (status.isNull()) return;
  const char *label = status["label"] | "";
  const char *text = status["text"] | "";
  snprintf(m->line, sizeof(m->line), "%s%s%s", label, (label[0] && text[0]) ? " " : "", text);
  strlcpy(m->emoji, status["emoji"] | "", sizeof(m->emoji));
  copyJsonSafe(m->text, sizeof(m->text), text);
}

// ── 서버 설정 받기 (리더 설정 → v2.0) ────────────────────────────
// GET /api/talent/config/v2?device=<이 기기> → { config: { backlight, attendanceMode, attendanceCard } }
// 못 받으면 아무것도 바꾸지 않는다 — 들고 있던 값(또는 펌웨어 기본값)으로 계속 돈다.
// 와이파이가 흔들릴 때마다 밝기가 튀거나 출석모드가 꺼지면 현장에서 더 혼란스럽다.
static void fetchConfig() {
  Busy busy;
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

  const bool wasAttending = cfgAttendMode && cfgAttendCard[0];
  cfgAttendMode = c["attendanceMode"] | false;
  strlcpy(cfgAttendCard, c["attendanceCard"] | "", sizeof(cfgAttendCard));

#if TFT_BLK_ENABLE
  const uint8_t bl = (uint8_t)(c["backlight"] | (int)cfgBacklight);
  if (bl >= 1 && bl <= 16 && bl != cfgBacklight) {  // 바뀔 때만 흔든다
    cfgBacklight = bl;
    backlightOn(cfgBacklight);
  }
#else
  // BLK 선이 없으면 밝기를 **읽지도 않는다** — 들고 있으면 상태·로그에 그 값이 뜨고,
  // 관리 화면에서 만진 사람은 기기가 받아 적용한 줄로 읽는다(config.h 의 TFT_BLK_ENABLE).
#endif
  cfgGotOnce = true;
  // 출석모드가 실제로 바뀌었으면 붙어 있는 쪽(서버·브라우저)에 바로 알린다 —
  // 관리 화면의 스위치를 누른 사람이 "이 기기가 받았구나" 를 그 자리에서 본다.
  // 화면이 바뀐 것이 아니므로 바뀌지 않았을 때는 보내지 않는다(태깅마다 오는 것과
  // 달리 이 알림은 드물어야 한다).
  if (wasAttending != (cfgAttendMode && cfgAttendCard[0])) sendState();
#if TFT_BLK_ENABLE
  Serial.printf("[설정] 밝기 %d/16 · 출석모드 %s%s%s\n", cfgBacklight,
                cfgAttendMode ? "켜짐" : "꺼짐", cfgAttendMode ? " · 카드 " : "",
                cfgAttendMode ? (cfgAttendCard[0] ? cfgAttendCard : "없음(지급 안 함)") : "");
#else
  Serial.printf("[설정] 밝기 조절 없음(BLK 미연결) · 출석모드 %s%s%s\n",
                cfgAttendMode ? "켜짐" : "꺼짐", cfgAttendMode ? " · 카드 " : "",
                cfgAttendMode ? (cfgAttendCard[0] ? cfgAttendCard : "없음(지급 안 함)") : "");
#endif
}

// ── 이름표 받기 (UID → 사진 파일 이름) ──────────────────────────
// GET /api/talent/roster?px=<TALENT_PX> → { data: [ { uid, sid, name, img } ] }
// img 는 그 아이의 기기용 그림 이름이다 — 사진이 있으면 ph-…, 없으면 이름 그림 nm-…
// (240 은 이름을 크게 새긴 동그라미 — 208 에는 이름 그림이 없어 쓰지 않는다).
// 사진 자체는 loop 가 한가할 때 한 장씩 미리 받는다.
//
// ?all=1 — 카드가 없는 아이까지 받는다(uid 가 빈 줄). 관리 화면에서 그 아이에게 포인트를
// 주면 허브가 학번으로 알려 오는데, 그때도 사진이 떠야 한다.
// 이름표는 수백 줄이라 통째로 담으면 heap 이 모자란다. **uid·sid·img 만 남기는 체를 걸어**
// 흘려 읽는다 — name 은 쓰지 않으므로 받는 즉시 버린다.
static bool fetchRoster() {
#if !PHOTO_ENABLE
  return false;
#else
  Busy busy;
  rosterNextAt = millis() + ROSTER_TTL_MS;
  if (WiFi.status() != WL_CONNECTED || !roster) return false;

  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/roster?px=%d&all=1", TALENT_SERVER, TALENT_PX);

  const uint32_t t0 = millis();
  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[이름표] 받기 실패 code=%d\n", code);
    return false;
  }

  JsonDocument filter;
  filter["data"][0]["uid"] = true;
  filter["data"][0]["sid"] = true;
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
    const char *sid = r["sid"] | "";
    const char *img = r["img"] | "";
    if ((!uid[0] && !sid[0]) || !img[0]) continue;  // 찾을 열쇠나 그림이 없는 줄은 담지 않는다
    if (n >= MAX_ROSTER) {
      whole = false;
      break;
    }
    if (strlen(uid) >= ROSTER_UID_LEN || strlen(sid) >= ROSTER_UID_LEN ||
        strlen(img) >= ROSTER_IMG_LEN) {
      whole = false;
      continue;
    }
    strlcpy(roster[n].uid, uid, ROSTER_UID_LEN);
    strlcpy(roster[n].sid, sid, ROSTER_UID_LEN);
    strlcpy(roster[n].img, img, ROSTER_IMG_LEN);
    n++;
  }
  rosterCount = n;
  rosterGotOnce = true;
  // 학번 수 — 카드를 여럿 쥔 아이는 줄이 여럿이라 줄 수가 아니라 학번을 센다(홈의 등록 인원 대신)
  rosterMembers = 0;
  for (int i = 0; i < n; i++) {
    if (!roster[i].sid[0]) continue;
    bool dup = false;
    for (int j = 0; j < i && !dup; j++) dup = !strcmp(roster[j].sid, roster[i].sid);
    if (!dup) rosterMembers++;
  }
  photoPrefetchIdx = 0;  // 이름표가 바뀌었으니 사진을 처음부터 다시 훑는다
  Serial.printf("[이름표] 그림 있는 줄 %d개%s (px %d, %lums)\n", rosterCount,
                whole ? "" : " (칸이 모자라 일부만)", TALENT_PX,
                (unsigned long)(millis() - t0));
  return true;
#endif
}

// ── 홈 숫자 받기 ─────────────────────────────────────────────────
// GET /api/talent/summary → { members, totalPoints, todayEarn, todaySpend }
// (서버 TalentManager.js — 관리 화면 머리 줄과 같은 셈). 못 받으면 들고 있던 값 그대로 —
// 한 번도 못 받았으면 홈이 등록 인원만 이름표로 채우고 나머지는 '-' 로 둔다.
// 아이가 기다리는 왕복이 아니라 상태 불(Busy)은 켜지 않는다.
static void fetchSummary() {
  summaryNextAt = millis() + SUMMARY_TTL_MS;
  if (WiFi.status() != WL_CONNECTED) return;
  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/summary", TALENT_SERVER);
  const uint32_t t0 = millis();
  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[홈] 숫자 받기 실패 code=%d%s\n", code,
                  code == 404 ? " (서버에 /summary 가 아직 없다 — 배포 전)" : "");
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, *http.getStreamPtr());
  http.end();
  if (err || !(doc["success"] | false)) {
    Serial.println("[홈] 숫자 해석 실패");
    return;
  }
  // ★ /summary 가 없는 옛 서버는 이 주소를 GET /api/talent/:sid 로 받아 'SUMMARY 라는 아이' 의
  //   빈 잔액({success:true, uid:"SUMMARY", balance:0})을 돌려준다 — 200 이라 실패로 안 보인다.
  //   그대로 믿으면 홈이 0명 · 0P 로 뜬다. 칸이 있는지로 가른다.
  if (!doc["totalPoints"].is<long>()) {
    Serial.println("[홈] 서버에 /summary 가 아직 없다(옛 서버 — 배포 전) — 숫자 없이 둔다");
    return;
  }
  homeData.members = doc["members"] | 0L;
  homeData.total = doc["totalPoints"] | 0L;
  homeData.earn = doc["todayEarn"] | 0L;
  homeData.spend = doc["todaySpend"] | 0L;
  homeData.ok = true;
  Serial.printf("[홈] 등록 %ld명 · 총 %ld P · 오늘 +%ld / -%ld (%lums)\n", homeData.members,
                homeData.total, homeData.earn, homeData.spend, (unsigned long)(millis() - t0));
}

// ── 대기 화면 네 장의 알림판 받기 ────────────────────────────────
// GET /api/talent/signage → { date, today:[{name}], points:[{rank,name,balance}], month:[{rank,name,count}] }
// GET /api/talent/feed?limit=PAGE_RECENT_N → { data:[{name,delta,category,reason,agoSec}] }
// (서버 TalentManager.js — nfcProject/singleTest 가 480 화면에 띄우는 것과 같은 창구.)
// 못 받으면 들고 있던 것 그대로 — 한 번도 못 받은 장은 돌리지 않는다(pageHasData).
// 홈 숫자처럼 아이가 기다리는 왕복이 아니라 상태 불(Busy)은 켜지 않는다.
// 사진 이름(img)은 체로 걸러 버린다 — 이 화면들은 글자만 그린다.
static void fetchBoard() {
  boardNextAt = millis() + BOARD_TTL_MS;
  if (WiFi.status() != WL_CONNECTED) return;
  const uint32_t t0 = millis();
  PagesData next;
  memcpy(&next, &pagesData, sizeof(next));  // 한쪽만 받으면 다른 쪽은 들고 있던 것으로
  char url[160];

  // ① 알림판 — 오늘 출석 · 포인트 순위 · 이번 달 출석 순위
  snprintf(url, sizeof(url), "%s/api/talent/signage", TALENT_SERVER);
  int code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[알림판] 받기 실패 code=%d\n", code);
  } else {
    JsonDocument filter;
    filter["date"] = true;
    filter["today"][0]["name"] = true;
    for (const char *k : {"points", "month"}) {
      filter[k][0]["name"] = true;
      filter[k][0]["rank"] = true;
    }
    filter["points"][0]["balance"] = true;
    filter["month"][0]["count"] = true;
    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, *http.getStreamPtr(), DeserializationOption::Filter(filter));
    http.end();
    // 이 창구를 모르는 옛 서버는 'signage' 를 학번으로 읽어(GET /:sid) 200 을 준다 — date 가 없으면 아니다
    if (err || !doc["date"].is<const char *>()) {
      Serial.printf("[알림판] 해석 실패%s\n", err ? "" : " (서버에 /signage 가 없다 — 배포 전)");
    } else {
      next.boardOk = true;
      JsonArrayConst today = doc["today"].as<JsonArrayConst>();
      next.todayN = today.size();
      memset(next.today, 0, sizeof(next.today));
      int n = 0;
      for (JsonVariantConst r : today) {
        if (n >= PAGE_TODAY_N) break;
        copyJsonSafe(next.today[n++], PAGE_NAME_LEN, r["name"] | "");
      }
      auto fill = [](JsonArrayConst arr, PageRank *out, const char *valueKey) {
        memset(out, 0, sizeof(PageRank) * PAGE_RANK_N);
        int n = 0;
        for (JsonVariantConst r : arr) {
          if (n >= PAGE_RANK_N) break;
          PageRank &w = out[n++];
          copyJsonSafe(w.name, sizeof(w.name), r["name"] | "");
          w.rank = r["rank"] | n;
          w.value = r[valueKey] | 0L;
        }
        return n;
      };
      next.pointsN = fill(doc["points"].as<JsonArrayConst>(), next.points, "balance");
      next.monthN = fill(doc["month"].as<JsonArrayConst>(), next.month, "count");
      const char *date = doc["date"] | "";  // 'YYYY-MM-DD'
      next.mon = strlen(date) >= 7 ? atoi(date + 5) : 0;
    }
  }

  // ② 최근 내역
  snprintf(url, sizeof(url), "%s/api/talent/feed?limit=%d", TALENT_SERVER, PAGE_RECENT_N);
  code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[최근 내역] 받기 실패 code=%d\n", code);
  } else {
    JsonDocument filter;
    for (const char *k : {"name", "delta", "category", "reason", "agoSec"}) filter["data"][0][k] = true;
    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, *http.getStreamPtr(), DeserializationOption::Filter(filter));
    http.end();
    if (err || !doc["data"].is<JsonArrayConst>()) {
      Serial.println("[최근 내역] 해석 실패");
    } else {
      next.recentOk = true;
      memset(next.recent, 0, sizeof(next.recent));
      int n = 0;
      for (JsonVariantConst r : doc["data"].as<JsonArrayConst>()) {
        if (n >= PAGE_RECENT_N) break;
        PageRecent &w = next.recent[n++];
        copyJsonSafe(w.name, sizeof(w.name), r["name"] | "");
        char why[sizeof(w.why)];
        joinWhy(why, sizeof(why), r["category"] | "", r["reason"] | "");
        copyJsonSafe(w.why, sizeof(w.why), why);
        w.delta = r["delta"] | 0L;
        w.agoSec = r["agoSec"] | 0UL;
      }
      next.recentN = n;
      pagesRecentAt = millis();
    }
  }

  memcpy(&pagesData, &next, sizeof(next));
  Serial.printf("[알림판] 오늘 %d명 · %d월 출석 %d줄 · 포인트 %d줄 · 최근 %d건 (%lums)\n",
                pagesData.todayN, pagesData.mon, pagesData.monthN, pagesData.pointsN,
                pagesData.recentN, (unsigned long)(millis() - t0));
  // 지금 띄운 장이 비었으면(오늘 출석이 지워졌다 등) 홈으로 — 빈 장을 들고 있지 않게
  if (mode == MODE_IDLE && !pageHasData(idlePage)) {
    idlePage = PAGE_HOME;
    pageNextAt = millis() + PAGE_HOME_MS;
  }
}

// ── 이름·잔액·기분 조회 ──────────────────────────────────────────
// GET /api/talent/feed?uid=<UID>&limit=0 → { who: { name, balance, known, status } }
static bool lookupTalent(const char *uid, char *name, size_t nameCap, long *points, bool *known,
                         Mood *mood = nullptr) {
  Busy busy;
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
// POST /api/talent/earn { uid, card, device } → { name, balance, delta, item, category, status }
// item 은 사유(보통 비어 있다 — '비 오는 날' 같은 덧붙임만), category 는 분류('출석').
// ★ category 는 2026-11-05 에 덧붙였다 — 그 전 서버는 싣지 않으므로 비면 '출석' 으로 둔다.
// 금액은 보내지 않는다 — 얼마를 어떤 사유로 줄지는 서버에 등록된 그 카드가 정한다.
// 기기는 현장에 놓인 물건이라 요청을 흉내 내기 쉬운데, 값이 서버에만 있으면 그래 봐야
// 그 카드에 적힌 만큼만 올라간다.
static bool attendEarn(const char *uid, char *name, size_t nameCap, long *points, long *delta,
                       char *why, size_t whyCap, Mood *mood = nullptr, char *deny = nullptr,
                       size_t denyCap = 0) {
  Busy busy;
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
  const char *cat = doc["category"] | "";
  joinWhy(why, whyCap, cat[0] ? cat : "출석", doc["item"] | "");
  // 지급 응답도 status 를 함께 준다 — 출석모드에서는 이 한 번으로 화면을 그리므로,
  // 기분을 따로 물으면 리더 앞에 선 아이를 왕복 한 번만큼 더 기다리게 한다.
  if (mood) moodFromJson(doc["status"], mood);
  Serial.printf("[출석] %s +%ldP (%s) → 잔액 %ld P\n", uid, *delta, why, *points);
  return true;
}

// ── 미등록 카드 알리기 ───────────────────────────────────────────
// POST /api/talent/seen { uid, device } → 관리자 화면의 '미등록카드' 에 뜨게 한다.
// 이걸 부르지 않으면 화면에 "등록되지 않은 키링" 만 뜰 뿐 서버에는 기록이 남지 않아,
// 관리자가 등록할 대상 자체가 생기지 않는다.
static void reportSeen(const char *uid) {
  Busy busy;
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
// UID 하나로 (1) 출석모드면 지급하고 (2) 아니면 이름·잔액만 물어 (3) 화면을 정한다.
// loop 에서 부른다 — 서버 왕복이 있어 콜백 안에서 하지 않는다.
// ★ 여기서는 그리지 않는다. 서버와 주고받기가 다 끝나면 showNfc 가 깃발을 세우고,
//   loop 가 바로 이어서 presentNfc(사진 손에 쥐기 → 한 장 그리기)를 부른다.
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
    char why[64] = "";
    if (attendEarn(uid, name, sizeof(name), &points, &delta, why, sizeof(why), &mood, deny,
                   sizeof(deny))) {
      rememberAttend(uid);
      // 안내문은 소리·불이 읽는 꼴('출석 완료 +5P') 그대로 둔다 — 화면은 delta·why 로 그린다.
      char note[32];
      snprintf(note, sizeof(note), "출석 완료 +%ldP", delta);
      Change chg;
      chg.delta = delta;
      chg.why = why;
      showNfc(uid, name, points, true, note, &mood, chg);
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
  // 사진·이름이 바뀌었다 — 이름표를 다시 받아라(값은 실려 오지 않는다). 받는 것은 loop 가
  // 대기 화면일 때 한다. 바뀐 아이의 img 이름이 달라져 그 한 장만 새로 받는다.
  if (!strcmp(s, "roster")) {
    rosterWanted = true;
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
        // 포인트 변화 — 서버(talentFeed.showLog)는 note 를 '+5P 사유' · '+5P 되돌림' 으로 짓고
        // 분류·사유를 category·reason 으로 따로 싣는다. 금액은 note 맨 앞의 수다.
        // 되돌림의 수는 **지운 내역의** 금액이라(+5 를 지웠다) 잔액은 거꾸로 움직인다(-5).
        const char *note = doc["note"] | "";
        Change chg;
        if (note[0] == '+' || note[0] == '-') chg.delta = atol(note);
        chg.undo = strstr(note, "되돌림") != nullptr;
        if (chg.undo) chg.delta = -chg.delta;
        char why[64];
        joinWhy(why, sizeof(why), doc["category"] | "", doc["reason"] | "");
        // 옛 서버는 category·reason 이 없다 — note 의 '+5P ' 뒤를 사유로 쓴다
        if (!why[0] && !chg.undo && chg.delta) {
          const char *sp = strchr(note, ' ');
          strlcpy(why, sp ? sp + 1 : "", sizeof(why));
        }
        chg.why = why;
        showNfc(uid, doc["name"] | "", doc["points"] | 0L, true, note, &m, chg);
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

// ── 와이파이 (아는 곳 목록, creds.h) ─────────────────────────────
// 목록의 0 번부터 차례로 붙어 본다. 한 곳에 WIFI_TRY_MS 만큼 붙어 보고 안 되면 다음 순위로,
// 다 안 되면 처음으로 돌아가 계속 반복한다. WiFi.begin() 은 기다리지 않고 바로
// 돌아오므로 붙는 동안에도 loop 가 멈추지 않는다 — 카드를 계속 읽어야 하기 때문이다.
//
// 목록은 이제 creds.h 가 짠다(2026-11-06) — 폰으로 넣은 것(NVS)이 앞이고, 구울 때 넣은
// 네 벌은 config.h 의 WIFI_USE_BUILTIN 이 1 일 때만 뒤에 붙는다. 빌드할 때만 넣는 와이파이
//   PLATFORMIO_BUILD_FLAGS='-DWIFI_SSID_OVERRIDE=\"이름\" -DWIFI_PASS_OVERRIDE=\"비번\"'
// 는 예전처럼 **그 하나에만** 붙어 본다.
static bool wifiEverOnline = false;  // 켠 뒤 한 번이라도 붙어 봤나(설정 모드 자동 진입 조건)
static int wifiNoApRounds = 0;       // 목록을 다 돌아도 못 붙은 바퀴 수(붙으면 0 으로)

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

// ── 설정 모드 ──────────────────────────────────────────────────────
// 여기에 끼운 까닭: 위의 화면 그리는 손(drawStatus·drawUTF8·drawRing·goIdle)과 바로 위
// 와이파이 셈(wifiIdx·wifiTried·wifiNoApRounds), creds.h 의 목록을 그대로 쓴다.
// loop 는 둘 중 하나만 돌린다 — 함께 돌면 라디오를 다툰다.
#include "portal.h"

static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiParked = false;
    wifiParkTries = 0;
    wifiNoApRounds = 0;
    wifiEverOnline = true;
    return;
  }
  if (wifiApCount == 0) return;  // 아는 곳이 없다 — 설정 모드가 맡는다

  if (wifiParked) {  // 틈을 둔 뒤 실제로 건다
    if (millis() - wifiParkAt < WIFI_SETTLE_MS) return;
    if (WiFi.begin(wifiAps[wifiIdx].ssid, wifiAps[wifiIdx].pass) == WL_CONNECT_FAILED &&
        wifiParkTries < WIFI_SETTLE_MAX) {
      wifiParkTries++;  // 아직 안 빠졌다 — 틈을 더 주고 다시
      wifiParkAt = millis();
      return;
    }
    wifiParked = false;
    wifiParkTries = 0;
    wifiTryAt = millis();
    wifiTried = true;
    Serial.printf("[와이파이] %d순위 '%s' 에 붙어 보는 중\n", wifiIdx + 1, wifiAps[wifiIdx].ssid);
    return;
  }

  if (wifiTried && millis() - wifiTryAt < WIFI_TRY_MS) return;  // 붙는 중 — 더 기다린다

  if (wifiTried) {  // 실패 → 다음 순위. 처음으로 돌아왔으면 한 바퀴를 다 돈 것이다
    wifiIdx = (wifiIdx + 1) % wifiApCount;
    if (wifiIdx == 0) {
      wifiNoApRounds++;
      Serial.printf("[와이파이] 아는 곳 %d곳에 다 못 붙었다 (%d바퀴)\n", wifiApCount,
                    wifiNoApRounds);
    }
  }
  if (wifiIdx >= wifiApCount) wifiIdx = 0;
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

// ── 메모리 기록 ──────────────────────────────────────────────────
// 한 줄(주기 — config.h 의 MEM_LOG_MS):
//   [메모리] 내부 남음/최저/큰덩이 | DMA 남음/큰덩이 | PSRAM 남음/최저/큰덩이 | 스택 여유 loop·nfc
//   · 내부   mbedTLS·WiFi·WebSocket 이 쓰는 자리. **최저**가 켠 뒤 가장 바닥까지 내려간 값이다.
//   · DMA    내부 RAM 가운데 SPI DMA 가 쓸 수 있는 자리(화면·와이파이 버퍼)
//   · 큰덩이 한 번에 잡을 수 있는 가장 큰 덩이 — 남은 양은 넉넉한데 이것만 작으면 조각났다는 뜻
// 전부(시리얼 'm'): 위에 더해 각 영역의 전체 크기, 이 펌웨어가 PSRAM 에 잡은 것(이름표·사진 캐시·
//   화면 버퍼), 그리고 **모든 태스크**의 스택 여유.
// ★ 본 고리(loop)에서만 부른다 — loop 스택 여유를 uxTaskGetStackHighWaterMark(NULL) 로 재기 때문이다.
static unsigned kb(size_t b) { return (unsigned)((b + 512) / 1024); }

static unsigned stackFree(TaskHandle_t h) {
  return (unsigned)(uxTaskGetStackHighWaterMark(h) * sizeof(StackType_t));
}

static void memLine() {
  Serial.printf("[메모리] 내부 %uK · 최저 %uK · 큰덩이 %uK | DMA %uK · 큰덩이 %uK | "
                "PSRAM %uK · 최저 %uK · 큰덩이 %uK | 스택 여유 loop %u · nfc %u | %lus\n",
                kb(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                kb(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                kb(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                kb(heap_caps_get_free_size(MALLOC_CAP_DMA)),
                kb(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
                kb(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                kb(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)),
                kb(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)), stackFree(nullptr),
                nfcTaskHandle ? stackFree(nfcTaskHandle) : 0u, (unsigned long)(millis() / 1000));
}

static void memReportAll() {
  struct Area {
    const char *name;
    uint32_t caps;
  };
  static const Area AREAS[] = {
      {"내부", MALLOC_CAP_INTERNAL},
      {"DMA", MALLOC_CAP_DMA},
      {"PSRAM", MALLOC_CAP_SPIRAM},
  };
  Serial.printf("[메모리] ── 전부 (켠 지 %lu초) ──\n", (unsigned long)(millis() / 1000));
  for (const Area &a : AREAS) {
    const size_t total = heap_caps_get_total_size(a.caps);
    const size_t freeB = heap_caps_get_free_size(a.caps);
    Serial.printf("  %-6s 전체 %6uK · 쓰는 중 %6uK (%2u%%) · 남음 %6uK · 최저 %6uK · 큰덩이 %6uK\n",
                  a.name, kb(total), kb(total - freeB),
                  total ? (unsigned)((total - freeB) * 100 / total) : 0u, kb(freeB),
                  kb(heap_caps_get_minimum_free_size(a.caps)),
                  kb(heap_caps_get_largest_free_block(a.caps)));
  }

  // 이 펌웨어가 잡은 큰 덩이들
  uint16_t *fb = gfx->getFramebuffer();
  Serial.printf("  화면 버퍼 240x240 %uK · %s\n", kb((size_t)240 * 240 * 2),
                fb ? (esp_ptr_external_ram(fb) ? "PSRAM" : "내부") : "없음");
  Serial.printf("  이름표   %d/%d칸 · %uK\n", rosterCount, MAX_ROSTER,
                kb(sizeof(RosterRow) * MAX_ROSTER));
#if PHOTO_CACHE_MAX > 0
  int live = 0;
  for (int i = 0; i < photoCacheSlots; i++) {
    if (photoCache[i].valid) live++;
  }
  Serial.printf("  사진캐시 %d장 담김 · 자리 %d/%d칸 · 사진 %uK + 칸 표 %uK\n", live, photoCacheSlots,
                PHOTO_CACHE_MAX, kb((size_t)photoCacheSlots * PHOTO_BYTES),
                kb(sizeof(PhotoCacheEntry) * PHOTO_CACHE_MAX));
#endif

  // 태스크마다 스택 여유 — 한 번도 안 쓰고 남은 바이트(0 에 가까우면 넓혀야 한다)
#if configUSE_TRACE_FACILITY
  const UBaseType_t n = uxTaskGetNumberOfTasks();
  TaskStatus_t *ts = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * (n + 2));
  if (ts) {
    const UBaseType_t got = uxTaskGetSystemState(ts, n + 2, nullptr);
    Serial.printf("  태스크 %u개 — 이름 · 스택 여유 · 우선순위 · 코어\n", (unsigned)got);
    for (UBaseType_t i = 0; i < got; i++) {
#if configTASKLIST_INCLUDE_COREID
      const int core = ts[i].xCoreID;
#else
      const int core = -1;
#endif
      Serial.printf("    %-16s %6u · %2u · %s\n", ts[i].pcTaskName,
                    (unsigned)(ts[i].usStackHighWaterMark * sizeof(StackType_t)),
                    (unsigned)ts[i].uxCurrentPriority,
                    core == 0 ? "0" : core == 1 ? "1" : "-");
    }
    free(ts);
  }
#else
  Serial.printf("  스택 여유 loop %u · nfc %u (태스크 목록은 이 빌드에서 못 본다)\n",
                stackFree(nullptr), nfcTaskHandle ? stackFree(nfcTaskHandle) : 0u);
#endif
}

// loop 맨 앞에서 부른다 — 주기가 됐을 때만 한 줄.
static void memService() {
#if MEM_LOG_MS > 0
  static uint32_t at = 0;
  const uint32_t now = millis();
  if (at && now - at < MEM_LOG_MS) return;
  at = now;
  memLine();
#endif
}

// 버튼 짧게 · 터치 — 대기 중이면 다음 장, NFC·일괄 화면이면 대기로
static void nextScreen() {
#if PAGES_ENABLE
  if (mode == MODE_IDLE) {
    pageAdvance();
    return;
  }
#endif
  goIdle();
}

// ── 터치 (남는 GPIO 하나) ─────────────────────────────────────────
// 켤 때 손이 안 닿은 값을 바탕으로 재 두고, 그보다 TOUCH_ON_PCT 넘게 커지면 '닿았다'.
// 닿는 순간 한 번만 넘긴다 — 손을 대고 있어도 계속 넘어가지 않는다(떼야 다음).
// 바탕값은 손이 안 닿은 동안 천천히 따라간다 — 습도·온도·케이스를 닫은 뒤로 값이 흘러도
// 켤 때 잰 값에 묶여 저절로 '닿음' 이 되거나 아예 안 먹는 일이 없게.
#if TOUCH_ENABLE
static uint32_t touchBase = 0;  // 손이 안 닿은 값
static uint32_t touchLast = 0;  // 마지막으로 잰 값('u' 로 본다)
static bool touchDown = false;

static void touchBegin() {
  uint64_t sum = 0;
  // 처음 몇 번은 터치 장치가 막 켜져 높게 나온다 — 버린다(그래도 높으면 touchService 가 바로 내린다)
  for (int i = 0; i < 8; i++) {
    touchRead(TOUCH_PIN);
    delay(5);
  }
  for (int i = 0; i < 16; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(2);
  }
  touchBase = (uint32_t)(sum / 16);
  Serial.printf("Touch  : GPIO%d · 바탕값 %lu (닿음 +%d%% · 뗌 +%d%%)\n", TOUCH_PIN,
                (unsigned long)touchBase, TOUCH_ON_PCT, TOUCH_OFF_PCT);
}

static void touchService() {
  static uint32_t nextAt = 0;
  static int over = 0;
  if ((int32_t)(millis() - nextAt) < 0 || !touchBase) return;
  nextAt = millis() + TOUCH_POLL_MS;

  const uint32_t v = touchRead(TOUCH_PIN);
  touchLast = v;
  const uint32_t on = touchBase + touchBase / 100 * TOUCH_ON_PCT;
  const uint32_t off = touchBase + touchBase / 100 * TOUCH_OFF_PCT;
  if (!touchDown) {
    over = (v > on) ? over + 1 : 0;
    if (over >= TOUCH_DEBOUNCE_N) {
      touchDown = true;
      over = 0;
      Serial.printf("[터치] 닿음 %lu (바탕 %lu)\n", (unsigned long)v, (unsigned long)touchBase);
      if (!portalActive()) nextScreen();
    } else if (v < touchBase) {
      // 손은 값을 올리기만 한다 — 바탕보다 낮으면 그것이 새 바탕이다. 켠 직후 터치 장치가
      // 덜 가라앉아 바탕을 높게(62670 — 안정값 16460 의 네 배) 잡은 일이 있었다(2026-11-06).
      touchBase = v;
    } else if (v < off) {
      touchBase += (v - touchBase) / 64;  // 안 닿은 동안 천천히 따라 오른다
    }
  } else if (v < off) {
    touchDown = false;
  }
}
#else
static void touchBegin() {}
static void touchService() {}
#endif

// ── 버튼 (터치 대신) ────────────────────────────────────────────
// 짧게: 대기 화면으로 · 길게(1.2초 넘게 쥐었다 떼면): 설정 다시 받기
// · PORTAL_HOLD_MS(5초) 쥐고 있으면: 와이파이 설정 모드 (2026-11-06)
//
// ★ 길게 누름이 **손을 뗄 때** 걸리게 바꿨다. 전에는 1.2초가 되는 순간 걸렸는데, 그러면
//   5초까지 쥐고 있는 사람도 1.2초에 설정 받기(수백 ms 짜리 https)로 먼저 빨려 들어간다.
//   1.2초가 되면 화면에 "떼면 설정 받기 · 계속 누르면 와이파이 설정" 을 띄워 갈림길을 알린다.
// 설정 모드에서는: 짧게 누르면 설정 모드를 걷는다(아는 곳이 하나라도 있을 때만).
static void buttonService() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  static bool hinted = false;
  static bool holdFired = false;

  const bool down = (digitalRead(BTN_PIN) == LOW);
  const uint32_t held = millis() - downAt;
  if (down && !wasDown) {
    downAt = millis();
    hinted = holdFired = false;
  }

  if (portalActive()) {  // 설정 모드 — 짧게 누르면 걷는다
    if (!down && wasDown && !holdFired) {
      if (wifiApCount > 0) {
        Serial.println("[버튼] 짧게 누름 — 설정 모드를 걷는다");
        portalStop();
      } else {
        Serial.println("[버튼] 아는 와이파이가 없어 설정 모드를 걷지 않는다");
      }
    }
    wasDown = down;
    return;
  }

#if PORTAL_ENABLE
  if (down && !holdFired && held >= PORTAL_HOLD_MS) {
    holdFired = true;  // 누르는 동안 한 번만
    wasDown = down;
    Serial.println("[버튼] 5초 누름 — 와이파이 설정 모드");
    portalStart("버튼을 5초 눌렀다");
    return;
  }
  if (down && !hinted && held >= LONG_PRESS_MS) {
    hinted = true;
    gfx->fillScreen(RGB565_BLACK);
    drawRing(COL_GOLD);
    drawUTF8("떼면 설정 다시 받기", 104, PX_NOTE, RGB565_WHITE);
    drawUTF8("계속 누르면 와이파이 설정", 136, PX_NOTE, COL_GOLD);
    gfx->flush();
  }
  // 쥐고 있는 동안에는 대기 화면 고리가 그 안내를 덮지 않게 다음 그리기를 미룬다
  if (down && hinted) idleNextAt = millis() + 500;
  const bool longPress = (!down && wasDown && !holdFired && held >= LONG_PRESS_MS);
#else
  const bool longPress = (!down && wasDown && held >= LONG_PRESS_MS);
#endif
  if (longPress) {
    Serial.println("[버튼] 길게 누름 — 설정 다시 받기");
    drawStatus("설정 다시 받는 중");
    soundSay(SND_CONFIG);
    fetchConfig();
    goIdle();
  } else if (!down && wasDown && !holdFired && held < LONG_PRESS_MS) {
    Serial.println("[버튼] 짧게 누름");
    nextScreen();
  }
  wasDown = down;
}

void setup() {
  // 받기 버퍼를 늘린다 — 기본 256바이트라, 시리얼로 넣는 알림 JSON 한 줄(허브와 같은 글자 ·
  // 기분까지 실으면 270바이트를 넘는다)이 loop 가 다른 일을 하는 동안 넘쳐 통째로 사라졌다.
  Serial.setRxBufferSize(1024);
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
  // 불도 **여기서** 켠다 — 아래 2초를 기다리는 동안 노랑이 들어와, 전원을 꽂은 사람이
  // 화면이 뜨기 전에도 상자가 살아 있는 것을 본다.
  ledBegin();
  ledBase(LED_BOOT);
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
#if LED_ENABLE
  Serial.printf("Led    : GPIO%d (v1.0 보드는 38) · 빨강 안됨 · 노랑 하는중 · 초록 됐다\n",
                LED_PIN);
#else
  Serial.println("Led    : 꺼 둠 (LED_ENABLE 0)");
#endif

  uidLock = xSemaphoreCreateMutex();
  pinMode(BTN_PIN, INPUT_PULLUP);
  touchBegin();  // 바탕값을 잰다 — 켜는 순간 터치 자리에 손을 대고 있지 말 것

#if PHOTO_ENABLE
  // 이름표는 PSRAM 에 둔다(400칸 = 약 51KB). 내부 RAM 은 화면 버퍼·TLS·WebSocket 이
  // 나눠 쓰므로 이만한 덩이를 여기서 빼지 않는다.
  roster = (RosterRow *)ps_calloc(MAX_ROSTER, sizeof(RosterRow));
  if (!roster) roster = (RosterRow *)malloc(sizeof(RosterRow) * MAX_ROSTER);
  Serial.printf("Roster : %d칸 %s (%u bytes)\n", MAX_ROSTER, roster ? "확보" : "실패",
                (unsigned)(sizeof(RosterRow) * MAX_ROSTER));
#if PHOTO_CACHE_MAX > 0
  // 칸 표만 여기서 잡는다(PSRAM). 사진 칸은 필요할 때 한 장씩 잡는다.
  photoCache = (PhotoCacheEntry *)ps_calloc(PHOTO_CACHE_MAX, sizeof(PhotoCacheEntry));
  if (!photoCache) Serial.println("Photo  : 캐시 칸 표를 못 잡았다 — 캐시 없이 돈다");
  Serial.printf("Photo  : 캐시 최대 %d장 x %u KB = %u KB · 지금 PSRAM %lu KB\n", PHOTO_CACHE_MAX,
                (unsigned)(PHOTO_BYTES / 1024),
                (unsigned)((size_t)PHOTO_CACHE_MAX * PHOTO_BYTES / 1024),
                (unsigned long)(ESP.getFreePsram() / 1024));
#endif
#endif

  backlightBegin();  // BLK 선이 있으면 내려 둔다(없으면 아무 일도 안 한다)

  if (!gfx->begin(TFT_SPI_HZ)) {
    Serial.println("ERROR: gfx->begin() 실패 — 디스플레이 배선을 확인하세요");
    // 화면이 없으니 알릴 길은 불과 시리얼뿐이다 — 여기서 빨강이 제값을 한다.
    ledBase(LED_FATAL);
    while (true) {
      delay(1000);
    }
  }
  // 초기화 중의 깨진 화면을 가린다 — **백라이트에 기대지 않는다.** BLK 선이 없는
  // 모듈이 흔해서(config.h 의 TFT_BLK_ENABLE), 백라이트로 가리면 그 판에서는 그 한 번이
  // 그대로 비친다. 패널 자체의 화면 끄기로 가리면 선이 있든 없든 똑같다.
  // begin() 은 끝에서 화면을 켜므로(DISPON) 여기서 다시 끄고, 검게 칠한 뒤 켠다.
  // ★ 라이브러리의 displayOff() 를 쓰지 않는 까닭: 그것은 화면 끄기(0x28)가 아니라
  //   잠자기(SLPIN)여서 끄고 켜는 데 120ms+120ms 를 기다린다. 여기서 바라는 것은
  //   '지금은 내보내지 말라' 한 마디뿐이다.워1
  bus->sendCommand(GC9A01_DISPOFF);
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();
  bus->sendCommand(GC9A01_DISPON);
  backlightOn(cfgBacklight);

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
    ledBase(LED_FATAL);
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
#if PN532_OPTIONAL
    // 리더 없이 화면만 시험할 때(config.h 의 PN532_OPTIONAL) — 서지 않고 이어 간다.
    // 허브·ws 로 오는 알림은 그대로 뜬다. 카드만 못 읽는다.
    Serial.println("  PN532_OPTIONAL — 리더 없이 이어 간다(카드는 못 읽는다)");
#else
    drawFatal("PN532", "찾을 수 없음");
    ledBase(LED_FATAL);
    soundSay(SND_READER_FAIL);
    while (true) {
      soundPump(1000);
    }
#endif
  }
  if (ver) {
    Serial.printf("PN532 found: chip PN5%02X, firmware %d.%d\n", (ver >> 24) & 0xFF,
                  (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);

    nfc.SAMConfig();
    nfc.setPassiveActivationRetries(1);  // 카드가 없을 때 빨리 포기하도록

    // 카드 스캔은 전용 일꾼에게 맡긴다(위 nfcTask 주석). 반드시 PN532 가 선 뒤에 올린다.
    xTaskCreatePinnedToCore(nfcTask, "nfc", 4096, nullptr, 1, &nfcTaskHandle, 0);
  }

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
  credsLoad();  // 아는 와이파이 목록 — 폰으로 넣은 것(NVS)
#if PORTAL_ENABLE
  // 아는 와이파이가 하나도 없다 = 처음 켠 기기다. 붙어 볼 곳이 없으니 곧바로 QR 을 띄운다.
  if (wifiApCount == 0) {
    portalStart("아는 와이파이가 없다 — 처음 켰다");
    Serial.printf("Ready. free heap %lu\n", (unsigned long)ESP.getFreeHeap());
    return;
  }
#endif
  drawStatus("WiFi 붙는 중");
#if SOUND_SAY_BOOT
  // ★ soundSay() 가 아니라 **이어 세우는** 쪽이다. 위에서 세운 '리더 준비 중' 이 아직
  //   줄에 그대로 있다(setup 은 soundLoop 을 돌리지 않는다) — 끊는 쪽으로 부르면 그것이
  //   버려져 늘 뒤의 한 토막만 읽혔다. loop 가 돌기 시작하면 둘이 차례로 읽힌다.
  soundSayMore(SND_WIFI);
#endif

  Serial.printf("Ready. free heap %lu\n", (unsigned long)ESP.getFreeHeap());
  memReportAll();  // 켠 직후의 바탕값 — 이것과 견줘 무엇이 얼마나 먹는지 본다
}

void loop() {
  memService();   // 메모리 한 줄(MEM_LOG_MS 마다)
  // 카드 스캔은 여기 없다 — 전용 일꾼(nfcTask)이 돈다. 읽은 UID 는 pendingUid 로 넘어온다.
  soundLoop();    // 다 읽은 토막 다음 것을 밀어 넣는다(기다리지 않는다)
  ledLoop();      // 깜빡임을 돌리고, 잠깐 켜 둔 색이 지났으면 바탕으로 되돌린다

  // ── 설정 모드일 때 ─────────────────────────────────────────────
  // 그동안에는 와이파이·태깅 일을 모두 쉰다 — 라디오를 저쪽(AP·DNS·웹)이 쓰고, 공유기에
  // 붙어 있지 않아 서버에 물을 수도 없다. 읽힌 카드는 쌓이지 않게 그때그때 버린다.
  // 시리얼은 세 글자만 받는다: 'r' 다시 세우기 · 'w' 설정 모드 걷기 · 'W' 넣은 와이파이 지우기.
  if (portalActive()) {
    portalService();
    char drop[32];
    takePendingUid(drop, sizeof(drop));
    buttonService();  // 짧게 누르면 설정 모드를 걷는다(아래 buttonService)
    while (Serial.available()) {
      const int c = Serial.read();
      if (c == 'r' || c == 'R') {
        Serial.println("[명령] 재부팅합니다...");
        Serial.flush();
        ESP.restart();
      } else if (c == 'w') {
        portalStop();
      } else if (c == 'W') {
        credsClear();
      }
    }
    delay(5);
    return;
  }

  wifiService();  // 와이파이 — 붙어 있으면 아무 일도 하지 않는다

#if PORTAL_ENABLE
  // 아는 와이파이에 아예 못 붙으면 설정 모드로 — 새 교회에 가져다 켠 자리다.
  // **한 번이라도 붙었던 뒤에는 들어가지 않는다**: 공유기를 잠깐 껐을 때 기기가 설정 모드로
  // 달아나면 안 된다(돌아오면 wifiService 가 혼자 다시 붙는다).
  if (PORTAL_AUTO_ROUNDS && !wifiEverOnline && wifiNoApRounds >= PORTAL_AUTO_ROUNDS) {
    portalStart("아는 와이파이에 붙지 못했다");
    return;
  }
#endif

  // 붙거나 끊기는 순간에만 다시 그린다(매번 그리면 화면이 깜빡인다)
  const bool nowOnline = (WiFi.status() == WL_CONNECTED);
  if (nowOnline != online) {
    online = nowOnline;
    if (online) {
      Serial.printf("[와이파이] 연결됨 %s (%s) · RSSI %d dBm\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      startServers();  // 붙은 뒤에 mDNS·WebSocket 을 켠다
      if (listScrollQuiet()) {
        // 목록을 굴리는 중이다 — 받기는 굴리기가 끝난 뒤 아래 받기 자리에서(바로 받으면 화면이
        // 1초 남짓 멈춘다 · 와이파이가 끊겼다 붙을 때마다 그랬다)
        cfgWanted = true;
        rosterWanted = true;
      } else {
        fetchConfig();  // 밝기·출석모드를 받아 온다(못 받으면 들고 있던 값으로 돈다)
        fetchRoster();  // UID→사진 이름. 사진 자체는 대기 중에 한 장씩 미리 받는다
      }
      // 홈 위 띠의 시각 — 기기에 시계가 없어 NTP 로 맞춘다(기다리지 않는다. 맞으면 다음 분에 뜬다)
      configTzTime("KST-9", "pool.ntp.org", "time.google.com");
      summaryWanted = true;  // 홈 숫자 — 아래 loop 가 대기 화면일 때 받는다
    } else {
      Serial.println("[와이파이] 끊김");
    }
    if (mode == MODE_IDLE) drawIdle();  // 대기 화면일 때만 갱신(NFC 화면은 건드리지 않는다)
    sendState();
  }

  // 상태 불의 **바탕색** — '쓸 수 있나' 한 가지만 말한다. 붙었으면 초록, 끊겼으면
  // 빨강 깜빡, 아직 한 번도 못 붙었으면 노랑(붙는 중)이다.
  // 노랑을 거두는 자리가 둘이다: 한 번이라도 붙어 봤거나(serversUp — 붙은 뒤 한 번만
  // 켜고 다시 내리지 않는다), 켠 뒤 LED_WIFI_GRACE_MS 가 지났거나. 뒤쪽이 없으면
  // 공유기가 꺼져 있는 상자가 영원히 '준비 중' 인 척한다.
  if (online) {
    ledBase(LED_READY);
  } else if (serversUp || millis() >= LED_WIFI_GRACE_MS) {
    ledBase(LED_OFFLINE);
  }

  if (serversUp) webSocket.loop();  // 받은 것을 처리
  // 다시 붙기·heartbeat 가 여기서 돈다. ★ 끊겨 있으면 목록을 굴리는 동안은 부르지 않는다 —
  // 다시 붙는 TLS 핸드셰이크가 1초 남짓 고리를 붙잡아 화면이 멈춘다(v2 에서 재 보니 1209ms). 붙어 있을
  // 때는 받은 것만 처리해 금방 돌아오므로 그대로 부른다. 쉬는 것은 길어야 한 번 굴리는 동안(20초쯤)이다.
  if (hubBegun && (hubUp || !listScrollQuiet())) hub.loop();

  // 넘어온 태그 — 조회·지급은 수백 ms 가 걸려 콜백 밖(여기)에서 한다
  char uid[32];
  char uidLog[72];
  if (takePendingUid(uid, sizeof(uid), uidLog, sizeof(uidLog))) {
    if (uidLog[0]) Serial.println(uidLog);  // 리더가 읽은 줄 — 찍는 자리는 여기뿐이다
    processTag(uid);  // 서버와 주고받기만 — 화면은 아래 presentNfc 가
  }

  // 정해 둔 NFC 화면을 그린다 — 태그(processTag)든 허브·ws 로 밀려온 알림이든 여기 하나로
  // 온다. 사진이 캐시에 없으면 먼저 받고, **받기가 다 끝난 뒤** 한 장을 그린다.
  if (nfcPresentWanted) presentNfc();

  // 설정 다시 받기 — 주기(5분)가 됐거나, 관리 화면이 "지금 받아라" 를 밀었을 때.
  // 민 쪽이 있으면 그 자리에서 받으므로 밝기·출석모드가 누른 즉시 바뀐다.
  // ★ 이 아래 대기 중 받기들은 목록을 굴리는 동안 쉰다(listScrollQuiet — 화면이 멈칫거리지 않게).
  const bool netQuiet = listScrollQuiet();
  if (online && !netQuiet && (cfgWanted || (int32_t)(millis() - cfgNextAt) >= 0)) {
    cfgWanted = false;
    fetchConfig();
  }

  // 이름표 다시 받기 — 아이가 사진을 바꾸면 img 이름이 달라져 이때 반영된다.
  // NFC·일괄 화면을 띄우고 있을 때는 미룬다(받는 동안 loop 가 멈춰 화면이 굳는다).
  if (online && mode == MODE_IDLE && !netQuiet &&
      (rosterWanted || (int32_t)(millis() - rosterNextAt) >= 0)) {
    rosterWanted = false;
    fetchRoster();
  }

  // 홈 숫자 다시 받기 — 주기(SUMMARY_TTL_MS)거나 포인트가 바뀐 뒤(summaryWanted).
  // 대기 화면일 때만, 다음 태그가 기다리지 않을 때만. 받은 뒤 다시 그리는 것은 drawIdle 이
  // 서명으로 알아서 한다(받는 일과 그리는 일은 같은 loop 에서 차례로 돈다).
  if (online && mode == MODE_IDLE && !pendingUidWaiting() && !netQuiet &&
      (summaryWanted || (int32_t)(millis() - summaryNextAt) >= 0)) {
    if (summaryWanted) boardWanted = true;  // 포인트가 바뀌었다 — 순위·최근 내역도 바뀌었다
    summaryWanted = false;
    fetchSummary();
  }

#if PAGES_ENABLE
  // 대기 화면 네 장의 알림판 — 홈 숫자와 같은 자리 · 같은 조건(대기 화면 · 다음 태그 없음).
  // 홈 숫자를 받은 같은 바퀴에 이어 받지 않고 다음 바퀴로 미룬다 — 그 사이 태그가 오면 먼저 받는다.
  else if (online && mode == MODE_IDLE && !pendingUidWaiting() && !netQuiet &&
           (boardWanted || (int32_t)(millis() - boardNextAt) >= 0)) {
    boardWanted = false;
    fetchBoard();
  }

  // 대기 화면 넘기기 — 단추를 쥐고 있는 동안은(길게 누름 안내를 띄웠다) 넘기지 않는다
  if (mode == MODE_IDLE && (int32_t)(millis() - pageNextAt) >= 0) {
    if (digitalRead(BTN_PIN) == LOW)
      pageNextAt = millis() + 500;
    else
      pageAdvance();
  }
#endif

  buttonService();
  touchService();

  // 시리얼 명령 — 한 글자씩(줄바꿈은 무시)
  while (Serial.available()) {
    const int c = Serial.read();
    // '{' 로 시작하는 한 줄은 **알림 JSON** 이다 — ws·허브로 오는 것과 똑같이 handleCommand 로 간다.
    // 망 없이(핫스팟이 맥과 기기에 같은 주소를 주는 일이 있었다) 허브 알림 화면을 시험하려는 길:
    //   {"uid":"S012","name":"오이안","points":36,"note":"+5P 찬양 율동","category":"특별 미션","reason":"찬양 율동"}
    if (c == '{') {
      static char line[512];
      size_t n = 0;
      line[n++] = '{';
      const uint32_t until = millis() + 500;  // 한 줄이 다 올 때까지 잠깐 기다린다
      while ((int32_t)(millis() - until) < 0 && n < sizeof(line) - 1) {
        const int ch = Serial.read();
        if (ch < 0) {
          delay(1);
          continue;
        }
        if (ch == '\n' || ch == '\r') break;
        line[n++] = (char)ch;
      }
      line[n] = '\0';
      Serial.printf("[시리얼] 알림: %s\n", line);
      handleCommand(line, n);
      continue;
    }
    switch (c) {
      case 'h':
      case 'H':
      case 'n':
      case 'N':
        Serial.println("[명령] 대기 화면");
        goIdle();
        break;
      case 'g':
      case 'G':
        // 대기 화면 다음 장 — 받은 것이 없는 장은 건너뛴다('k' 처럼 시험 숫자를 넣지 않는다)
        if (mode != MODE_IDLE) goIdle();
        pageAdvance();
        Serial.printf("[명령] 대기 화면 %d/%d 장\n", idlePage + 1, PAGE_COUNT);
        break;
      case 'u':
      case 'U':
#if TOUCH_ENABLE
        Serial.printf("[터치] GPIO%d · 지금 %lu · 바탕 %lu (+%ld%%) · 닿음 기준 +%d%% · %s\n", TOUCH_PIN,
                      (unsigned long)touchLast, (unsigned long)touchBase,
                      touchBase ? ((long)touchLast - (long)touchBase) * 100 / (long)touchBase : 0L,
                      TOUCH_ON_PCT, touchDown ? "닿아 있음" : "떨어져 있음");
#else
        Serial.println("[터치] 꺼 둠 (TOUCH_ENABLE 0)");
#endif
        break;
      case 'b':
      case 'B':
        Serial.println("[명령] 알림판 다시 받기");
        fetchBoard();
        break;
      case 'c':
      case 'C':
        Serial.println("[명령] 설정 다시 받기");
        fetchConfig();
        break;
      case 'i':
      case 'I':
#if TFT_BLK_ENABLE
        Serial.printf("[상태] 기기 %s · 밝기 %d/16 · 출석모드 %s · 출석 카드 %s · 설정 %s\n",
                      TALENT_DEVICE_ID, cfgBacklight, cfgAttendMode ? "켜짐" : "꺼짐",
                      cfgAttendCard[0] ? cfgAttendCard : "없음",
                      cfgGotOnce ? "받음" : "못 받음(기본값으로 동작)");
#else
        Serial.printf("[상태] 기기 %s · 밝기 조절 없음(BLK 미연결) · 출석모드 %s · "
                      "출석 카드 %s · 설정 %s\n",
                      TALENT_DEVICE_ID, cfgAttendMode ? "켜짐" : "꺼짐",
                      cfgAttendCard[0] ? cfgAttendCard : "없음",
                      cfgGotOnce ? "받음" : "못 받음(기본값으로 동작)");
#endif
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
        ledReport();
        // 일꾼 스택에서 **한 번도 안 쓰고 남은** 바이트. 0 에 가까우면 넓혀야 한다.
        Serial.printf("[상태] nfc 일꾼 스택 여유 %u bytes (잡은 것 4096)\n",
                      nfcTaskHandle ? (unsigned)(uxTaskGetStackHighWaterMark(nfcTaskHandle) *
                                                 sizeof(StackType_t))
                                    : 0u);
        break;
      case 'm':
      case 'M':
        memReportAll();
        break;
      // 화면 버퍼를 통째로 시리얼로 — 맥에서 PNG 로 바꿔 눈으로 본다(화면을 못 보는 자리에서).
      // '<<FB 240 240>>' 줄 뒤에 RGB565(리틀엔디안) 115,200바이트, 그 뒤 '<<END>>'.
      // ★ 평소 Serial 은 버퍼가 차면 버린다(setTxTimeoutMs(0)) — 여기서는 자리가 날 때까지
      //   기다렸다 쓴다. 버리면 그림이 밀려 깨진다.
      case 'p': {
        uint16_t *fb = gfx->getFramebuffer();
        if (!fb) break;
        Serial.print("\n<<FB 240 240>>\n");
        const uint8_t *b = (const uint8_t *)fb;
        const size_t n = (size_t)240 * 240 * 2;
        for (size_t o = 0; o < n;) {
          const int room = Serial.availableForWrite();
          if (room <= 0) {
            delay(1);
            continue;
          }
          const size_t k = (n - o < (size_t)room) ? n - o : (size_t)room;
          o += Serial.write(b + o, k);
        }
        Serial.print("\n<<END>>\n");
        break;
      }
      // 홈 화면 시험 숫자 — 서버의 /summary 없이 배치를 본다(다음에 받으면 진짜 값으로 바뀐다)
      case 'k':
        homeData = {true, 72, 496, 0, 0};
        Serial.println("[홈] 시험 숫자 — 등록 72 · 총 496 · 오늘 +0 / -0");
        drawIdle();
        break;
      case 'f':
      case 'F':
        Serial.println("[명령] 이름표 다시 받기");
        fetchRoster();
        for (int i = 0; i < rosterCount; i++) {
          Serial.printf("  %-16s %-8s %s\n", roster[i].uid[0] ? roster[i].uid : "-", roster[i].sid,
                        roster[i].img);
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
          // 카드 없는 아이는 학번으로 묻는다(카드가 있어야 출석 지급은 된다)
          const char *key = roster[demoIdx].uid[0] ? roster[demoIdx].uid : roster[demoIdx].sid;
          Serial.printf("[시연] %d/%d  %s\n", demoIdx + 1, rosterCount, key);
          processTag(key);
          nfcHoldMs = 30000;  // 들여다볼 시간을 준다(presentNfc 가 그린 때부터)
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
      // 스피커·SD·번호를 눈이 아니라 귀로 확인한다. 한 번 누를 때마다 다음 토막.
      case 'v':
      case 'V':
        soundTestNext();
        break;
      // 상태 불을 눈으로 확인한다 — 핀이 맞는지, 어느 색이 어느 상태인지.
      // 한 번 누르면 그 색을 4초 보여 주고 제 상태로 돌아간다.
      case 'l':
      case 'L':
        ledTestNext();
        break;
      case 'w':
        portalStart("시리얼 명령");
        return;  // 설정 모드 — 이 바퀴는 여기서 접는다
      case 'W':
        credsClear();
        Serial.println("[명령] 다시 세우면 반영됩니다");
        break;
      case 'j':
      case 'J':
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
        Serial.println("[명령] h 대기 · g 대기 다음 장 · u 터치 값 · b 알림판 받기 · c 설정 받기 · f 이름표 받기+목록 · d 실제 조회 시연 · "
                       "i 상태 · m 메모리 전부 · s 출석 목록 · x 사진 캐시 비우기 · t 시험 화면 · "
                       "v 소리 토막 · l 상태 불 · w 와이파이 설정 모드 · W 넣은 와이파이 지우기 · "
                       "j 와이파이 다시 붙기 · r 재부팅");
        break;
      default:
        break;
    }
  }

  // NFC·일괄 화면은 얼마 뒤 자동으로 대기 화면으로 돌아간다
  if ((mode == MODE_NFC || mode == MODE_BULK) && (int32_t)(millis() - nfcUntil) >= 0) goIdle();

  // 들고 있는 연결이 녹슬지 않게 조용히 한 번 찌른다 — 대기 화면일 때만.
  if (online && mode == MODE_IDLE && !netQuiet && (int32_t)(millis() - httpWarmAt) >= 0) httpWarm();

#if PHOTO_CACHE_MAX > 0
  // 사진 미리 받아 두기 — 이름표를 받은 뒤 한 장씩 캐시에 채운다. 줄여 담으면 한 장이 18KB 라
  // 캐시(PHOTO_CACHE_MAX · 200장)에 한 교회 전원이 들어간다. 태깅할 때는 받지 않고 바로 깔린다.
  // 한 장 받는 데 0.3초쯤(원본 115KB) — 그동안 대기 화면 고리가 멈춰 있다 돈다.
  // ★ 대기 화면일 때 한 번에 한 장만 — 받는 동안 loop 가 멈추므로 길게 붙잡지 않는다.
  //   받는 것과 대기 화면 그리기는 같은 loop 에서 차례로 돌아 겹치지 않는다.
  if (online && mode == MODE_IDLE && !pendingUidWaiting() && !nfcPresentWanted && !netQuiet &&
      photoPrefetchIdx < rosterCount) {
    const int i = photoPrefetchIdx++;
    if (!photoCacheGet(roster[i].img)) photoGet(roster[i].img, false);
    if (photoPrefetchIdx >= rosterCount) {
      Serial.printf("[사진] 미리 받기 끝 (%d명)\n", rosterCount);
      memLine();  // 사진을 다 담은 뒤의 PSRAM
    }
  }
#endif

  // 대기 화면은 고리가 돌아야 해서 다시 그린다 — 다만 **프레임을 제한한다**.
  // 전에는 루프마다 그렸다. drawIdle() 은 화면을 통째로 다시 그리고 115,200바이트를
  // 패널로 밀어 넣어 그만큼 루프가 길어졌다(그래서 밖에서 보낸 것에 답하는 데까지
  // 55ms 단위로 끊겼다).
#if PAGES_ENABLE
  listScrollTick();  // 목록을 굴리는 중이면 보이는 칸만 — 아래 drawIdle 은 분이 바뀔 때만 그린다
#endif
  if (mode == MODE_IDLE && (int32_t)(millis() - idleNextAt) >= 0) drawIdle(false);
}
