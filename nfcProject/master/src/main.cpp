// nfcProject — WiFi WebSocket 디스플레이 (CST820 원형)
//
// 보드: LILYGO T-RGB 2.1" 원형 (ESP32-S3 · ST7701S RGB 480×480 · CST820 터치)
// 이 보드는 화면만 담당한다 —
//   1) 아는 와이파이에 붙는다 (폰으로 넣은 것이 앞, ChurchSecrets.h 의 네 벌이 뒤 — creds.h)
//   2) nfc-display.local 로 자기 이름을 알린다(mDNS)/Users/kimyongmin/workspace/arduino/nfcProject/config.h
//   3) 포트 81 에 WebSocket 서버를 연다
//   4) 외부(NFC 시스템)가 붙어 명령을 보내면 그에 맞춰 화면을 바꾼다
//
// ── 화면 세 가지 ────────────────────────────────────────────────────
//   1) 대기 화면  : 항상 '사용 활성'(useEnable) 그림에서 시작한다. 비활성 그림은 더 쓰지 않는다.
//                   화면을 짧게 누르면(또는 시리얼 'n') microSD 에 받아 둔 사진이 순서대로 넘어간다 —
//                   활성 → 사진1 → 사진2 → … → 사진N → 다시 활성.
//   2) 원격 전환  : WebSocket 으로 enable(=활성으로) / toggle·next(=다음 사진) 가 오면 바뀐다.
//   3) NFC 화면   : 리더(nfcProjectClient)가 카드 UID 를 보내면, 이 기기가 서버에 이름·잔액을
//                   물어 그 UID 의 사진을 전체화면으로 덮고 위에 이름(상단)·잔여 포인트(하단)를 얹는다.
//                   그 아이가 펀펀포인트 앱이나 조회 화면에 적어 둔 **'지금 기분' 한 줄**이 있으면
//                   이름 바로 아래에 금색으로 한 줄 더 얹는다(없으면 띠를 넓히지도 않는다).
//                   이모티콘은 서버가 한국어 이름으로 바꿔 보낸다 — 이 화면의 폰트에 😀 글리프가 없다.
//                   몇 초 뒤(NFC_SHOW_MS) 자동으로 대기 화면으로 돌아간다.
//                   UID 사진은 microSD 의 /talent/<UID>.565 를 읽는다(없으면 내장 MEMBERS[]).
//
// ── 서버 설정 · 출석모드 (관리자 화면 '리더 설정 → v2.0') ───────────
//   부팅해 와이파이에 붙으면 GET /api/talent/config/v2 로 설정을 받고, 그 뒤 CONFIG_TTL_MS 마다 다시 받는다.
//     · 밝기(backlight 1~16)     받는 즉시 백라이트에 적용된다
//     · 출석모드(attendanceMode) 켜면 키링을 댄 그 자리에서 바로 출석 포인트가 지급된다
//     · 출석 카드(attendanceCard) 얼마를 어떤 사유로 줄지는 서버에 등록된 이 카드가 정한다.
//                                비어 있으면 지급하지 않고 사진·잔액만 띄운다.
//   ★ 관리 화면에서 값을 바꾸면 서버가 허브로 {"state":"config"} 를 밀어 준다 — 주기를
//     기다리지 않고 그 자리에서 다시 받는다(상단 바 풍선의 '출석모드' 스위치가 그것이다).
//     받은 뒤 출석모드가 바뀌었으면 지금 상태를 알려 준다(buildState 의 `attend`).
//   서버와 이야기하는 쪽은 이 기기 하나다 — 리더는 읽은 UID 를 넘기기만 한다.
//   한 태깅이 두 곳에서 처리되면 같은 출석이 두 번 올라간다.
//   같은 키링이 ATTEND_COOLDOWN_MS 안에 또 오면 "이미 출석했어요" 만 띄우고 주지 않는다
//   (기기에 시계가 없어 "하루 한 번" 을 알 수 없다 — 시간 간격으로 막는다).
//
// ── 화면이 밀릴 때 (가끔 그림이 한쪽으로 쓸리는 것) ─────────────────
//   RGB 패널은 PSRAM 의 그림을 쉬지 않고 읽어 내보낸다. 그 읽기가 한 번이라도 늦으면
//   그 줄부터 그림이 밀린다(다음 VSYNC 에 제자리로 돌아온다 — 그래서 가끔만 보인다).
//   늦어지는 까닭은 둘이다. 1) 플래시에 쓰는 동안 캐시가 꺼져 갱신 인터럽트가 못 돈다
//   → WiFi.persistent(false) 로 와이파이가 NVS 에 쓰지 않게 막았다.
//   2) PSRAM 을 크게 주무르는 일이 겹친다 → 중계 버퍼를 스무 줄로 늘리고,
//      화면 한 장을 옮기는 동안에는 고리(아래)를 쉬게 했다.
//
// ── 불러오는 중 표시 ────────────────────────────────────────────────
//   서버에 무엇을 물을 때(조회·출석·설정·사진) 화면 테두리를 금색 고리가 돈다.
//   따로 띄운 일꾼이 그린다 — 서버 왕복이 loop 를 붙잡고 있는 동안에도 돌아간다.
//
// ── 사진 동기화 (화면 길게 누르기) ──────────────────────────────────
//   화면을 LONG_PRESS_MS 이상 누르면 펀펀포인트 서버(config.h 의 TALENT_SERVER)에서
//   이름표(GET /api/talent/roster?px=480)를 받아, 사진이 있는 UID 마다 480 원형 사진
//   (GET /api/talent/photo/device/<img>)을 내려받아 microSD 에 /talent/<UID>.565 로 저장한다.
//   요청에는 x-talent-key 헤더(TALENT_DEVICE_KEY)를 실어 서버가 기기를 확인한다.
//   이미 받은 사진은 다시 받지 않는다 — 이름표의 img(ph-<사진 해시>-480.565)는 사진이 바뀔 때만
//   달라지므로, 받을 때 그 값을 /talent/<UID>.ver 에 적어 두고 다음 동기화에서 같으면 건너뛴다.
//   이름표에 없는 사진은 지운다 — 아이가 빠지거나 사진을 내리면 SD 의 /talent/<UID>.565(와 .ver)
//   도 같이 치운다(받다 만 .tmp 도). 카드가 차오르거나 없는 아이가 넘겨 볼 목록에 남지 않게.
//   단, 이름표에 사진이 한 장도 없으면 아무것도 지우지 않는다 — 빈 이름표에 카드를 비우지 않게.
//   서버가 리틀엔디안 RGB565 로 주므로 파일 바이트를 그대로 화면 버퍼로 읽으면 된다.
//
// ── 화면이 켜지는 순서 (v1.0 과 같다) ───────────────────────────────
//   1) XL9535 확장칩   화면 명령 줄(CS·SCK·MOSI)과 전원 인에이블이 여기 붙어 있다
//   2) RGB 패널        ESP32 가 16개 데이터선으로 픽셀을 계속 흘려보낸다(PSRAM 프레임버퍼)
//   3) 초기화 표       ST7701S 에 "이런 화면이다" 를 알려 준다(2.1인치는 type4)
//   4) 백라이트        GPIO46 을 켜야 보인다. 밝기는 '흔든 횟수' 로 정한다(BL_LEVEL)
//
// ── 리더는 두 갈래로 온다 (선 · 와이파이) ───────────────────────────
//   선(I2C)    리더(ESP32-C3)를 옆면 4핀 커넥터에 SDA·SCL·GND 로 문다(config.h 배선).
//              리더가 이 버스의 슬레이브(0x30)라 먼저 말을 못 건다 — 이쪽이 LINK_POLL_MS
//              마다 "줄 것 있나" 하고 읽어 온다(linkService). 결과는 이쪽이 써 넣는다.
//              공유기·mDNS 를 안 거쳐 즉시 닿고, 망이 죽어도 태깅이 된다. 리더는 이 길을 먼저 쓴다.
//   WebSocket  선이 없거나 조용하면 리더가 와이파이로 넘어와 붙는다. 브라우저·폰도 여기로 붙는다.
//   두 길에 흐르는 **글자는 똑같다**(아래 규약) — 받는 곳도 handleCommand 하나다.
//   다만 선으로 되돌려 주는 답은 I2C 한 덩이에 들어가야 해서 mode·online 을 뺀 짧은
//   꼴이다(linkSendResult). 리더는 uid 로 자기 답인지 가리므로 그래도 알아본다.
//
// ── WebSocket 규약 (ws://nfc-display.local:81/) ─────────────────────
//   보내는 쪽 → 보드
//     · 대기 화면 사진 바꾸기
//         JSON  {"state":"enable"} / {"state":"toggle"} / {"state":"next"} / {"state":"prev"}
//         글자  "enable" / "toggle" / "next" / "prev"   (enable = 활성 화면으로, toggle·next = 다음 사진)
//         ※ "disable"·{"enabled":false} 는 예전 규약이다 — 비활성 그림을 걷어내 활성 화면으로 간다.
//     · NFC 태그 알림 — **UID 하나면 된다**. 이름·잔액은 이 기기가 서버에 물어 채운다.
//         JSON  {"uid":"04A1B2C3"}
//         (옛 규약 {"uid":…,"name":…,"points":…} 도 받는다 — 그때는 묻지 않고 그대로 띄운다)
//   보드 → 붙은 쪽 : 상태가 바뀌거나 새로 붙을 때마다
//     · JSON  {"mode":"idle","photo":"enable","index":0,"count":12,"online":true}   (photo = enable 또는 사진 UID)
//     · JSON  {"mode":"nfc","uid":"04A1B2C3","name":"홍길동","points":1205,
//              "known":true,"note":"출석 완료 +5P","mood":"기쁨 오늘 소풍 가요","online":true}
//       리더는 이 응답으로 소리를 가른다 — 서버와 말하지 않으므로 결과를 알 길이 이것뿐이다.
//       mood 는 그 아이의 '지금 기분'(적어 둔 것이 없으면 빈 문자열). **선(I2C)으로는 가지 않는다** —
//       리더는 소리만 가르고 화면이 없어서, 쓰지 않는 칸으로 한 덩이를 키울 이유가 없다.
//
// ── 빌드 (Arduino IDE 도구 메뉴) ────────────────────────────────────
//   보드: ESP32S3 Dev Module   PSRAM: OPI PSRAM   Flash Size: 16MB
//   Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)  ← 그림과 한글 폰트가 플래시에 들어간다
//   USB CDC On Boot: Enabled
//   라이브러리: GFX Library for Arduino · ArduinoJson · WebSockets(by Markus Sattler)
//   한글: U8g2 에 딸린 굴림 16px(u8g2_font_gulim16_t_korean2)으로 그린다 — 늘려도 획이 산다.
//        굴림에 없는 드문 글자(KS X 1001 밖)가 섞인 줄만 GFX 의 u8g2_font_quan7_h_cjk(한글 전체)로 그린다.
//
//   ※ LilyGo-T-RGB 라이브러리(LV_Helper.h·LilyGo_RGBPanel.h)는 넣지 말 것 — lvgl/SensorLib
//     버전 충돌로 빌드가 깨진다. 이 스케치는 Arduino_GFX 로 직접 그린다(v1.0 과 같은 이유).
//
// ── 와이파이를 폰으로 바꾸기 (설정 모드, 2026-10-26) ─────────────────
//   새 교회로 기기를 가져갔을 때 노트북 없이 와이파이를 바꿀 수 있다. **앱은 없다** —
//   이 보드가 스스로 와이파이(SoftAP)를 띄우고, 화면에 뜬 QR 을 폰 카메라로 찍으면
//   그 AP 에 붙어 설정 창이 저절로 뜬다(캡티브 포털). 거기서 교회 와이파이를 고르고
//   비밀번호를 넣으면, 보드가 그 자리에서 붙어 보고 **맞을 때만** 플래시(NVS)에 적는다.
//   들어가는 길 세 가지: 켤 때 화면 누르고 있기 · 아는 곳이 안 보일 때 저절로 · 시리얼 'w'.
//   자세한 것은 portal.h(설정 모드)와 creds.h(목록 규칙) 머리말에.
//
// ── 시리얼 명령 (115200) ────────────────────────────────────────────
//   n 다음 사진 · p 이전 사진 · h 활성 화면으로 · s 사진 목록 · l SD 파일 목록 · r 재부팅 · ? 도움말
//   w 와이파이 설정 모드로 들어가기(설정 모드에서 'w' 를 또 치면 걷는다)
//   W 폰으로 넣은 와이파이를 모두 잊기(구울 때 넣은 네 벌은 남는다 — 다시 세우면 반영)
//   t 시험 NFC 화면(카드·서버 없이 이름·포인트·기분을 띄워 글자를 본다 — 누를 때마다 다음 보기)
//   g 불러오는 중 고리를 3초 돌려 본다(서버 없이 확인용)
//
// ── 그림 바꾸기 ─────────────────────────────────────────────────────
// 그림 헤더는 tools/img2rgb565_dither.py 로 만든다(v1.0 머리말 참고). 자기 그림으로 바꾸려면
// useEnable.h 를 --var USEENABLE 로 다시 만들어 이 폴더에 덮어쓴다.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>   // 서버(funfunServer)에 붙어 변화를 받아 온다
#include <HTTPClient.h>          // 서버에서 사진 내려받기(길게 누르기)
#include <WiFiClientSecure.h>    // https 다운로드
#include <SD_MMC.h>             // microSD (SDMMC 1-bit) 저장
#include <U8g2lib.h>             // 한글 u8g2 폰트(굴림·quan7)·UTF8 출력을 켜고, 굴림 폰트도 여기서 온다
#include <Arduino_GFX_Library.h>
#include "config.h"              // WIFI_SSID · WIFI_PASSWORD · MDNS_HOSTNAME · WS_PORT
#include "creds.h"               // 아는 와이파이 목록(구울 때 넣은 네 벌 + 폰으로 넣은 네 벌)
#include "useEnable.h"           // USEENABLE[480×480]  — 사용 활성(대기 화면의 첫 장)
#include "connected.h"
#include <esp_now.h>
#include <esp_wifi.h>           // CONNECTED[50×35]    — 연결 아이콘(와이파이에 붙었을 때만 겹친다)

// ── 루프 태스크 스택을 16KB 로 (기본 8KB) ───────────────────────────
// **서버와 https 로 말하는 일을 전부 loop() 에서 한다**(설정·이름표·사진·조회·지급).
// mbedTLS 의 악수, 그중에서도 인증서 사슬을 뜯는 자리(`mbedtls_ssl_parse_certificate`)가
// 스택을 크게 먹는데, 거기에 syncPhotos 의 지역변수까지 겹치면 기본 8KB 가 아슬아슬하다.
//
// ★ 2026-10-11 에 실제로 넘쳤다. 서버를 youthvision.co.kr → funfun.youthvision.co.kr 로
//   돌린 순간 **부팅 루프**가 됐다(Stack canary watchpoint triggered (loopTask) — 백트레이스가
//   syncPhotos → HTTPClient::GET → mbedtls_ssl_parse_certificate 였다). 주소만 바꿨는데
//   깨진 까닭은 **호스트가 다르면 인증서 사슬도 다르기 때문**이다 — 새 사슬이 조금 더
//   커서 모자라던 여유를 넘겼다. 옛 주소로 돌아가도 여유가 없는 것은 그대로이니,
//   주소를 되돌리는 것은 고치는 것이 아니다.
//
// 스택은 **쓰는 만큼만** 실제 메모리를 차지하는 것이 아니라 태스크를 만들 때 통째로 잡는다 —
// 8KB 를 더 쓰는 셈이지만 내부 RAM 에 여유가 있고(빌드 보고의 RAM 21%), 부팅조차 못 하는
// 것보다 낫다. 줄이려면 https 를 loop 밖의 전용 태스크로 옮겨야 하는데, 그러면 화면·터치와
// 주고받을 것이 생겨 지금 구조(한 줄기로 흐르는 loop)가 통째로 바뀐다.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ── 연결 아이콘 위치 ────────────────────────────────────────────────
// (CONN_X, CONN_Y) 가 아이콘의 왼쪽 위 모서리. 화면은 480×480, 왼쪽 위가 (0, 0).
static const int CONN_X = 307;
static const int CONN_Y = 30;

// ── client 연결 상태 점 ─────────────────────────────────────────────
// 화면 상단 가운데에 작은 점 하나로 client(NFC 시스템) 연결 상태를 보여 준다.
//   초록 = client 가 WebSocket 으로 붙어 있다(정상 연결)
//   노랑 = 와이파이·서버는 떴는데 아직 붙은 client 가 없다(대기 중)
//   빨강 = 와이파이가 없어 서버에 붙을 수 없다(연결 실패)
static const int DOT_CX = 240;   // 점 가운데 x (화면 상단 가운데)
static const int DOT_CY = 22;    // 점 가운데 y
static const int DOT_R  = 8;     // 점 반지름
#define DOT_GREEN  0x07E0
#define DOT_YELLOW 0xFFE0
#define DOT_RED    0xF800

// 점의 세 상태. **여기서 선언한다** — 아래(drawLinkDot 옆)에 두면 안 된다.
// 아두이노는 빌드할 때 함수 원형을 스케치 맨 앞(첫 함수 정의 자리)에 끼워 넣는데,
// LinkState 를 돌려주는 linkState() 의 원형이 이 선언보다 앞에 놓여 "타입을 모른다" 로 깨진다.
enum LinkState { LINK_FAIL, LINK_WAIT, LINK_OK };

// ── 핀 ──────────────────────────────────────────────────────────────
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL  48
#define PIN_BL       46          // 백라이트
#define TOUCH_ADDR   0x15        // CST820 의 I2C 주소

// ── 밝기 ────────────────────────────────────────────────────────────
// PWM 이 아니다. 핀을 짧게 흔든 횟수로 16단계 중 하나를 고른다(LILYGO 보드 방식).
// 이 값은 **서버에 못 붙었을 때 쓰는 기본값**이다 — 붙으면 관리자 화면(리더 설정 → v2.0)의
// 밝기가 이깁니다. 서버 스키마(yvServer/talent/talentConfigV2.js)의 기본값과 같게 둘 것.
#define BL_LEVEL 16              // ← 1(가장 어둡다) … 16(가장 밝다)

static void backlightOn(uint8_t level) {
  if (level < 1)  level = 1;
  if (level > 16) level = 16;
  digitalWrite(PIN_BL, HIGH);                      // 16단계에서 시작
  delayMicroseconds(30);
  for (uint8_t i = 0; i < (uint8_t)(16 - level); i++) {
    digitalWrite(PIN_BL, LOW);                     // 한 번 흔들 때마다 한 단계 어둡게
    digitalWrite(PIN_BL, HIGH);
  }
}

// ── 화면 객체 (v1.0 과 동일한 값) ───────────────────────────────────
Arduino_XL9535SWSPI *bus = new Arduino_XL9535SWSPI(
    PIN_I2C_SDA, PIN_I2C_SCL,
    2 /* 전원 인에이블 */, 3 /* CS */, 5 /* SCK */, 4 /* MOSI */);

// 픽셀 클럭 8MHz · 중계 버퍼 열 줄 — 화면이 자글거리는 것을 막는다(v1.0 PanelTFT 와 같다).
static const int32_t PCLK_HZ      = 8000000L;
static const int     BOUNCE_LINES = 20;   // 열 줄 → 스무 줄. 인터럽트가 늦어도 버틸 여유를 늘린다
                                          // (내장 RAM 을 20KB 쯤 더 쓴다 — 지금 270KB 가 논다)

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    45 /* DE */, 41 /* VSYNC */, 47 /* HSYNC */, 42 /* PCLK */,
    21 /* R0 */, 18 /* R1 */, 17 /* R2 */, 16 /* R3 */, 15 /* R4 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    7  /* B0 */, 6  /* B1 */, 5  /* B2 */, 3  /* B3 */, 2  /* B4 */,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 1 /* hsync_pulse_width */, 30 /* hsync_back_porch */,
    1 /* vsync_polarity */, 20 /* vsync_front_porch */, 1 /* vsync_pulse_width */, 30 /* vsync_back_porch */,
    1 /* pclk_active_neg */,
    PCLK_HZ /* 픽셀 클럭 */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    480 * BOUNCE_LINES /* 중계 버퍼(픽셀 수) */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0 /* 회전 */, true /* 그릴 때마다 바로 반영 */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_type4_init_operations, sizeof(st7701_type4_init_operations));

// ── WebSocket 서버 ──────────────────────────────────────────────────
WebSocketsServer webSocket = WebSocketsServer(WS_PORT);

// ── 서버에 붙어 변화 받기 (2026-10-12) ──────────────────────────────
// 이 보드는 WebSocket **서버**이기도 하지만(위), 펀펀포인트 서버에는 **client 로 붙는다.**
// 관리 화면에서 포인트를 조정하거나 앱·다른 리더에서 변화가 생기면 서버가 그 아이를
// 이 화면에 띄우라고 밀어 준다 — `{"uid","name","points","note","status"}` 한 덩이로,
// 리더가 보내는 것과 **같은 말**이라 받는 곳도 `handleCommand` 하나다.
//
// 왜 이쪽이 붙나: 서버는 교회 와이파이 밖(데이터센터)에 있어 이 보드를 먼저 부를 수
// 없다(사설 주소다). 관리 화면의 브라우저가 대신 붙던 때도 있었는데, 운영 화면이
// https 라 `ws://` 가 막히고 화면을 닫으면 아무것도 오지 않았다. 지금은 이 보드가
// 붙어 있으므로 **아무도 화면을 보고 있지 않아도** 변화가 온다.
//
// 주소는 TALENT_SERVER 에서 떼어 쓴다(아래 hubTarget) — 서버 주소를 두 곳에 적어 두면
// 한쪽만 고쳐져, 조회는 새 서버로 가는데 화면은 옛 서버를 기다리는 꼴이 된다.
WebSocketsClient hub;
static bool hubBegun = false;    // 한 번 begin 했나(라이브러리가 스스로 다시 붙는다)
static bool hubUp = false;       // 지금 붙어 있나 — 시리얼 'i' 와 상태 보내기가 본다

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

// ── 대기 화면 사진 목록 (터치·시리얼로 순차 전환) ────────────────────
// 0번은 언제나 활성 그림(내장 USEENABLE), 1번부터는 microSD /talent/<UID>.565 사진이다.
// 목록은 부팅 때와 동기화(길게 누르기) 뒤에 SD 를 훑어 다시 만든다(scanPhotos). 이름 순 정렬.
#define MAX_PHOTOS 200
#define UID_MAX    24                    // UID 문자열 한 개의 최대 길이(널 포함)
static char photoUids[MAX_PHOTOS][UID_MAX]; // SD 사진의 UID(파일 이름에서 .565 를 뗀 것)
static int  sdPhotoCount = 0;
static int  bufIdx = -1;                 // photoBuf 에 지금 담긴 대기 사진 번호(-1 = 모름) — 같은 사진을 또 읽지 않게

// ── UID 별 사진 (NFC 태그 시) ───────────────────────────────────────
// 지금은 기기에 내장한 사진을 UID 에 연결한다. microSD 를 넣으면 loadPhotoForUid() 안에서
// 카드 파일(/sd/<uid>.raw · 480*480*2 바이트 RGB565)을 읽어 오도록 바꾸면 된다.
struct Member { const char *uid; const uint16_t *photo; };
static const Member MEMBERS[] = {
  { "04A1B2C3", USEENABLE },     // ← 예시. 실제 카드 UID·사진으로 교체하세요
};
static const int MEMBER_COUNT = sizeof(MEMBERS) / sizeof(MEMBERS[0]);

// ── microSD ─────────────────────────────────────────────────────────
static bool      sdReady  = false;
static uint16_t *photoBuf = nullptr;   // SD 에서 읽은 사진 한 장(480×480 RGB565) — loadPhotoForUid 가 채운다

// UID 로 사진 한 장을 고른다.
//   1) microSD 의 /talent/<UID>.565 (길게 눌러 서버에서 받아 둔 것) — 있으면 이걸 쓴다
//   2) 내장 MEMBERS[] 표
//   3) 그래도 없으면 기본 사진(useDisable)
// SD 파일은 서버가 리틀엔디안 RGB565 로 저장했고, ESP32 도 리틀엔디안이라 바이트를 그대로 읽으면
// 네이티브 RGB565 값이 된다(내장 헤더의 값과 같은 방식). draw16bitRGBBitmap 이 그대로 그린다.
static const uint16_t *loadPhotoForUid(const char *uid) {
  if (sdReady && photoBuf) {
    char path[48];
    snprintf(path, sizeof(path), "/talent/%s.565", uid);
    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
      const size_t need = 480 * 480 * sizeof(uint16_t);
      bufIdx = -1;                             // 버퍼를 NFC 사진으로 덮는다 — 대기 사진은 다시 읽어야 한다
      if ((size_t)f.size() == need && f.read((uint8_t *)photoBuf, need) == need) {
        f.close();
        return photoBuf;
      }
      f.close();
    }
  }
  for (int i = 0; i < MEMBER_COUNT; i++)
    if (!strcasecmp(uid, MEMBERS[i].uid)) return MEMBERS[i].photo;
  return USEENABLE;
}

// ── 화면 색·글자 (NFC 오버레이) ─────────────────────────────────────
#define COL_WHITE 0xFFFF
#define COL_GOLD  0xFEA0         // 잔여 포인트 숫자 색
#define COL_EDGE  0x0000         // 글자 뒤에 두르는 테두리색(검정)
#define BAND_TOP_H 108           // 상단 이름 띠 높이
#define BAND_MOOD_H 46           // '지금 기분' 한 줄이 있을 때 상단 띠에 더하는 높이
#define BAND_BOT_Y 356           // 하단 포인트 띠 시작 y
#define BAND_KEEP  90            // 띠에 남길 사진 비중(작을수록 어둡다 — 글자가 더 도드라진다)

// 글자 크기는 '높이 몇 픽셀' 로 적는다 — 폰트 원래 크기의 정수배로만 늘려 획이 흐트러지지 않는다.
#define PX_NAME   48             // NFC 상단 이름
#define PX_LABEL  32             // NFC 하단 라벨(또는 '출석 완료' 안내)
// NFC 상단 '지금 기분' 한 줄 — 이름(48=배수 3)보다 한 단계 작게, 하단 라벨과 같은 32(배수 2).
// ★ 글자 크기는 폰트 원래 높이(16)의 **정수배로만** 커진다(drawCenteredUTF8 의 px/unit).
//   그래서 30 으로 적으면 배수 1 = 16px 이 되어 띠(46)에 비해 유난히 작아진다 —
//   16 의 배수로 적을 것. 긴 글은 여기서 시작해 저절로 한 단계 줄고, 그래도 넘치면 fitEllipsis 다.
#define PX_MOOD   32
// '지금 기분' 줄 왼쪽에 놓는 캐릭터 그림(토끼·곰·고양이·햄스터 × 열 가지 표정).
// 서버가 정사각 RGB565 로 만들어 준다 — **funfunServer 의 CHAR_PX 와 같아야 한다**.
// 어긋나면 받은 파일 크기가 안 맞아 그림을 버리고 한국어 이름으로 되돌아간다(그래도 화면은 뜬다).
// 띠(BAND_MOOD_H=46)보다 작게 잡아 위아래에 숨 쉴 자리를 둔다.
#define MOOD_ICON_PX   40
#define MOOD_ICON_GAP  10        // 그림과 글자 사이
// 그림 바깥을 채운 색 — 이 색인 점은 그리지 않아 띠의 사진이 비친다.
// 서버 talentArt.js 의 TRANSPARENT_KEY 와 같은 값이다(연결 아이콘도 같은 자홍을 쓴다).
#define MOOD_ICON_TRANSPARENT 0xF81F
#define PX_POINTS 48             // NFC 하단 잔여 포인트 숫자
#define PX_STATUS 32             // 동기화 등 안내 문구
#define PX_BULK_TITLE 32         // 일괄 화면 제목(하단 라벨과 같은 크기)
#define PX_BULK_NAME  24         // 일괄 화면 이름 한 줄 — 배수 1(16px)로 고정, 여럿이 다 보이게 작게
#define TEXT_EDGE   3            // 글자 뒤 테두리 두께(픽셀) — 큰 글자일수록 이만큼까지 두껍게
#define TEXT_MARGIN 14           // 원형 화면 테두리에서 띄울 여백
static const uint32_t NFC_SHOW_MS = 6000;   // NFC 화면을 몇 ms 보여 준 뒤 대기 화면으로 돌아갈지

// ── 상태 ────────────────────────────────────────────────────────────
enum Mode { MODE_IDLE, MODE_NFC, MODE_BULK };
static Mode    mode      = MODE_IDLE;
static int     photoIdx  = 0;    // 대기 화면에서 지금 보이는 사진 번호(0 = 활성 그림, 1.. = SD 사진)
static uint32_t nfcUntil = 0;    // 이 시각(millis)이 지나면 NFC·일괄 화면을 접는다(두 모드가 함께 쓴다)
static char    nfcUid[32] = "";

static char    nfcName[40] = "";  // 지금 NFC 화면에 띄운 이름·잔액·한 줄 안내 — 붙은 리더에게 그대로 알려 준다
static long    nfcPoints   = 0;
static bool    nfcKnown    = false;
static char    nfcNote[64] = "";   // 서버가 보내는 거절 안내가 들어온다(한글 스무 자쯤)
// 아이가 펀펀포인트 앱이나 조회 화면에 적어 둔 '지금 기분' 한 줄. 서버가 이모티콘을 한국어
// 이름으로 바꿔 보내 주므로(who.status.label) 여기 담기는 것은 **그릴 수 있는 글자뿐**이다 —
// 이 화면의 폰트에는 이모티콘 글리프가 없어서, 😀 가 그대로 오면 네모(두부)가 뜬다.
// '기쁨 오늘 소풍 가요' 처럼 label 과 text 를 이어 둔 꼴이다. 비어 있으면 그 줄을 그리지 않는다.
// 크기는 서버가 보낼 수 있는 최대로 잡는다 — 라벨('고양이 포인트' 19바이트) + 공백 +
// 본문 서른 자(STATUS_LEN=30, 이모티콘이 섞이면 한 자가 4바이트라 120바이트)에 넉넉히.
static char    nfcMood[160] = "";
// 지금 화면의 캐릭터 그림 파일 이름(ch-<동물>-<표정>-<크기>.565). 비어 있으면 그림 없이 글자만 그린다.
// 이 칸이 차 있으면 nfcMood 에는 **아이가 적은 글만** 담긴다 — 그림이 이름('토끼 인사')을 대신하기 때문이다
// (앱의 StatusLine 과 같은 모양: 그림 + 아이가 적은 글). 그림을 못 받으면 이름을 앞에 붙인 한 줄로 되돌아간다.
static char    nfcMoodIcon[40] = "";
static uint16_t moodIcon[MOOD_ICON_PX * MOOD_ICON_PX];   // 그 그림의 픽셀(3.2KB — 한 장만 들고 있는다)
static char    moodIconHave[40] = "";                    // moodIcon 에 지금 담긴 그림(같은 캐릭터면 다시 읽지 않는다)

// ── 일괄 지급·사용 (2026-10-13) ───────────────────────────────────────
// 관리 화면에서 여러 아이에게 한 번에 포인트를 줄 때(출석 등), 한 아이씩 NFC 화면으로
// 올리면 화면이 아이 수만큼 빠르게 바뀌어 누가 받았는지 놓친다. 그래서 일괄은 **한
// 화면에 제목 + 받은 아이 명단을 함께** 띄운다 — `{"bulk":{"headline":"…","names":[…]}}`
// (`funfunServer` 의 `talent/talentFeed.js` 의 `showBulk` 가 보낸다).
#define BULK_NAMES_MAX 40          // 한 번에 받을 이름 수 상한(서버도 같은 수로 자른다)
#define BULK_NAME_LEN  20          // 이름 한 칸의 최대 길이(널 포함) — UTF-8 넉넉히
static char    bulkHeadline[48] = "";                     // "일괄지급 출석 +5P" 처럼 한 줄
static char    bulkNames[BULK_NAMES_MAX][BULK_NAME_LEN];
static int     bulkCount = 0;
static const uint32_t BULK_SHOW_BASE_MS     = 4000;        // 기본으로 보여 줄 시간
static const uint32_t BULK_SHOW_PER_NAME_MS = 400;         // 이름 하나마다 더 주는 시간(읽을 틈)
static const uint32_t BULK_SHOW_MAX_MS      = 15000;        // 아무리 많아도 이 안에서 끝낸다

// 리더가 보낸 UID 를 여기 담아 두고 loop 에서 처리한다.
// WebSocket 콜백 안에서 서버를 부르지 않는 이유: 조회·지급은 수백 ms 가 걸리는 일인데,
// 그동안 콜백이 돌아오지 않으면 같은 소켓으로 들어온 다음 것이 밀려 라이브러리가 엉킨다.
static char    pendingUid[32] = "";

// ── 서버 설정 (리더 설정 → v2.0) ────────────────────────────────────
// 부팅해서 와이파이에 붙으면 받고, 그 뒤 CONFIG_TTL_MS 마다 다시 받는다.
// 서버에 못 붙으면 아래 기본값으로 돈다 — 값은 서버 스키마(talentConfigV2.js)와 같게 둘 것.
static uint8_t  cfgBacklight   = BL_LEVEL;
static bool     cfgAttendMode  = false;
static char     cfgAttendCard[24] = "";
static uint32_t cfgNextAt      = 0;      // 다음 설정 조회 시각(millis)
static bool     cfgGotOnce     = false;  // 한 번이라도 받아 봤나(시리얼 표시용)
// 관리 화면이 "지금 다시 받아라" 를 밀면 서는 깃발({"state":"config"} — 서버의 deviceHub
// 가 보내는 'config'). 주기(CONFIG_TTL_MS · 5분)를 기다리지 않고 그 자리에서 받아 오려는
// 것이다 — 출석모드를 켜는 자리는 주일 아침 줄이 선 현장이라 5분이 "켰는데 안 켜진다" 다.
// ★ 받는 자리에서 바로 fetchConfig 를 부르지 않는다. 수백 ms 짜리 https 왕복이라 받는
//   콜백(와이파이 일꾼) 위에서 돌면 그동안 라디오가 멎는다 — 태그를 pendingUid 로 미뤄
//   두는 것과 같은 규율이다. 깃발만 세우고 푸는 것은 loop 가 한다.
static volatile bool cfgWanted = false;

// 최근 출석한 키링 — 같은 아이가 잇달아 대도 한 번만 센다(ATTEND_COOLDOWN_MS).
struct AttendSeen { char uid[24]; uint32_t at; };
static AttendSeen attendSeen[ATTEND_RECENT_MAX];
static int        attendSeenCount = 0;

// ── 리더와 잇는 선 (I2C 마스터) ─────────────────────────────────────
// WebSocket 서버는 그대로 열어 둔다 — 브라우저·폰은 계속 ws 로 붙는다. 리더만 선으로 온다.
// 선과 ws 로 오는 글자는 **똑같다**(JSON 한 덩이). 그래서 푸는 곳도 handleCommand 하나다.
// 리더는 슬레이브라 먼저 말을 못 건다 — 이쪽이 LINK_POLL_MS 마다 읽어 가고(linkService),
// 결과는 이쪽이 써 넣는다(linkSend). 이 버스는 터치·확장칩과 함께 쓴다(Wire 하나).
#if LINK_I2C_ENABLE
static uint32_t linkAliveUntil = 0;      // 이 시각까지는 리더가 선 너머에 살아 있다고 본다
static bool linkAlive() { return (int32_t)(millis() - linkAliveUntil) < 0; }

static void linkSend(const char *msg) {
  if (!linkAlive()) return;                // 선에 리더가 없다 — 보내 봐야 실패하고, 코어가 에러 줄만 쏟는다
  Wire.beginTransmission((uint8_t)LINK_I2C_ADDR);
  Wire.write((const uint8_t *)msg, strlen(msg));
  if (Wire.endTransmission() == 0) linkAliveUntil = millis() + LINK_ALIVE_MS;
}
#else
static bool linkAlive()            { return false; }
static void linkSend(const char *) {}
#endif

// ── 리더와 선 없이 잇기 (ESP-NOW) — 2순위 길 ───────────────────────
// 선이 빠졌을 때 넘어가는 길이다. 오가는 글자는 선과 **똑같은 JSON** 이라 푸는 곳도
// handleCommand 하나다 — 규약을 둘로 두면 한쪽만 고치는 일이 생긴다.
//
// ★ 받는 콜백은 **와이파이 일꾼(task)에서** 불린다. 거기서 handleCommand 를 부르면
//   화면을 그리고 서버에 HTTP 를 거는 수백 ms 짜리 일이 그 일꾼 위에서 돌아 라디오가
//   멎는다(선 쪽 콜백 규율과 같은 까닭). 그래서 **베껴 두기만 하고** loop 에서 푼다.
//
// ★ 짝은 저절로 맺힌다 — 리더가 브로드캐스트로 먼저 부르면 그 MAC 을 배워 둔다.
//   채널을 맞추는 일은 리더가 한다(이쪽 채널은 공유기가 정하므로 고를 수 없다).
#if ESPNOW_ENABLE
static const uint8_t ESPNOW_BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const size_t  ESPNOW_MAGIC_LEN = sizeof(ESPNOW_MAGIC) - 1;

static uint8_t  readerMac[6]   = { 0 };     // 배워 둔 리더의 MAC (0 = 아직 모른다)
static bool     readerKnown    = false;
static volatile bool readerPeerPending = false;   // 콜백이 배운 MAC — loop 가 짝으로 등록한다
static uint32_t espnowAliveUntil = 0;       // 이때까지는 리더가 라디오 너머에 살아 있다고 본다
static char     espnowIn[224];              // 콜백이 베껴 두는 곳 — loop 가 푼다
static volatile bool espnowInReady = false;
static bool     espnowUp       = false;     // esp_now_init 을 했나

static bool espnowAlive() { return espnowUp && (int32_t)(millis() - espnowAliveUntil) < 0; }

// 리더를 짝으로 등록한다(이미 있으면 그대로 둔다).
// 채널을 0 으로 두는 것이 핵심이다 — "지금 쓰는 채널" 이라는 뜻이라, 공유기가 바뀌어
// 이쪽 채널이 옮겨 가도 짝을 다시 맺지 않아도 된다.
static bool espnowPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// 한 덩이 보낸다 — 앞에 표식을 붙이고 그 뒤에 JSON 을 그대로 싣는다.
// ESP-NOW 한 덩이는 250바이트까지다. 넘치면 보내지 않는다(잘라 보내면 받는 쪽이
// JSON 을 못 읽어 통째로 버린다 — 조용히 반쪽을 보내는 것보다 알리는 편이 낫다).
static bool espnowSend(const uint8_t *mac, const char *msg) {
  if (!espnowUp) return false;
  uint8_t buf[250];
  const size_t n = strlen(msg);
  if (ESPNOW_MAGIC_LEN + n > sizeof(buf)) {
    Serial.printf("[라디오] 덩이가 너무 큽니다(%u바이트) — 못 보냄\n", (unsigned)(ESPNOW_MAGIC_LEN + n));
    return false;
  }
  memcpy(buf, ESPNOW_MAGIC, ESPNOW_MAGIC_LEN);
  memcpy(buf + ESPNOW_MAGIC_LEN, msg, n);
  return esp_now_send(mac, buf, ESPNOW_MAGIC_LEN + n) == ESP_OK;
}

// 받았다 — **와이파이 일꾼 위에서 불린다.** 베끼기만 하고 곧바로 돌아간다.
static void espnowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len <= (int)ESPNOW_MAGIC_LEN) return;
  if (memcmp(data, ESPNOW_MAGIC, ESPNOW_MAGIC_LEN)) return;     // 우리 것이 아니다
  const uint8_t *body = data + ESPNOW_MAGIC_LEN;
  size_t n = (size_t)len - ESPNOW_MAGIC_LEN;
  if (n >= sizeof(espnowIn)) n = sizeof(espnowIn) - 1;

  // 보낸 쪽을 리더로 기억해 둔다(브로드캐스트로 와도 src_addr 은 그 보드의 MAC 이다).
  // 짝 등록(esp_now_add_peer)은 여기서 하지 않는다 — 이 콜백은 **와이파이 일꾼 위**라
  // ESP-NOW API 를 되부르면 그 일꾼이 제 자물쇠를 기다리게 될 수 있다. MAC 만 적어 두고
  // 등록은 loop(espnowService)에서 한다.
  if (!readerKnown || memcmp(readerMac, info->src_addr, 6)) {
    memcpy(readerMac, info->src_addr, 6);
    readerKnown = true;
    readerPeerPending = true;
  }
  espnowAliveUntil = millis() + ESPNOW_ALIVE_MS;

  if (espnowInReady) return;                 // 앞의 것을 아직 안 풀었다 — 덮지 않는다
  memcpy(espnowIn, body, n);
  espnowIn[n] = '\0';
  espnowInReady = true;
}

// 와이파이를 켠 뒤에 한 번 부른다(esp_now_init 은 라디오가 올라와 있어야 한다).
static void espnowBegin() {
  if (espnowUp) return;
  if (esp_now_init() != ESP_OK) { Serial.println("[라디오] ESP-NOW 를 켜지 못했습니다"); return; }
  esp_now_register_recv_cb(espnowRecv);
  espnowPeer(ESPNOW_BCAST);                  // 리더에게 답할 때 쓰는 길(짝을 맺기 전)
  espnowUp = true;
  uint8_t ch = 0; wifi_second_chan_t s2;
  esp_wifi_get_channel(&ch, &s2);
  Serial.printf("[라디오] ESP-NOW 켜짐 — 내 MAC %s · 채널 %u (리더가 이 채널을 찾아온다)\n",
                WiFi.macAddress().c_str(), ch);
}
// 라디오를 내린다 — 와이파이 드라이버를 껐다 켜면 ESP-NOW 도 함께 내려가므로,
// 껐다 켜기 전에 이쪽도 가지런히 내려 둔다. 짝(리더 MAC)도 함께 잊는다:
// 드라이버가 새로 올라오면 짝 목록이 비어 있어, 들고 있던 MAC 으로 보내도 가지 않는다.
// 리더가 다시 부르면 그때 다시 배운다(espnowRecv).
static void espnowEnd() {
  if (!espnowUp) return;
  esp_now_deinit();
  espnowUp          = false;
  readerKnown       = false;
  readerPeerPending = false;
  espnowAliveUntil  = 0;
}
#else
static bool espnowAlive() { return false; }
static void espnowBegin() {}
static void espnowEnd() {}
#endif

static bool online    = false;   // 화면에 그려 둔 연결 상태(아이콘이 떠 있나)
static bool wasDown   = false;   // 직전에 손가락이 닿아 있었나(누르는 순간을 가려내려고)
static bool serversUp = false;   // mDNS·WebSocket 을 켰나(와이파이가 붙은 뒤 한 번만)
static uint32_t downAt   = 0;    // 손가락이 닿기 시작한 시각(길게/짧게 가려내려고)
static bool     longFired = false; // 이번 누름에서 '길게' 동작을 이미 한 번 했나
static bool     bootSyncDone = false; // 부팅 후 와이파이가 처음 붙었을 때 사진을 한 번 받아 왔나

// ── 합쳐 그리기용 버퍼 ──────────────────────────────────────────────
// 배경을 먼저 그리고 아이콘/띠를 나중에 얹으면 그 틈에 깜빡인다(RGB 화면은 프레임버퍼를
// 초당 수십 번 계속 읽어 간다). 그래서 PSRAM 에 한 장을 두고 배경+아이콘+띠를 먼저 합친 뒤
// 한 번에 옮긴다. 그 위에 글자만 gfx 로 직접 얹는다(글자 영역은 작아 깜빡임이 거의 없다).
static uint16_t *frame = nullptr;
static void blitFrame();
// 캐릭터 그림을 챙기는 일은 SD·서버를 쓴다(저 아래 httpSend 뒤에 있다) — 화면 쪽에서 먼저 부른다.
static bool loadMoodIcon(const char *name);

// RGB565 두 색을 섞는다. a=0..255 는 앞 색(fg)의 비중.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
  uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
  uint16_t r = (r1 * a + r2 * (255 - a)) / 255;
  uint16_t g = (g1 * a + g2 * (255 - a)) / 255;
  uint16_t b = (b1 * a + b2 * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}

// 정수를 천 단위 콤마로 (예: 1200 → "1,200"). NFC 하단 포인트 표기용.
static void commafy(long v, char *out, size_t n) {
  char tmp[16];
  bool neg = v < 0; if (neg) v = -v;
  int len = snprintf(tmp, sizeof(tmp), "%ld", v);
  size_t o = 0;
  if (neg && o < n - 1) out[o++] = '-';
  for (int i = 0; i < len && o < n - 1; i++) {
    if (i > 0 && (len - i) % 3 == 0 && o < n - 1) out[o++] = ',';
    out[o++] = tmp[i];
  }
  out[o] = '\0';
}

// 서버에서 온 글자를 **JSON 에 그대로 끼워 넣어도 안전하게** 베낀다.
// 이 파일이 내보내는 상태 알림은 snprintf 로 조립한 JSON 이라(buildState) 값 안에 `"` 나 `\`
// 가 섞이면 그 자리에서 깨지고, 받는 쪽(리더·브라우저)이 답을 통째로 못 알아본다.
// '지금 기분' 은 아이가 **제 손으로 적은 글**이라 그런 글자가 실제로 들어올 수 있다 —
// 이름·안내문과 달리 서버가 고른 말이 아니다. 따옴표·역슬래시·제어문자는 떨어뜨린다
// (바꿔 끼우지 않고 버리는 쪽: 글자 수가 늘지 않아 버퍼 계산이 그대로 맞는다).
// UTF-8 뒷바이트는 전부 0x80 이상이라 한글은 손대지 않는다.
// 잘라야 할 때는 **글자 경계에서** 끊는다 — UTF-8 한 글자를 반만 베껴 두면 그 자리에
// 없는 글자가 생겨 화면에 깨진 네모가 뜨고, 받는 쪽의 JSON 도 유효하지 않은 문자열이 된다.
static void copyJsonSafe(char *dst, size_t cap, const char *src) {
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)(src ? src : "");
  while (*p) {
    if (*p == '"' || *p == '\\' || *p < 0x20) { p++; continue; }
    size_t n = 1;                                  // 이 글자가 몇 바이트인가(UTF-8 선두 바이트가 말해 준다)
    if      ((*p & 0xE0) == 0xC0) n = 2;
    else if ((*p & 0xF0) == 0xE0) n = 3;
    else if ((*p & 0xF8) == 0xF0) n = 4;
    for (size_t i = 1; i < n; i++) if (!p[i]) { n = i; break; }   // 중간에 끊긴 입력은 있는 만큼만
    if (o + n > cap - 1) break;                    // 남은 자리에 통째로 안 들어가면 여기서 그만
    for (size_t i = 0; i < n; i++) dst[o++] = (char)p[i];
    p += n;
  }
  dst[o] = '\0';
}

// ── '지금 기분' 한 벌 ───────────────────────────────────────────────
// 서버가 준 것(label·text·icon)을 화면이 쓸 모양으로 풀어 둔 것이다.
// 그림을 쓸 수 있으면 `icon` + `text` 로, 못 쓰면 `line` 한 줄로 그린다 —
// 둘을 미리 다 만들어 두는 까닭은, 그림을 받아 보기 전에는 어느 쪽이 될지 모르기 때문이다.
struct Mood {
  char icon[40];    // 캐릭터 그림 파일 이름. 이모티콘이거나 모양이 수상하면 빈 값
  char text[136];   // 아이가 적은 글만 — 그림 옆에 놓는다(서른 자 × 4바이트 + 여유)
  char line[160];   // 그림 없이 그릴 한 줄 — '토끼 인사 오늘도 화이팅!'
};

// 서버가 준 그림 이름이 우리가 쓰는 꼴인가. 이 글자를 그대로 주소와 SD 경로에 끼우므로,
// 점·빗금이 섞여 엉뚱한 파일을 열거나 지우는 일이 없게 생김새를 먼저 본다.
//   ch-<소문자>-<소문자>-<숫자>.565
static bool moodIconNameOk(const char *s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n < 12 || n >= sizeof(((Mood *)nullptr)->icon)) return false;
  if (strncmp(s, "ch-", 3) || strcmp(s + n - 4, ".565")) return false;
  int dash = 0;
  for (size_t i = 3; i < n - 4; i++) {
    const char c = s[i];
    if (c == '-') { dash++; continue; }
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  return dash == 2;
}

// label·text·icon 한 벌을 Mood 에 담는다. 서버 응답(moodFromJson)과 시험 화면이 함께 쓴다 —
// 두 벌로 두면 시험 화면에서 본 모양과 현장에서 뜨는 모양이 어긋난다.
// text 만 copyJsonSafe 로 받는다: 아이가 제 손으로 적은 글이라 따옴표가 섞일 수 있다
// (label·icon 은 서버가 제 표에서 고른 말이다).
static void moodSet(Mood *m, const char *icon, const char *label, const char *text) {
  if (!icon) icon = ""; if (!label) label = ""; if (!text) text = "";
  m->icon[0] = '\0';
  copyJsonSafe(m->text, sizeof(m->text), text);
  if (moodIconNameOk(icon)) { strncpy(m->icon, icon, sizeof(m->icon) - 1); m->icon[sizeof(m->icon) - 1] = '\0'; }
  if (label[0] && m->text[0]) snprintf(m->line, sizeof(m->line), "%s %s", label, m->text);
  else                        snprintf(m->line, sizeof(m->line), "%s", label[0] ? label : m->text);
}

// frame 버퍼에 배경 사진 한 장을 통째로 채운다(항상 전체화면 480×480).
static void fillBackground(const uint16_t *bg) {
  memcpy(frame, bg, 480 * 480 * sizeof(uint16_t));
}

// frame 버퍼에 연결 아이콘을 얹는다(투명색은 건너뛴다).
static void overlayConnIcon() {
  for (int y = 0; y < CONNECTED_H; y++) {
    const int fy = CONN_Y + y;
    if (fy < 0 || fy >= 480) continue;
    for (int x = 0; x < CONNECTED_W; x++) {
      const int fx = CONN_X + x;
      if (fx < 0 || fx >= 480) continue;
      const uint16_t c = CONNECTED[y * CONNECTED_W + x];
      if (c != CONNECTED_TRANSPARENT) frame[fy * 480 + fx] = c;
    }
  }
}

// frame 버퍼의 한 가로 띠를 어둡게(반투명 검정처럼) 만든다 — 글자가 사진 위에서 잘 보이게.
static void darkenBand(int y0, int y1, uint8_t keep /*앞(사진)색 비중*/) {
  if (y0 < 0) y0 = 0; if (y1 > 480) y1 = 480;
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < 480; x++)
      frame[y * 480 + x] = blend565(frame[y * 480 + x], 0x0000, keep);
}

// frame 버퍼에 캐릭터 그림을 얹는다(비침색인 점은 건너뛴다 — 띠의 사진이 그대로 비친다).
// 연결 아이콘(overlayConnIcon)과 같은 방식이다. 띠를 어둡게 한 **뒤에** 불러야 그림이
// 같이 어두워지지 않는다.
static void overlayMoodIcon(int x0, int y0) {
  for (int y = 0; y < MOOD_ICON_PX; y++) {
    const int fy = y0 + y;
    if (fy < 0 || fy >= 480) continue;
    for (int x = 0; x < MOOD_ICON_PX; x++) {
      const int fx = x0 + x;
      if (fx < 0 || fx >= 480) continue;
      const uint16_t c = moodIcon[y * MOOD_ICON_PX + x];
      if (c != MOOD_ICON_TRANSPARENT) frame[fy * 480 + fx] = c;
    }
  }
}

// ── 글자체 ──────────────────────────────────────────────────────────
// 글자는 굴림 16px(u8g2_font_gulim16_t_korean2)로 그린다. 한 글자가 16픽셀 칸이라
// 2배·3배로 늘려도 자모가 또박또박 남는다 — 8픽셀 폰트(quan7)를 4배로 늘리면
// 획이 뭉개져 두 발짝 떨어지면 이름을 못 읽는다.
// 다만 굴림 쪽은 KS X 1001 의 2350 자만 들어 있다(흔한 이름·문구는 다 되지만 '똠·뷁' 류가 없다).
// 그런 글자가 한 자라도 섞이면 그 줄만 한글이 전부 든 quan7 로 되돌려 그린다 —
// 글자가 없으면 u8g2 는 그 자리를 그냥 비우고 지나가, 아이 이름이 통째로 사라진다.
#define FONT_MAIN    u8g2_font_gulim16_t_korean2
#define FONT_MAIN_PX 16          // 굴림 한 글자의 원래 높이
#define FONT_ALL     u8g2_font_quan7_h_cjk
#define FONT_ALL_PX  8           // quan7 한 글자의 원래 높이

// UTF-8 한 글자를 유니코드 번호로 꺼내고, 다음 글자 자리를 돌려준다.
static const char *utf8Next(const char *s, uint16_t *cp) {
  const uint8_t c = (uint8_t)*s++;
  uint16_t v; int more;
  if      (c < 0x80) { *cp = c; return s; }
  else if (c < 0xE0) { v = c & 0x1F; more = 1; }
  else if (c < 0xF0) { v = c & 0x0F; more = 2; }
  else               { v = c & 0x07; more = 3; }
  while (more-- && ((uint8_t)*s & 0xC0) == 0x80) v = (v << 6) | ((uint8_t)*s++ & 0x3F);
  *cp = v;
  return s;
}

// u8g2 폰트 표에 이 글자가 있나 — Arduino_GFX 가 글자를 찾는 길을 그대로 따라간다
// (머리 23바이트 뒤로 1바이트짜리 글자들이 늘어서고, 그 뒤에 유니코드 묶음별 찾기표가 있다).
static uint16_t fontWord(const uint8_t *f, uint8_t off) { return ((uint16_t)f[off] << 8) | f[off + 1]; }

static bool fontHasGlyph(const uint8_t *font, uint16_t cp) {
  const uint8_t *p = font + 23;
  if (cp <= 255) {
    for (;;) {
      if (p[1] == 0)  return false;
      if (p[0] == cp) return true;
      p += p[1];
    }
  }
  const uint8_t *table = font + 23 + fontWord(font, 21);   // 유니코드 찾기표(4바이트짜리 칸)
  p = table;
  uint16_t last;
  do {
    p += fontWord(table, 0);                               // 이 묶음 글자들이 시작하는 자리
    last = fontWord(table, 2);                             // 이 묶음의 마지막 글자
    table += 4;
  } while (last < cp);
  for (;;) {
    const uint16_t e = fontWord(p, 0);
    if (e == 0 || e > cp) return false;   // 글자는 번호순으로 늘어서 있다 — 지나쳤으면 없는 글자다
    if (e == cp)          return true;
    p += p[2];
  }
}

// 이 줄을 굴림으로 다 그릴 수 있나(한 자라도 없으면 quan7 로 간다).
static bool mainFontCovers(const char *s) {
  uint16_t cp;
  while (*s) {
    s = utf8Next(s, &cp);
    if (!fontHasGlyph((const uint8_t *)FONT_MAIN, cp)) return false;
  }
  return true;
}

// 원형 화면에서 y0..y1 줄에 글자를 놓을 수 있는 가로폭(가장 좁은 줄 기준, 양옆 여백 뺀 값).
// 네모난 화면이면 480 을 그대로 쓰면 되지만, 이 화면은 지름 480 원이라 위아래로 갈수록 좁다.
static int roundWidthAt(int y0, int y1) {
  int dy = abs(y0 - 240); const int dy1 = abs(y1 - 240);
  if (dy1 > dy) dy = dy1;
  if (dy >= 240) return 0;
  const int half = (int)sqrtf((float)(240 * 240 - dy * dy));
  const int w = 2 * half - 2 * TEXT_MARGIN;
  return w > 0 ? w : 0;
}

// 글자 한 줄을 지금 폰트·크기로 (x, y) 에 찍는다(테두리·굵게 칠을 여러 번 겹치려고 떼 놓았다).
static void printAt(const char *s, int x, int y, uint16_t color) {
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// 글자 한 줄을 이 자리에 놓을 폰트·배수를 고르고, 그때의 크기를 재 둔다.
//   reserve : 그 줄에서 글자 말고 다른 것(캐릭터 그림 + 사이 여백)이 이미 먹은 가로폭
// 그리는 쪽(drawCenteredUTF8)과 재는 쪽(textWidthAt)이 **같은 규칙**을 쓰도록 떼 놓았다 —
// 어긋나면 그림 옆에 둔 글자가 가운데에서 비껴 앉는다. 돌아올 때 폰트·배수는 맞춰져 있다.
static int fitTextScale(const char *s, int cy, int px, int reserve,
                        int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) {
  const bool useMain = mainFontCovers(s);
  const int unit = useMain ? FONT_MAIN_PX : FONT_ALL_PX;
  gfx->setFont(useMain ? (const uint8_t *)FONT_MAIN : (const uint8_t *)FONT_ALL);
  gfx->setUTF8Print(true);

  int scale = px / unit; if (scale < 1) scale = 1;
  for (;;) {
    gfx->setTextSize(scale);
    gfx->getTextBounds(s, 0, 0, x1, y1, w, h);
    if (scale <= 1) break;
    if ((int)*w + 2 * TEXT_EDGE + reserve <= roundWidthAt(cy - *h / 2, cy + *h / 2)) break;
    scale--;
  }
  return scale;
}

// 그 줄을 그렸을 때의 가로폭. 캐릭터 그림과 글자를 **한 덩이로** 가운데에 놓으려고 미리 잰다.
static int textWidthAt(const char *s, int cy, int px, int reserve = 0) {
  if (!s || !s[0]) return 0;
  int16_t x1, y1; uint16_t w, h;
  fitTextScale(s, cy, px, reserve, &x1, &y1, &w, &h);
  return (int)w;
}

// 글자 한 줄을 (가운데 x = cx) 에 세로 중앙(cy)으로 놓고 그린다.
//   px : 바라는 글자 높이. 원 안에 안 들어가면 폰트 배수를 한 단계씩 줄인다(긴 이름도 안 잘린다).
// 뒤에 검은 테두리를 두르고 획을 1픽셀 굵게 겹쳐 찍는다 — 사진 위에서도 글자만 떠 보인다.
static void drawCenteredUTF8(const char *s, int cx, int cy, int px, uint16_t color, int reserve = 0) {
  int16_t x1, y1; uint16_t w, h;
  const int scale = fitTextScale(s, cy, px, reserve, &x1, &y1, &w, &h);

  const int bx = cx - w / 2 - x1, by = cy - h / 2 - y1;
  int edge = scale < 2 ? 2 : scale;                                  // 작은 글자엔 얇게, 큰 글자엔 두껍게
  if (edge > TEXT_EDGE) edge = TEXT_EDGE;
  static const int8_t ox[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
  static const int8_t oy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  for (int i = 0; i < 8; i++)                                        // 여덟 방향 검은 테두리
    printAt(s, bx + ox[i] * edge, by + oy[i] * edge, COL_EDGE);
  printAt(s, bx,     by, color);                                     // 제 색으로 한 번
  printAt(s, bx + 1, by, color);                                     // 1픽셀 옆으로 한 번 더 — 굵게
}

// 한 줄이 **가장 작은 크기(배수 1)에서도** 원 안에 안 들어가면 뒤를 떼고 '...' 를 붙인다.
// drawCenteredUTF8 은 배수를 한 단계씩 줄여 맞추다 1 에서 멈추므로, 그보다 긴 줄은 양옆이
// 말없이 잘려 나간다. 이름은 그만큼 길 일이 없어 여태 드러나지 않았는데, '지금 기분' 은
// 아이가 서른 자까지 적을 수 있다 — 잘린 자리를 알려 주어야 "뒤에 더 있다" 가 읽힌다.
//
// ★ '…'(U+2026)도 '·'(U+00B7)도 굴림16(t_korean2)에 **없다**(폰트 표를 떠서 확인했다).
//   쓰면 그 한 자 때문에 mainFontCovers 가 false 가 되어 줄 전체가 quan7(8px)로 되돌려
//   그려지고 글자가 확 작아진다. 그래서 ASCII 마침표 셋만 쓴다.
//
// s 는 제자리에서 고친다(바꿀 수 있는 버퍼라야 한다). cap 은 그 버퍼의 크기다.
// reserve 는 그 줄에서 글자 말고 다른 것(캐릭터 그림)이 먹은 폭 — 그만큼 일찍 잘라야 한다.
static void fitEllipsis(char *s, size_t cap, int cy, int px, int reserve = 0) {
  if (!s || !s[0] || cap < 8) return;
  gfx->setFont(mainFontCovers(s) ? (const uint8_t *)FONT_MAIN : (const uint8_t *)FONT_ALL);
  gfx->setUTF8Print(true);
  gfx->setTextSize(1);                       // 배수 1 에서도 넘치는 것만 자른다
  int16_t x1, y1; uint16_t w, h;
  const auto fits = [&]() {
    gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    return (int)w + 2 * TEXT_EDGE + reserve <= roundWidthAt(cy - h / 2, cy + h / 2);
  };
  if (fits()) return;                        // 그대로 들어간다 — 배수는 drawCenteredUTF8 이 고른다

  size_t len = strlen(s);
  for (;;) {
    if (len == 0) { s[0] = '\0'; return; }
    size_t cut = len - 1;                    // 뒤에서 UTF-8 한 글자 떼기(뒷바이트를 거슬러 올라간다)
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    len = cut;
    if (len + 4 > cap) continue;             // '...' 를 붙일 자리가 없다 — 한 글자 더 뗀다
    char keep[4];
    memcpy(keep, s + len, 4);                // 재어 보고 아니면 되돌리려고 그 자리를 잠깐 치워 둔다
    memcpy(s + len, "...", 4);
    if (fits()) return;
    memcpy(s + len, keep, 4);
  }
}

// 안내 문구 한 줄을 검은 화면 가운데에 띄운다(사진 동기화 진행 상황 등).
static void drawStatus(const char *msg) {
  if (frame) {
    for (int i = 0; i < 480 * 480; i++) frame[i] = 0x0000;
    blitFrame();
  } else {
    gfx->fillScreen(0x0000);
  }
  drawCenteredUTF8(msg, 240, 240, PX_STATUS, COL_WHITE);
}

// ── 불러오는 중 표시 (가장자리를 도는 고리) ─────────────────────────
// 서버에 무엇을 물을 때(이름·잔액 조회, 출석 지급, 설정 받기, 사진 내려받기)
// 화면 테두리를 따라 짧은 금색 고리가 돈다 — 멈춰 있는 것과 기다리는 중인 것을 가른다.
// 서버 왕복은 수백 ms 동안 loop 를 붙잡고 있어서, 그리는 일은 따로 띄운 작은 일꾼이 맡는다.
// 그 사이 loop 는 소켓을 기다리며 쉬고 있으므로 이 일꾼이 돌 틈이 난다.
// 지울 때는 frame(지금 화면 그림)에서 그 자리 색을 되살린다 — 배경을 다시 그릴 필요가 없다.
// 그림은 gfx->writePixelPreclipped 로 화면 버퍼에 바로 쓴다(startWrite 묶음을 건드리지 않는다 —
// loop 가 한창 그리는 중일 수 있어, 둘이 같은 묶음 상태를 주무르면 화면이 얼룩진다).
#define SPIN_R_IN    226         // 고리 안쪽 반지름(화면 반지름은 240)
#define SPIN_R_OUT   236         // 고리 바깥쪽 반지름
#define SPIN_SPAN    50          // 고리 조각이 덮는 각도
#define SPIN_STEP    14          // 한 칸에 도는 각도
#define SPIN_MS      40          // 한 칸 도는 데 걸리는 시간
#define SPIN_COLOR   COL_GOLD

static volatile bool spinWant = false;   // 돌려 달라(서버와 이야기하는 동안)
static volatile bool spinBusy = false;   // 지금 화면에 조각이 올라가 있다
static volatile int  spinDepth = 0;      // 겹쳐 부른 횟수(사진 동기화 안에서 또 부른다)
static volatile bool spinPause = false;  // 화면을 통째로 옮기는 중 — 잠깐 비켜 있어라
static int spinAt = 0;                   // 지금 조각이 놓인 각도

// 고리 조각 하나를 그리거나(erase=false) 지운다(erase=true — frame 의 색으로 되돌린다).
static void spinArc(int deg, bool erase) {
  if (!frame) return;                    // 되돌릴 그림이 없으면 아예 그리지 않는다
  for (int i = 0; i <= SPIN_SPAN * 4; i++) {            // 0.25°씩 — 바깥 둘레에서도 점이 안 벌어진다
    const float a = (deg + i * 0.25f) * 0.01745329f;
    const float cs = cosf(a), sn = sinf(a);
    for (int r = SPIN_R_IN; r <= SPIN_R_OUT; r++) {
      const int x = 240 + (int)(r * cs), y = 240 + (int)(r * sn);
      if (x < 0 || x >= 480 || y < 0 || y >= 480) continue;
      gfx->writePixelPreclipped(x, y, erase ? frame[y * 480 + x] : SPIN_COLOR);
    }
  }
}

// 화면 한 장(460KB)을 통째로 패널에 옮기는 동안에는 고리를 쉬게 한다 —
// 같은 순간에 둘이 PSRAM 을 두드리면 패널이 그림을 제때 못 읽어 화면이 밀린다.
// 통째로 다시 깔렸으니 앞 조각은 저절로 지워진 셈이라 지울 필요도 없다.
static void blitFrame() {
  spinPause = true;
  gfx->draw16bitRGBBitmap(0, 0, frame, 480, 480);
  spinBusy  = false;
  spinPause = false;
}

static void spinnerTask(void *) {
  for (;;) {
    if (spinPause) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    if (!spinWant) {
      if (spinBusy) { spinArc(spinAt, true); spinBusy = false; }   // 마지막 조각을 치운다
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (spinBusy) spinArc(spinAt, true);                 // 앞 조각을 지우고
    spinAt = (spinAt + SPIN_STEP) % 360;
    spinArc(spinAt, false);                              // 한 칸 옮겨 그린다
    spinBusy = true;
    vTaskDelay(pdMS_TO_TICKS(SPIN_MS));
  }
}

static void spinStart() { spinDepth++; spinWant = true; }

static void spinStop() {
  if (--spinDepth > 0) return;                           // 겹쳐 부른 것이 남았다 — 계속 돈다
  spinDepth = 0;
  spinWant = false;
  for (int i = 0; i < 100 && spinBusy; i++) vTaskDelay(pdMS_TO_TICKS(5));   // 조각이 치워질 때까지
}

// 서버를 부르는 함수 맨 앞에 'Spinner spin;' 한 줄만 두면 된다 —
// 어느 길로 함수를 빠져나가도(중간 return 이 여럿이다) 고리가 반드시 멈춘다.
struct Spinner {
  Spinner()  { spinStart(); }
  ~Spinner() { spinStop(); }
};

// ── client 연결 상태 점 ─────────────────────────────────────────────
// (enum LinkState 는 맨 위 '점' 설정 옆에 있다 — 함수 원형보다 앞이라야 한다)
static LinkState shownLink = (LinkState)-1;   // 화면에 그려 둔 점 색(바뀔 때만 다시 그린다)

// 지금 client 연결 상태. 붙은 client 있음 → 정상, 와이파이도 선도 없음 → 실패, 그 외 → 대기.
// 선으로 온 리더도 '붙은 client' 로 친다 — 와이파이가 죽어도 선만 살아 있으면 태깅은 된다.
// (그래서 선이 살아 있을 때는 와이파이가 없어도 빨강이 아니라 초록이다)
static LinkState linkState() {
  if (linkAlive() || espnowAlive())                  return LINK_OK;
  if (!online)                                       return LINK_FAIL;
  if (serversUp && webSocket.connectedClients() > 0) return LINK_OK;
  return LINK_WAIT;
}

// 상단 점을 지금 상태 색으로 그린다(어두운/밝은 배경 모두에서 보이게 검은 테두리를 두른다).
static void drawLinkDot() {
  const LinkState ls = linkState();
  shownLink = ls;
  uint16_t col = ls == LINK_OK ? DOT_GREEN : (ls == LINK_WAIT ? DOT_YELLOW : DOT_RED);
  gfx->fillCircle(DOT_CX, DOT_CY, DOT_R, col);
  gfx->drawCircle(DOT_CX, DOT_CY, DOT_R, 0x0000);
}

// ── 화면 그리기 ─────────────────────────────────────────────────────
static int photoTotal() { return 1 + sdPhotoCount; }   // 활성 그림 + SD 사진

// 대기 사진 번호의 이름 — 0 은 "enable", 그 밖은 SD 사진의 UID.
static const char *photoLabel(int idx) { return idx == 0 ? "enable" : photoUids[idx - 1]; }

// 대기 사진 번호의 픽셀을 돌려준다. SD 사진은 photoBuf 로 읽는다. 못 읽으면 nullptr.
static const uint16_t *idlePixels(int idx) {
  if (idx == 0) return USEENABLE;
  if (!sdReady || !photoBuf || idx > sdPhotoCount) return nullptr;
  if (bufIdx == idx) return photoBuf;                  // 이미 읽어 둔 사진
  char path[48];
  snprintf(path, sizeof(path), "/talent/%s.565", photoUids[idx - 1]);
  const size_t need = 480 * 480 * sizeof(uint16_t);
  bufIdx = -1;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  const uint32_t t0 = millis();
  const bool ok = (size_t)f.size() == need && f.read((uint8_t *)photoBuf, need) == need;
  f.close();
  if (!ok) return nullptr;
  bufIdx = idx;
  Serial.printf("[SD] %s 읽음 (%lums)\n", path, (unsigned long)(millis() - t0));
  return photoBuf;
}

// 대기 화면 — 지금 사진 + (붙어 있으면) 연결 아이콘.
static void drawIdle() {
  if (photoIdx < 0 || photoIdx >= photoTotal()) photoIdx = 0;
  const uint16_t *px = idlePixels(photoIdx);
  if (!px) {                                           // SD 사진을 못 읽으면 활성 그림으로 돌아간다
    Serial.printf("대기 화면: 사진 #%d(%s) 을 못 읽어 활성 화면으로\n", photoIdx, photoLabel(photoIdx));
    photoIdx = 0;
    px = USEENABLE;
  }
  if (!frame) {                                        // 버퍼를 못 잡았으면 배경만이라도
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)px, 480, 480);
    drawLinkDot();
    return;
  }
  fillBackground(px);
  if (online) overlayConnIcon();
  blitFrame();
  drawLinkDot();
  Serial.printf("대기 화면: 사진 %d/%d (%s) · 연결 아이콘 %s\n",
                photoIdx, photoTotal() - 1, photoLabel(photoIdx), online ? "보임" : "숨김");
}

// NFC 화면 — UID 사진 전체화면 + 상단 이름(+ '지금 기분') + 하단 잔여 포인트.
// note 가 있으면 하단 라벨('잔여 포인트') 자리에 그 말을 대신 쓴다 —
// 출석모드에서 "출석 완료 +5P" 처럼, 무슨 일이 일어났는지 숫자보다 먼저 읽히게.
//
// mood 는 아이가 앱·조회 화면에 적어 둔 한 줄이다. **이름 아래**에 두는 까닭:
// 이 화면에서 먼저 읽혀야 하는 것은 '누구' 와 '얼마' 이고, 기분은 거기 덧붙는 말이다.
// 하단 띠에 두면 포인트 숫자와 다투고, 출석모드의 '출석 완료 +5P' 와 자리가 겹친다.
// 적어 둔 것이 없으면 띠를 넓히지도 않는다 — 빈 띠가 넓으면 사진이 그만큼 가려진다.
//
// moodIconName 이 있으면 그 줄 왼쪽에 **캐릭터 그림**을 둔다(moodIcon 에 이미 담겨 있어야
// 한다 — showNfc 가 챙겨 둔다). 앱의 한 줄 상태와 같은 모양이다: 그림 + 아이가 적은 글.
// 그림과 글자를 **한 덩이로 묶어** 가운데에 놓는다 — 그림만 왼쪽에 붙이면 글이 짧을 때
// 둘이 멀찍이 떨어져 서로 딴 줄처럼 보인다.
static void drawNfc(const uint16_t *photo, const char *name, long points, const char *note,
                    const char *mood, const char *moodIconName = "") {
  if (!frame) {                                        // 버퍼가 없으면 사진만
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)photo, 480, 480);
    drawLinkDot();
    return;
  }
  const bool hasIcon = moodIconName && moodIconName[0];
  const bool hasText = mood && mood[0];
  const bool hasMood = hasIcon || hasText;
  const int topH = hasMood ? BAND_TOP_H + BAND_MOOD_H : BAND_TOP_H;
  const int moodY = BAND_TOP_H + BAND_MOOD_H / 2;
  const int reserve = hasIcon ? MOOD_ICON_PX + MOOD_ICON_GAP : 0;

  // 글자는 띠를 얹기 전에 **재 두기만** 한다 — 그림을 어디에 둘지는 글자 폭이 정하고,
  // 그림은 사진·띠와 한 번에 옮겨야 깜빡이지 않기 때문이다.
  char fit[sizeof(nfcMood)] = "";
  int textW = 0;
  if (hasText) {
    strncpy(fit, mood, sizeof(fit) - 1); fit[sizeof(fit) - 1] = '\0';
    fitEllipsis(fit, sizeof(fit), moodY, PX_MOOD, reserve);
    textW = textWidthAt(fit, moodY, PX_MOOD, reserve);
  }

  fillBackground(photo);
  darkenBand(0, topH, BAND_KEEP);                      // 상단 띠(사진을 조금만 남기고 어둡게)
  darkenBand(BAND_BOT_Y, 480, BAND_KEEP);              // 하단 띠
  if (hasIcon) {                                       // 띠를 어둡게 한 뒤에 — 그림은 밝게 남긴다
    const int groupW = MOOD_ICON_PX + (textW ? MOOD_ICON_GAP + textW : 0);
    overlayMoodIcon(240 - groupW / 2, moodY - MOOD_ICON_PX / 2);
  }
  blitFrame();                                         // 사진+띠+그림을 한 번에

  drawCenteredUTF8(name, 240, BAND_TOP_H / 2, PX_NAME, COL_WHITE);   // 상단: 이름
  // 상단: 기분 — 금색으로 둔다(하단 포인트와 같은 색이라 "서버가 보내 준 그 아이의 값" 으로 읽힌다).
  // 그림이 있으면 그 폭의 절반만큼 오른쪽으로 민다. 덩이의 가운데가 240 이면 글자의 가운데는
  // 240 + (그림 + 사이)/2 다 — 글자 폭과 상관없이 늘 같다(덜어 보면 T 가 지워진다).
  if (fit[0]) drawCenteredUTF8(fit, 240 + reserve / 2, moodY, PX_MOOD, COL_GOLD, reserve);

  char num[24]; commafy(points, num, sizeof(num));
  char pts[32]; snprintf(pts, sizeof(pts), "%s P", num);
  const bool hasNote = note && note[0];
  drawCenteredUTF8(hasNote ? note : "잔여 포인트", 240, BAND_BOT_Y + 26, PX_LABEL,
                   hasNote ? COL_GOLD : COL_WHITE);                  // 하단: 라벨(또는 안내)
  drawCenteredUTF8(pts,                            240, BAND_BOT_Y + 72, PX_POINTS, COL_GOLD);   // 하단: 숫자
  drawLinkDot();

  Serial.printf("NFC 화면: uid=%s 이름=%s 포인트=%ld%s%s%s%s%s%s\n", nfcUid, name, points,
                hasNote ? " · " : "", hasNote ? note : "",
                hasMood ? " · 기분 " : "", hasIcon ? moodIconName : "",
                (hasIcon && hasText) ? " " : "", hasText ? mood : "");
}

// 일괄 지급·사용 화면 — 사진 한 장이 아니라 제목(얼마를 왜) + 받은 아이 명단을 검은
// 배경에 줄줄이 띄운다. 한 아이씩(drawNfc) 보여 주면 여럿일 때 화면이 너무 빨리 넘어가
// 누가 받았는지 놓친다 — 그래서 명단을 **한 번에** 보여 준다.
//
// 원형 화면이라 위아래로 갈수록 좁아지지만, 이름은 짧아(한글 2~4자) 아래 고정폭으로도
// 넉넉히 들어간다. 화면에 다 못 들어갈 만큼 많으면 앞쪽만 쓰고 "+K명 더" 로 묶는다.
static void drawBulk() {
  if (frame) {
    for (int i = 0; i < 480 * 480; i++) frame[i] = 0x0000;
    blitFrame();
  } else {
    gfx->fillScreen(0x0000);
  }
  drawCenteredUTF8(bulkHeadline, 240, 96, PX_BULK_TITLE, COL_GOLD);

  const int lineH  = 32;
  const int firstY = 152;
  const int lastY  = 432;                       // 이 아래로는 원이 너무 좁아진다
  const int maxLines = (lastY - firstY) / lineH + 1;
  int shown = bulkCount;
  bool overflow = false;
  if (shown > maxLines) { shown = maxLines - 1; overflow = true; }   // 마지막 줄을 '+K명' 에 내준다
  for (int i = 0; i < shown; i++)
    drawCenteredUTF8(bulkNames[i], 240, firstY + i * lineH, PX_BULK_NAME, COL_WHITE);
  if (overflow) {
    char more[24];
    snprintf(more, sizeof(more), "+%d명 더", bulkCount - shown);
    drawCenteredUTF8(more, 240, firstY + shown * lineH, PX_BULK_NAME, COL_GOLD);
  }
  drawLinkDot();
  Serial.printf("일괄 화면: %s · %d명%s\n", bulkHeadline, bulkCount, overflow ? " (화면에 다 못 넣어 뒤를 줄임)" : "");
}

// 지금 상태를 붙어 있는 모두(또는 한 client)에게 알린다.
// NFC 화면일 때는 조회 결과까지 함께 보낸다 — 리더(nfcProjectClient)는 서버와 말하지 않으므로
// 자기가 읽은 카드가 어떻게 됐는지 알 길이 이 응답뿐이다(소리를 그것으로 가른다).
// `attend` 는 **이 기기가 실제로 들고 있는** 출석모드다 — 관리 화면의 스위치는 서버에
// 저장된 값이고 이것은 기기가 받아 간 값이다. 둘을 나란히 두어야 "켰는데 이 기기만 아직
// 꺼져 있다"(설정을 못 받았다 · 기기 전용 값이 따로 걸려 있다)가 보인다. 켜도 카드를
// 고르지 않았으면 지급하지 않으므로, 그 상태는 켜짐으로 세지 않는다.
static void buildState(char *msg, size_t cap) {
  const char *attend = (cfgAttendMode && cfgAttendCard[0]) ? "true" : "false";
  if (mode == MODE_NFC)
    snprintf(msg, cap,
             "{\"mode\":\"nfc\",\"uid\":\"%s\",\"name\":\"%s\",\"points\":%ld,"
             "\"known\":%s,\"note\":\"%s\",\"mood\":\"%s\",\"moodIcon\":\"%s\","
             "\"attend\":%s,\"online\":%s}",
             nfcUid, nfcName, nfcPoints, nfcKnown ? "true" : "false", nfcNote, nfcMood,
             nfcMoodIcon, attend, online ? "true" : "false");
  else if (mode == MODE_BULK)
    snprintf(msg, cap,
             "{\"mode\":\"bulk\",\"headline\":\"%s\",\"count\":%d,\"attend\":%s,\"online\":%s}",
             bulkHeadline, bulkCount, attend, online ? "true" : "false");
  else
    snprintf(msg, cap,
             "{\"mode\":\"idle\",\"photo\":\"%s\",\"index\":%d,\"count\":%d,"
             "\"attend\":%s,\"online\":%s}",
             photoLabel(photoIdx), photoIdx, sdPhotoCount, attend, online ? "true" : "false");
}

// 선으로는 **NFC 결과만** 보낸다. 리더가 쓰는 것은 그것뿐이고(부저를 가르고 **말로 읽는다**),
// 대기 화면 알림은 리더에게 아무 쓸모가 없다.
//
// ★ 2026-10-21 부터 이 덩이가 **소리의 대본**이다. 리더에 DFPlayer 가 붙었는데(이 보드는
//   밖으로 I2C 만 내주므로 저쪽에 붙였다 — slave/src/sound.h), 그 모듈이 읽을 말을 고르는
//   재료가 바로 여기 note·points 다. 그래서 note 의 글자를 고치면 **리더의 표(NOTE_CLIPS)도
//   함께** 고쳐야 한다 — 어긋나면 그 문구만 조용해진다(틀리게 읽지는 않는다). 그리고 I2C 한 덩이에 들어가야 해서 mode·online 처럼
// 안 쓰는 칸을 뺀 짧은 꼴로 만든다 — 칸마다 길이를 잘라 두어 버퍼를 넘지 않게 한다
// (넘쳐서 잘리면 JSON 이 깨져 리더가 답을 못 알아본다).
//
// ★ '지금 기분'(mood)은 **일부러 싣지 않는다.** 리더는 이 답으로 소리만 가르고 화면이 없다 —
//   쓰지 않는 칸을 넣으면 한 덩이가 길어져 Wire 버퍼를 넘길 뿐이다. 기분은 화면이 그린다.
//   (라디오 쪽은 250바이트까지 들어가지만, 두 길이 **같은 글자**라야 리더가 한 곳에서 푼다.)
static void linkSendResult() {
  if (mode != MODE_NFC) return;
  char msg[224];
  // ★ note 를 32 → 63바이트로 넓혔다(2026-10-21, 소리를 달면서). 한글 한 자가 3바이트라
  //   32 로는 "서버에 물어보지 못했어요"(35바이트)가 글자 중간에서 잘려, 리더가 그 문구를
  //   표에서 찾지 못해 조용해졌다. nfcNote 가 64바이트이므로 63 이면 늘 통째로 간다.
  snprintf(msg, sizeof(msg),
           "{\"uid\":\"%.24s\",\"name\":\"%.40s\",\"points\":%ld,\"known\":%s,\"note\":\"%.63s\"}",
           nfcUid, nfcName, nfcPoints, nfcKnown ? "true" : "false", nfcNote);
  linkSend(msg);                       // 선 — 살아 있을 때만 나간다
#if ESPNOW_ENABLE
  // 선이 죽어 있을 때만 라디오로. 둘 다 살아 있으면 선으로만 보낸다 — 답이 겹쳐 가도
  // 리더는 먼저 온 것만 쓰지만(handleDisplayReply), 쓸데없이 라디오를 두드릴 일은 없다.
  if (!linkAlive() && readerKnown) espnowSend(readerMac, msg);
#endif
}

// ── 리더에게 "이 말을 읽어 줘" ───────────────────────────────────────
// 스피커는 리더에 있다(까닭은 config.h 의 '소리'). 태깅 결과는 위 linkSendResult 로 저절로
// 건너가지만, **이 보드 제 쪽에서 일어난 일**(와이파이가 끊겼다·설정을 다시 받는다)은
// 보낼 자리가 없었다. 그래서 칸 하나를 더 쓴다 —
//   {"say":"설정 다시 받는 중"}
// 번호가 아니라 **글자**를 보내는 것이 요점이다. 화면에 그리는 글자를 그대로 보내면
// 리더가 제 표에서 토막을 찾고, 모르는 글자는 조용히 지나간다. 소리 번호를 두 저장소에
// 나눠 들고 있지 않아도 된다(funfunFirmware 와 같은 규율 — sound.h 의 '읽는 말 = 화면 글자').
//
// 선이 없으면 라디오로, 둘 다 없으면 그냥 지나간다 — 말 한 마디를 위해 기다리지 않는다.
static void readerSay(const char *note) {
  if (!note || !note[0]) return;
  char msg[96];
  snprintf(msg, sizeof(msg), "{\"say\":\"%.63s\"}", note);
  if (linkAlive()) { linkSend(msg); return; }
#if ESPNOW_ENABLE
  if (readerKnown) espnowSend(readerMac, msg);
#endif
}

// only < 0 이면 모두에게(ws 로 붙은 모두 + 선), only >= 0 이면 그 ws client 한 곳에만.
// mood 가 들어와 한 덩이가 길어졌다 — nfcMood(160) 와 moodIcon(40) 이 통째로 실릴 자리를 둔다.
// moodIcon 이 차 있으면 mood 에는 아이가 적은 글만 담긴다(이름은 그림이 대신한다) — 붙은 쪽이
// 그림을 띄우고 싶으면 /funfun/characters/<동물>-<표정>.png 로 받으면 된다.
// 모자라면 snprintf 가 뒤를 잘라 JSON 이 깨지고, 받는 쪽이 상태를 통째로 못 읽는다.
static void sendState(int8_t only = -1) {
  char msg[640];   // `attend` 가 붙으며 여유가 줄었다 — 넘치면 snprintf 가 잘라 JSON 이 깨진다
  buildState(msg, sizeof(msg));
  if (only >= 0) { webSocket.sendTXT((uint8_t)only, msg); return; }
  webSocket.broadcastTXT(msg);
  // 붙어 있으면 서버에도 같은 글자를 보낸다. 관리 화면의 '기기' 표시가 이것을 받아
  // "지금 무엇을 띄우고 있나" 를 보여 준다 — 눌러 보기 전에 기기가 살아 있는지 가린다.
  if (hubUp) hub.sendTXT(msg);
  linkSendResult();
}

// 대기 화면으로 (다시) 들어간다.
static void goIdle(bool announce = true) {
#if SOUND_SAY_IDLE
  // 들어올 때만 부른다 — 대기 화면은 사진을 바꿀 때마다 다시 그려지므로, 그리는 자리에
  // 두면 쉬지 않고 떠든다. 그래도 줄이 선 현장에서는 시끄럽다(config.h 참고).
  if (mode != MODE_IDLE) readerSay("카드를 올려 주세요");
#endif
  mode = MODE_IDLE;
  drawIdle();
  if (announce) sendState();
}

// 대기 화면 사진을 번호로 바꾼다(끝을 넘으면 처음으로 돈다).
static void showPhoto(int idx) {
  const int n = photoTotal();
  photoIdx = ((idx % n) + n) % n;
  goIdle();
}

// 순차 전환 — 터치·시리얼 n/p·WebSocket toggle/next/prev.
static void nextPhoto() { showPhoto(photoIdx + 1); }
static void prevPhoto() { showPhoto(photoIdx - 1); }
static void homePhoto() { showPhoto(0); }            // 활성 화면으로

// 파일 이름을 가나다(바이트) 순으로 — 매번 같은 순서로 넘어가게.
static int cmpUid(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

// SD 의 /talent/*.565(온전한 480 사진) 를 훑어 대기 사진 목록을 다시 만든다.
static void scanPhotos() {
  sdPhotoCount = 0;
  bufIdx = -1;
  if (sdReady) {
    File dir = SD_MMC.open("/talent");
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f && sdPhotoCount < MAX_PHOTOS; f = dir.openNextFile()) {
        const char *nm = f.name();                       // 코어 3.x 는 경로 없이 이름만 준다
        const char *sl = strrchr(nm, '/'); if (sl) nm = sl + 1;
        const size_t len = strlen(nm);
        if (!f.isDirectory() && f.size() == 480 * 480 * 2 && len > 4 && len - 4 < sizeof(photoUids[0]) &&
            !strcasecmp(nm + len - 4, ".565")) {
          memcpy(photoUids[sdPhotoCount], nm, len - 4);
          photoUids[sdPhotoCount][len - 4] = '\0';
          sdPhotoCount++;
        }
        f.close();
      }
      dir.close();
    }
  }
  qsort(photoUids, sdPhotoCount, sizeof(photoUids[0]), cmpUid);
  if (photoIdx >= photoTotal()) photoIdx = 0;
  Serial.printf("[사진] 넘겨 볼 사진: 활성 화면 + SD %d 장\n", sdPhotoCount);
}

// 넘겨 볼 사진 목록을 시리얼에 찍는다('s').
static void listPhotos() {
  Serial.printf("[사진] 모두 %d 장 (지금 %d)\n", photoTotal(), photoIdx);
  for (int i = 0; i < photoTotal(); i++)
    Serial.printf("  %s%3d  %s\n", i == photoIdx ? ">" : " ", i, photoLabel(i));
}

// NFC 화면을 띄운다(몇 초 뒤 자동으로 대기 화면으로 돌아간다).
// 이름이 비어 있으면(서버에 없는 키링이거나 조회를 못 했을 때) UID 를 대신 띄운다 —
// 빈 띠만 보이면 기기가 멈춘 것인지 카드를 모르는 것인지 구별할 수 없다.
static void showNfc(const char *uid, const char *name, long points, bool known,
                    const char *note = "", const Mood *mood = nullptr) {
  strncpy(nfcUid, uid, sizeof(nfcUid) - 1); nfcUid[sizeof(nfcUid) - 1] = '\0';
  // 이름·안내문도 copyJsonSafe 로 받는다. 서버가 고른 말일 때는 군더더기였지만, 이제
  // **관리 화면(브라우저)이 직접** 보내 올 수 있다 — 이름에 따옴표가 섞여 있으면 상태
  // 알림(buildState)의 JSON 이 그 자리에서 깨져 리더가 답을 통째로 못 알아본다.
  // 글자 경계에서 자르는 것도 함께 얻는다(strncpy 는 한글 한 자를 반만 베낀다).
  copyJsonSafe(nfcName, sizeof(nfcName), (name && name[0]) ? name : uid);
  copyJsonSafe(nfcNote, sizeof(nfcNote), note);

  // 캐릭터 그림을 먼저 챙긴다. 쓸 수 있으면 **앱과 같은 모양**(그림 + 아이가 적은 글)이고,
  // 못 쓰면(SD 도 서버도 못 닿거나 서버가 모르는 캐릭터면) 한국어 이름을 앞에 붙인 한 줄로
  // 되돌아간다 — 어느 쪽이든 아이가 적어 둔 것은 화면에 남는다.
  nfcMoodIcon[0] = '\0';
  const char *line = mood ? mood->line : "";
  if (mood && mood->icon[0] && loadMoodIcon(mood->icon)) {
    strncpy(nfcMoodIcon, mood->icon, sizeof(nfcMoodIcon) - 1);
    nfcMoodIcon[sizeof(nfcMoodIcon) - 1] = '\0';
    line = mood->text;                        // 이름('토끼 인사')은 그림이 대신한다
  }
  // 기분만 copyJsonSafe 로 받는다 — 아이가 제 손으로 적은 글이라 따옴표가 섞일 수 있다.
  copyJsonSafe(nfcMood, sizeof(nfcMood), line);
  nfcPoints = points;
  nfcKnown  = known;

  const uint16_t *photo = loadPhotoForUid(uid);
  mode = MODE_NFC;
  nfcUntil = millis() + NFC_SHOW_MS;
  drawNfc(photo, nfcName, points, nfcNote, nfcMood, nfcMoodIcon);
  sendState();
}

// 서버가 보낸 일괄 지급·사용 알림을 받아 화면에 올린다. 보여 줄 이름이 하나도 없으면
// (명단이 비거나 전부 실패) 아무 일도 하지 않는다 — 빈 화면을 띄울 이유가 없다.
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
// 시리얼 't' 를 누를 때마다 다음 보기로 넘어가고, 한 번 띄우면 1분 동안 그대로 둔다
// (6초 만에 사라지면 들여다볼 새가 없다). 'n'·'h' 를 누르면 곧바로 대기 화면으로 돌아간다.
static void showTestScreen() {
  static const struct {
    const char *name; long points; const char *note;
    const char *icon; const char *label; const char *text;   // 서버가 보내 주는 모양 그대로
  } SAMPLES[] = {
    // 이모티콘 — 그림이 없어 이름('기쁨')이 글 앞에 붙는다
    {"김영민",      1205, "출석 완료 +5P", "",                      "기쁨", "오늘 소풍 가요"},
    // 캐릭터 + 글 — 그림 + 아이가 적은 글(앱과 같은 모양). 처음 띄울 때 한 번 받아 온다
    {"박하은",     12340, "",              "ch-bunny-wave-40.565",  "토끼 인사", "오늘도 화이팅!"},
    // 캐릭터만 — 적어 둔 글이 없으면 그림 한 장만 가운데에 뜬다
    {"남궁민수현", 999999, "출석 완료 +5P", "ch-bear-love-40.565",   "곰 사랑", ""},
    // 굴림에 없는 글자 + 30자 꽉 찬 기분 — 그림 폭만큼 더 일찍 잘려 '...' 가 붙는다
    {"똠방각하",      30, "이미 출석했어요", "ch-cat-sleep-40.565",  "고양이 졸림",
     "어제 늦게 자서 너무 졸려요오"},
  };
  const int n = sizeof(SAMPLES) / sizeof(SAMPLES[0]);
  static int i = 0;
  Mood mood;
  moodSet(&mood, SAMPLES[i].icon, SAMPLES[i].label, SAMPLES[i].text);
  Serial.printf("[명령] 시험 화면 %d/%d — %s · %ld P%s%s%s%s\n", i + 1, n, SAMPLES[i].name, SAMPLES[i].points,
                SAMPLES[i].note[0] ? " · " : "", SAMPLES[i].note,
                mood.line[0] ? " · 기분 " : "", mood.line);
  showNfc("TEST", SAMPLES[i].name, SAMPLES[i].points, true, SAMPLES[i].note, &mood);
  nfcUntil = millis() + 60000;                 // 1분 동안 세워 둔다
  i = (i + 1) % n;
}

// ── microSD (SDMMC 1-bit) ───────────────────────────────────────────
// LILYGO T-RGB 는 SD 를 SDMMC 1-bit 로 붙인다. SD_CS(IO07)는 GPIO 가 아니라 XL9535 확장칩(bus)의
// 인에이블 핀이라, SD_MMC.begin 전에 확장칩으로 HIGH 로 켜야 카드가 잡힌다(공식 installSD 와 같은 순서).
static bool sdBegin() {
  bus->pinMode(SD_EN_EXPANDER, OUTPUT);
  bus->digitalWrite(SD_EN_EXPANDER, 1);        // SD 전원/인에이블 켜기(확장칩 7번)

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true /*1-bit*/, false /*포맷 안 함*/)) {
    Serial.println("[SD] 마운트 실패 — 카드/핀을 확인하세요");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[SD] 카드가 없습니다");
    return false;
  }
  SD_MMC.mkdir("/talent");
  SD_MMC.mkdir("/char");                       // '지금 기분' 의 캐릭터 그림을 받아 두는 곳
  Serial.printf("[SD] 준비됨 (%lluMB)\n", (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

// microSD 의 /talent 폴더에 들어 있는 사진 파일을 시리얼로 나열한다.
// 부팅 때 한 번 찍고, 시리얼로 'l' 을 보내면 다시 찍는다. 460800 바이트라야 온전한 480 사진이다.
static void listSD() {
  if (!sdReady) { Serial.println("[SD] 준비 안 됨 — 카드/핀 확인"); return; }
  File dir = SD_MMC.open("/talent");
  if (!dir || !dir.isDirectory()) { Serial.println("[SD] /talent 폴더가 없습니다"); return; }
  Serial.println("[SD] /talent 목록:");
  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char *nm = f.name();
    const size_t ln = strlen(nm);
    if (ln < 4 || strcasecmp(nm + ln - 4, ".565")) { f.close(); continue; }   // .ver(해시) 등은 건너뛴다
    const size_t sz = f.size();
    Serial.printf("   %-28s %8u bytes%s\n", f.name(), (unsigned)sz,
                  sz == 480 * 480 * 2 ? "  (사진 OK)" : "  (?)");
    n++; f.close();
  }
  dir.close();
  Serial.printf("[SD] 사진 %d 개\n", n);
}

// ── 서버 ────────────────────────────────────────────────────────────
// 연결 **하나를 맺어 계속 쓴다**(keep-alive). 전에는 요청마다 WiFiClientSecure 를
// 부르는 함수의 스택에 두어, 함수가 끝나면 연결이 소멸하고 다음 요청이 또 처음부터
// 맺었다. 그 고정비(DNS+TCP+TLS)를 시리얼로 재 보니 **본문 크기와 무관하게 늘 ~610ms**
// 였다(설정 받기를 8초 간격으로 세 번: 612·612·610ms — 세 번이 2ms 안이라 서버가
// 들쭉날쭉한 것이 아니라 고정비다. 같은 서버에 맥에서 curl 로 재면 67ms 다).
// 태깅 한 번에 출석 지급 한 번이니 아이 앞에서 그 610ms 를 꼬박 기다렸다.
// 치는 곳은 TALENT_SERVER 하나뿐이라 연결을 들고 있으면 핸드셰이크는 처음 한 번뿐이다.
// (funfunFirmware 가 먼저 이렇게 고쳤다 — 그쪽 머리말에 그때 잰 값이 있다.)
//
// ★ 객체가 하나뿐이라 **겹쳐 쓸 수 없다** — 한 요청을 http.end() 로 끝맺은 뒤에 다음을
//   부른다. 지금 서버를 부르는 자리는 모두 차례로 돈다(조회·지급·설정·이름표·사진).
static const uint32_t HTTP_TIMEOUT_MS   = 6000;
static const uint32_t HTTP_KEEPALIVE_MS = 30000;   // 이만큼 조용하면 한 번 찔러 살려 둔다
static WiFiClientSecure httpsCli;
static WiFiClient       httpPlainCli;
static HTTPClient       http;
static bool     httpInsecureSet = false;
static uint32_t httpWarmAt      = 0;

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
// 아직 읽지 않은 스트림이 닫혀 버린다(사진은 460KB 를 흘려 읽는다).
//
// retry: 들고 있는 연결을 서버가 조용히 닫는 일이 있다(keep-alive 시간이 지나면 끊는다).
//   그러면 보내기는 되지만 답이 오지 않는다 — 그때 한 번만 새로 맺어 다시 물어본다.
//   이 되살리기가 없으면 한참 조용했다가 태깅할 때 첫 요청이 늘 실패한다.
//   ★ 다만 **또 보내면 안 되는 요청에는 쓰지 않는다**(출석 지급 POST). 서버가 받아서
//     처리한 뒤에 답만 못 온 경우를 가릴 길이 없어, 다시 보내면 두 번 줄 수 있다.
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

// ── '지금 기분' 의 캐릭터 그림 받기 ────────────────────────────────
// 서버가 이름을 정해 준다(ch-<동물>-<표정>-<크기>.565 — TalentManager 의 statusIconOf).
// 한 번 받아 /char 에 넣어 두고 그 뒤로는 SD 에서 읽는다. 왕복이 드는 것은 그 캐릭터를
// **처음 만났을 때 한 번뿐**이고(3.2KB), 그 한 번도 서버에 이름·잔액을 물으러 간 길에 붙는다.
//
// 못 쓰면 false — 화면이 그림을 포기하고 한국어 이름('토끼 인사')으로 되돌아간다.
// 그림 한 장 때문에 아이 앞에서 화면이 비는 일은 없어야 한다.
static bool loadMoodIcon(const char *name) {
  if (!moodIconNameOk(name)) return false;
  if (!strcmp(moodIconHave, name)) return true;        // 이미 들고 있다 — 읽지도 받지도 않는다
  moodIconHave[0] = '\0';

  const size_t need = sizeof(moodIcon);
  char path[64];
  snprintf(path, sizeof(path), "/char/%s", name);

  if (sdReady) {
    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
      const bool ok = (size_t)f.size() == need && f.read((uint8_t *)moodIcon, need) == need;
      f.close();
      if (ok) { strcpy(moodIconHave, name); return true; }
      SD_MMC.remove(path);                             // 받다 만 것 — 치우고 아래에서 다시 받는다
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    // 부팅 직후(와이파이가 아직 붙는 중)에 카드를 대면 여기로 온다 — 화면은 이름으로
    // 되돌아가 제대로 뜬다. 말없이 넘어가면 "왜 그림이 안 나오나" 를 알 길이 없어 적어 둔다.
    Serial.printf("[기분] 캐릭터 %s — 와이파이가 아직 없어 이름으로 그린다\n", name);
    return false;
  }
  Spinner spin;                                        // 받는 동안 테두리 고리를 돌린다
  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, name);

  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[기분] 캐릭터 %s 못 받음 code=%d\n", name, code);
    return false;
  }
  const int got = http.getStreamPtr()->readBytes((uint8_t *)moodIcon, need);
  http.end();
  if ((size_t)got != need) {                           // 크기가 다르면 서버의 CHAR_PX 가 바뀐 것이다
    Serial.printf("[기분] 캐릭터 %s 크기 안 맞음 %d/%u — 펌웨어의 MOOD_ICON_PX 를 맞추세요\n",
                  name, got, (unsigned)need);
    return false;
  }
  strcpy(moodIconHave, name);
  if (sdReady) {                                       // 다음부터는 왕복 없이 SD 에서 읽는다
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) { f.write((const uint8_t *)moodIcon, need); f.close(); }
  }
  Serial.printf("[기분] 캐릭터 %s 받음\n", name);
  return true;
}

// 받아 둔 캐릭터 그림을 모두 지운다 — 동기화(길게 누르기) 때 부른다.
// 파일 이름에 **내용 해시가 없어서**("ch-토끼-인사-40") 디자인이 바뀌어도 기기는 들고 있는
// 옛 그림을 그대로 쓴다. 사진처럼 이름으로 가릴 수 없으니, 동기화를 다시 받는 자리로 삼는다.
// 폴더를 훑는 동안 지우지 않는다(FAT 목록이 흔들린다) — 이름을 모아 두고 닫은 뒤에 지운다.
static int clearMoodIcons() {
  if (!sdReady) return 0;
  static char doomed[64][40];
  int n = 0;
  File dir = SD_MMC.open("/char");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  for (File f = dir.openNextFile(); f && n < (int)(sizeof(doomed) / sizeof(doomed[0])); f = dir.openNextFile()) {
    const char *nm = f.name();
    const char *sl = strrchr(nm, '/'); if (sl) nm = sl + 1;
    const bool dirEntry = f.isDirectory();
    const size_t len = strlen(nm);
    f.close();
    if (dirEntry || len >= sizeof(doomed[0])) continue;
    memcpy(doomed[n++], nm, len + 1);
  }
  dir.close();
  int gone = 0;
  for (int i = 0; i < n; i++) {
    char path[64];
    snprintf(path, sizeof(path), "/char/%s", doomed[i]);
    if (SD_MMC.remove(path)) gone++;
  }
  moodIconHave[0] = '\0';                              // 들고 있던 것도 버린다
  return gone;
}

// 이미 받아 둔 사진이 최신인가 — /talent/<UID>.565 가 온전하고, /talent/<UID>.ver 에 적힌
// 해시(이름표의 img)가 지금 이름표의 img 와 같으면 true. 그러면 받지 않는다.
static bool photoUpToDate(const char *uid, const char *img) {
  char path[48], ver[48];
  snprintf(path, sizeof(path), "/talent/%s.565", uid);
  snprintf(ver,  sizeof(ver),  "/talent/%s.ver", uid);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  const bool whole = (size_t)f.size() == 480 * 480 * sizeof(uint16_t);
  f.close();
  if (!whole) return false;
  File v = SD_MMC.open(ver, FILE_READ);
  if (!v) return false;
  const String saved = v.readString();
  v.close();
  return saved == img;
}

// 받은 사진의 해시(이름표의 img)를 /talent/<UID>.ver 에 적는다.
static void saveVersion(const char *uid, const char *img) {
  char ver[48];
  snprintf(ver, sizeof(ver), "/talent/%s.ver", uid);
  File v = SD_MMC.open(ver, FILE_WRITE);
  if (v) { v.print(img); v.close(); }
}

// 사진 한 장(480×480 RGB565, 460800B)을 내려받아 /talent/<UID>.565 로 저장. 성공하면 true.
static bool downloadPhoto(const char *img, const char *uid) {
  Spinner spin;                                 // 받는 동안 테두리 고리를 돌린다
  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, img);

  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[동기화] %s 사진 실패 code=%d\n", uid, code); return false; }

  const size_t need = 480 * 480 * sizeof(uint16_t);
  char path[48], tmp[48];
  snprintf(path, sizeof(path), "/talent/%s.565", uid);
  snprintf(tmp,  sizeof(tmp),  "/talent/%s.tmp", uid);

  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) { http.end(); Serial.printf("[동기화] %s 파일 못 엶\n", uid); return false; }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  uint32_t idleAt = millis();
  while (http.connected() && written < need) {
    const size_t avail = stream->available();
    if (avail) {
      const int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n > 0) { f.write(buf, n); written += n; idleAt = millis(); }
    } else {
      if (millis() - idleAt > 5000) break;      // 5초간 아무것도 안 오면 포기
      delay(2);
    }
  }
  f.close();
  http.end();

  if (written != need) { SD_MMC.remove(tmp); Serial.printf("[동기화] %s 크기 안 맞음 %u/%u\n", uid, (unsigned)written, (unsigned)need); return false; }
  SD_MMC.remove(path);                          // 옛 파일을 치우고 tmp 를 정식 이름으로
  SD_MMC.rename(tmp, path);
  saveVersion(uid, img);                        // 다음 동기화에서 같은 사진을 건너뛰게
  return true;
}

// 이름표에 없는 사진을 SD 에서 지운다 — 동기화 끝에 한 번 부른다.
// 아이가 빠지거나 관리자가 사진을 내렸는데도 카드에 파일이 남으면, 넘겨 볼 목록에 계속 뜨고
// 카드도 한 장에 460KB 씩 차오른다. 그래서 이름표(keep)에 없는 UID 의 .565 와 .ver 를 지운다.
// 받다 만 .tmp 는 UID 와 상관없이 지운다(성공하면 정식 이름으로 바뀌므로 남아 있을 이유가 없다).
// keep 에는 '사진이 있는' UID 가 빠짐없이 들어 있어야 한다 — 하나라도 못 적었으면 부르지 않는다.
// 폴더를 훑는 동안 지우지 않는다(FAT 목록이 흔들린다) — 지울 이름을 모아 두고 닫은 뒤에 지운다.
static int pruneUnusedPhotos(const char keep[][UID_MAX], int keepCount) {
  if (!sdReady) return 0;
  static char doomed[MAX_PHOTOS][UID_MAX + 8];   // 지울 파일 이름(확장자까지)
  int n = 0;

  File dir = SD_MMC.open("/talent");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  for (File f = dir.openNextFile(); f && n < MAX_PHOTOS; f = dir.openNextFile()) {
    const char *nm = f.name();                       // 코어 3.x 는 경로 없이 이름만 준다
    const char *sl = strrchr(nm, '/'); if (sl) nm = sl + 1;
    const size_t len = strlen(nm);
    const bool dirEntry = f.isDirectory();
    f.close();
    if (dirEntry || len <= 4 || len >= sizeof(doomed[0])) continue;

    const char *ext = nm + len - 4;
    const bool isTmp = !strcasecmp(ext, ".tmp");
    const bool mine  = isTmp || !strcasecmp(ext, ".565") || !strcasecmp(ext, ".ver");
    if (!mine) continue;                             // 우리가 만든 파일이 아니면 건드리지 않는다

    bool keepIt = false;
    if (!isTmp && len - 4 < UID_MAX) {
      char uid[UID_MAX];
      memcpy(uid, nm, len - 4);
      uid[len - 4] = '\0';
      for (int i = 0; i < keepCount && !keepIt; i++) keepIt = !strcasecmp(uid, keep[i]);
    }
    if (keepIt) continue;

    memcpy(doomed[n], nm, len + 1);
    n++;
  }
  dir.close();

  int gone = 0;
  for (int i = 0; i < n; i++) {
    char path[64];
    snprintf(path, sizeof(path), "/talent/%s", doomed[i]);
    if (SD_MMC.remove(path)) {
      const size_t ln = strlen(doomed[i]);
      if (!strcasecmp(doomed[i] + ln - 4, ".565")) gone++;   // '지운 사진' 은 .565 만 센다
      Serial.printf("[동기화] 안 쓰는 파일 지움 %s\n", doomed[i]);
    }
  }
  if (n >= MAX_PHOTOS) Serial.println("[동기화] 지울 파일이 많습니다 — 다음 동기화에서 이어 지웁니다");
  return gone;
}

// 서버 이름표를 받아, 사진이 있는 UID 마다 480 사진을 내려받아 SD 로 동기화한다.
static void syncPhotos() {
  Spinner spin;                                 // 이름표부터 사진까지 — 끝날 때까지 돈다
  if (WiFi.status() != WL_CONNECTED) { drawStatus("와이파이가 없습니다"); delay(1500); goIdle(); return; }
  if (!sdReady)                      { drawStatus("SD 카드가 없습니다"); delay(1500); goIdle(); return; }

  drawStatus("이름표 받는 중...");
  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/roster?px=%d", TALENT_SERVER, TALENT_PX);

  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) {
    http.end();
    char m[40]; snprintf(m, sizeof(m), "이름표 실패 (%d)", code);
    drawStatus(m); delay(2000); goIdle(); return;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) { drawStatus("이름표 해석 실패"); delay(2000); goIdle(); return; }

  if (!doc["data"].is<JsonArray>()) { drawStatus("이름표가 비어 있습니다"); delay(2000); goIdle(); return; }
  JsonArray data = doc["data"].as<JsonArray>();
  int total = 0;
  for (JsonObject r : data) if (r["img"].is<const char *>()) total++;

  // 이름표에 사진이 있는 UID 를 적어 둔다 — 끝에 여기 없는 사진을 SD 에서 지우는 데 쓴다.
  // 한 개라도 못 적으면(자리가 없거나 UID 가 너무 길다) 지우기를 건너뛴다 — 쓰는 사진을
  // 잘못 지우는 것보다 남겨 두는 편이 낫다.
  static char rosterUids[MAX_PHOTOS][UID_MAX];
  int  rosterCount = 0;
  bool rosterWhole = true;

  int i = 0, ok = 0, same = 0;
  for (JsonObject r : data) {
    const char *uid = r["uid"] | "";
    const char *img = r["img"] | "";
    if (!uid[0] || !img[0]) continue;
    ++i;
    if (rosterCount < MAX_PHOTOS && strlen(uid) < UID_MAX) strcpy(rosterUids[rosterCount++], uid);
    else                                                   rosterWhole = false;
    if (photoUpToDate(uid, img)) { same++; continue; }   // 해시가 같다 — 받지 않는다
    char m[48]; snprintf(m, sizeof(m), "사진 받는 중 %d/%d", i, total);
    drawStatus(m);
    if (downloadPhoto(img, uid)) { ok++; Serial.printf("[동기화] %s 받음 (%s)\n", uid, img); }
  }

  // 이름표에서 빠진 사진을 지운다.
  // 이름표에 사진이 한 장도 없으면 지우지 않는다 — 서버·DB 가 잠깐 비어 빈 이름표를 받았을 때
  // 카드를 다 비워 버리면 현장에서 되돌릴 방법이 없다. 정말 다 내렸다면 카드를 직접 비우면 된다.
  int gone = 0;
  if (rosterCount == 0)   Serial.println("[동기화] 이름표에 사진이 없어 지우기는 건너뜁니다");
  else if (!rosterWhole)  Serial.println("[동기화] 이름표가 너무 커서 지우기는 건너뜁니다");
  else                    gone = pruneUnusedPhotos(rosterUids, rosterCount);

  // 받아 둔 캐릭터 그림도 비운다 — 그림이 바뀌었을 때 되받을 자리가 여기뿐이다(clearMoodIcons).
  // 다시 받는 것은 그 캐릭터를 고른 아이가 처음 카드를 댈 때라, 여기서 기다릴 일은 없다.
  const int chars = clearMoodIcons();

  Serial.printf("[동기화] 새로 받음 %d · 그대로 %d · 실패 %d · 지움 %d (모두 %d) · 캐릭터 비움 %d\n",
                ok, same, total - ok - same, gone, total, chars);
  char m[48]; snprintf(m, sizeof(m), "완료 - 새 %d 장 · 지움 %d 장", ok, gone);
  Serial.printf("[동기화] %s\n", m);
  drawStatus(m); delay(2000);
  scanPhotos();                                 // 새로 받은 사진까지 넘겨 볼 목록에 넣는다
  goIdle();
}

// ── 서버 설정 받기 (리더 설정 → v2.0) ──────────────────────────────
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
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[설정] 받기 실패 code=%d\n", code); return; }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) { Serial.println("[설정] 해석 실패"); return; }
  JsonObject c = doc["config"].as<JsonObject>();
  if (c.isNull()) { Serial.println("[설정] config 가 없다"); return; }

  const uint8_t bl = (uint8_t)(c["backlight"] | (int)cfgBacklight);
  const bool wasAttending = cfgAttendMode && cfgAttendCard[0];
  cfgAttendMode = c["attendanceMode"] | false;
  strncpy(cfgAttendCard, c["attendanceCard"] | "", sizeof(cfgAttendCard) - 1);
  cfgAttendCard[sizeof(cfgAttendCard) - 1] = '\0';

  if (bl >= 1 && bl <= 16 && bl != cfgBacklight) {    // 바뀔 때만 흔든다(같은 값이면 깜빡일 뿐이다)
    cfgBacklight = bl;
    backlightOn(cfgBacklight);
  }
  cfgGotOnce = true;
  Serial.printf("[설정] 밝기 %d/16 · 출석모드 %s%s%s\n", cfgBacklight,
                cfgAttendMode ? "켜짐" : "꺼짐",
                cfgAttendMode ? " · 카드 " : "",
                cfgAttendMode ? (cfgAttendCard[0] ? cfgAttendCard : "없음(지급 안 함)") : "");
  // 출석모드가 실제로 바뀌었으면 붙어 있는 쪽(서버·브라우저)에 바로 알린다 — 관리
  // 화면의 스위치를 누른 사람이 "이 기기가 받았구나" 를 그 자리에서 본다. 화면이
  // 바뀐 것은 아니므로 값이 그대로면 보내지 않는다(이 알림은 드물어야 한다).
  if (wasAttending != (cfgAttendMode && cfgAttendCard[0])) sendState();
}

// ── 서버가 보내 준 '지금 기분' 한 줄로 ──────────────────────────────
// 응답의 `status` 는 { emoji, label, text } 다. **label 만 쓴다** — emoji 는 😀 같은 날글자라
// 이 화면의 폰트에 글리프가 없어 네모(두부)로 뜨고, label 이 그것의 한국어 이름이다
// (서버의 deviceStatus·statusLabelOf). 둘을 '기쁨 오늘 소풍 가요' 처럼 한 줄로 잇는다.
// status 가 없으면(적어 둔 것이 없다) 빈 문자열 — 화면이 그 줄을 그리지 않는다.
// icon 이 있으면(캐릭터를 골랐다) 그 그림을 받아 와 이름 대신 띄운다 — 앱과 같은 모양이다.
static void moodFromJson(JsonVariantConst status, Mood *m) {
  moodSet(m, "", "", "");
  if (status.isNull()) return;
  moodSet(m, status["icon"] | "", status["label"] | "", status["text"] | "");
}

// ── 이름·잔액·기분 조회 ────────────────────────────────────────────
// GET /api/talent/feed?uid=<UID>&limit=0 → { who: { name, balance, known, status } }
// 예전에는 리더(nfcProjectClient)가 이 일을 했다. 서버와 말하는 쪽을 디스플레이 하나로
// 모으면서 이리로 옮겼다 — 출석 지급도 여기서 하므로 한 태깅이 한 곳에서만 처리된다.
//
// mood 는 넘기지 않아도 된다(nullptr) — 안 쓰는 곳에서 버퍼를 만들지 않게.
// 서버가 status 를 모르는 옛 판이면 빈 값이 되고 화면은 전과 똑같이 그려진다.
static bool lookupTalent(const char *uid, char *name, size_t nameCap, long *points, bool *known,
                         Mood *mood = nullptr) {
  Spinner spin;
  if (WiFi.status() != WL_CONNECTED) return false;
  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/feed?uid=%s&limit=0", TALENT_SERVER, uid);

  const uint32_t t0 = millis();
  const int code = httpGet(url);
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[서버] 조회 실패 code=%d\n", code); return false; }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) { Serial.println("[서버] 응답 해석 실패"); return false; }
  strncpy(name, doc["who"]["name"] | "", nameCap - 1); name[nameCap - 1] = '\0';
  *points = doc["who"]["balance"] | 0L;
  *known  = doc["who"]["known"] | false;
  if (mood) moodFromJson(doc["who"]["status"], mood);
  Serial.printf("[서버] %s → %s · %ld P%s%s%s (%lums)\n", uid, name, *points,
                *known ? "" : " (서버에 없는 키링)",
                (mood && mood->line[0]) ? " · 기분 " : "", (mood && mood->line[0]) ? mood->line : "",
                (unsigned long)(millis() - t0));
  return true;
}

// ── 출석 지급 ───────────────────────────────────────────────────────
// POST /api/talent/earn { uid, card, device } → { name, balance, delta, item }
// 금액은 보내지 않는다 — 얼마를 어떤 사유로 줄지는 서버에 등록된 그 카드가 정한다.
// 기기는 현장에 놓인 물건이라 요청을 흉내 내기 쉬운데, 값이 서버에만 있으면 그래 봐야
// 관리자가 정한 범위를 벗어나지 못한다(v1.0 리더와 같은 규칙).
static bool attendEarn(const char *uid, char *name, size_t nameCap, long *points, long *delta,
                       Mood *mood = nullptr, char *deny = nullptr, size_t denyCap = 0) {
  Spinner spin;
  if (WiFi.status() != WL_CONNECTED) return false;
  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/earn", TALENT_SERVER);

  char payload[192];
  snprintf(payload, sizeof(payload), "{\"uid\":\"%s\",\"card\":\"%s\",\"device\":\"%s\"}",
           uid, cfgAttendCard, TALENT_DEVICE_ID);

  // retry=false — 서버가 받아 처리한 뒤 답만 못 온 경우를 가릴 길이 없어, 다시 보내면
  // 포인트를 두 번 줄 수 있다. 연결이 닫혀 있었으면 이 한 번은 실패하고 조회로 넘어간다.
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
  if (deserializeJson(doc, body)) { Serial.println("[출석] 응답 해석 실패"); return false; }
  strncpy(name, doc["name"] | "", nameCap - 1); name[nameCap - 1] = '\0';
  *points = doc["balance"] | 0L;
  *delta  = doc["delta"] | 0L;
  // 지급 응답도 status 를 함께 준다 — 출석모드에서는 이 한 번으로 화면을 그리므로,
  // 기분을 따로 물으면 리더 앞에 선 아이를 왕복 한 번만큼 더 기다리게 한다.
  if (mood) moodFromJson(doc["status"], mood);
  Serial.printf("[출석] %s +%ldP → 잔액 %ld P\n", uid, *delta, *points);
  return true;
}

// ── 미등록 카드 알리기 ──────────────────────────────────────────────
// POST /api/talent/seen { uid, device } → 관리자 화면의 '미등록카드' 에 뜨게 한다.
// 서버에 없는 키링을 만나면 부른다 — 이걸 부르지 않으면 화면에 "등록되지 않은 키링"만
// 뜰 뿐 서버에는 아무 기록이 남지 않아, 관리자가 등록할 대상 자체가 생기지 않는다.
// (v1.0 리더 TalentNfcReader 의 reportSeen 과 같은 일. v2.0 은 서버와 말하는 쪽이
//  디스플레이 하나라 여기서 부른다.) 서버가 이미 아는 UID 는 걸러 기록하지 않는다.
static void reportSeen(const char *uid) {
  if (WiFi.status() != WL_CONNECTED) return;
  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/seen", TALENT_SERVER);

  char payload[128];
  snprintf(payload, sizeof(payload), "{\"uid\":\"%s\",\"device\":\"%s\"}", uid, TALENT_DEVICE_ID);

  // 되살려 보내도 덧나지 않는다 — 포인트가 오가지 않고, 관리 화면의 '미등록카드' 에
  // 같은 UID 가 한 번 더 올라갈 뿐이다.
  const int code = httpPost(url, payload, true);
  http.end();
  Serial.printf("[미등록] %s 알림 (code=%d)\n", uid, code);
}

// 이 키링이 방금 출석했나 — ATTEND_COOLDOWN_MS 안이면 true(다시 주지 않는다).
static bool attendedRecently(const char *uid) {
  const uint32_t nowMs = millis();
  for (int i = 0; i < attendSeenCount; i++)
    if (!strcmp(attendSeen[i].uid, uid) && (nowMs - attendSeen[i].at) < ATTEND_COOLDOWN_MS) return true;
  return false;
}

// 출석한 키링을 기억한다. 자리가 차면 가장 오래된 것을 밀어낸다.
static void rememberAttend(const char *uid) {
  const uint32_t nowMs = millis();
  for (int i = 0; i < attendSeenCount; i++) {
    if (!strcmp(attendSeen[i].uid, uid)) { attendSeen[i].at = nowMs; return; }
  }
  int slot = attendSeenCount;
  if (attendSeenCount < ATTEND_RECENT_MAX) {
    attendSeenCount++;
  } else {
    slot = 0;                                   // 가장 오래 전에 온 것을 찾아 덮는다
    for (int i = 1; i < ATTEND_RECENT_MAX; i++)
      if ((int32_t)(attendSeen[i].at - attendSeen[slot].at) < 0) slot = i;
  }
  strncpy(attendSeen[slot].uid, uid, sizeof(attendSeen[slot].uid) - 1);
  attendSeen[slot].uid[sizeof(attendSeen[slot].uid) - 1] = '\0';
  attendSeen[slot].at = nowMs;
}

// ── 태그 한 건 처리 ─────────────────────────────────────────────────
// 리더가 넘긴 UID 하나로 (1) 출석모드면 지급하고 (2) 아니면 이름·잔액만 물어
// (3) 그 결과를 화면에 띄운다. loop 에서 부른다 — 서버 왕복이 있어 콜백 안에서 하지 않는다.
static void processTag(const char *uid) {
  char name[40] = "";
  Mood mood;                   // 그 아이가 적어 둔 '지금 기분'(캐릭터 그림 이름 + 글 + 대비용 한 줄)
  moodSet(&mood, "", "", "");
  long points = 0, delta = 0;
  bool known = false;
  // 서버가 출석을 거절하며 보낸 까닭(출석 시간이 아니다 / 포인트 없는 시간대다).
  // 비어 있으면 거절이 아니었다는 뜻이다.
  char deny[64] = "";

  // 출석모드 — 카드를 고르지 않았으면 지급하지 않는다(관리자 화면이 경고로 알린다).
  if (cfgAttendMode && cfgAttendCard[0]) {
    if (attendedRecently(uid)) {
      if (lookupTalent(uid, name, sizeof(name), &points, &known, &mood))
        showNfc(uid, name, points, known, "이미 출석했어요", &mood);
      else
        showNfc(uid, "", 0, false, "이미 출석했어요");
      return;
    }
    if (attendEarn(uid, name, sizeof(name), &points, &delta, &mood, deny, sizeof(deny))) {
      rememberAttend(uid);
      char note[32];
      snprintf(note, sizeof(note), "출석 완료 +%ldP", delta);
      showNfc(uid, name, points, true, note, &mood);
      return;
    }
    // 지급이 안 됐다 — 조회라도 해서 사진·잔액은 띄운다. 아무 반응이 없으면
    // 아이는 기기가 고장난 줄 알고 계속 카드를 댄다.
    Serial.println("[출석] 지급하지 못해 조회만 한다");
  }

  if (lookupTalent(uid, name, sizeof(name), &points, &known, &mood)) {
    if (!known) reportSeen(uid);   // 서버에 없는 키링 — '미등록카드' 에 뜨게 알린다
    // 거절당했으면 그 까닭을 띄운다. 사진·잔액은 그대로 보여 준다 — 아무 반응이
    // 없으면 아이는 기기가 고장난 줄 알고 계속 카드를 댄다.
    showNfc(uid, name, points, known, deny[0] ? deny : (known ? "" : "등록되지 않은 키링"), &mood);
  } else {
    showNfc(uid, "", 0, false, "서버에 물어보지 못했어요");
  }
}

// ── WebSocket 명령 풀기 ─────────────────────────────────────────────
// 글자 명령 하나 — enable/disable = 활성 화면, toggle/next = 다음 사진, prev = 이전 사진.
static void handleWord(const char *s) {
  // 'config' 는 화면을 바꾸지 않는다 — "설정을 지금 다시 받아라" 다. 값은 실려 오지
  // 않는다: 이 기기가 **제 기기 키로** 다시 물어 가져간다(관리 화면이 지은 설정을
  // 그대로 믿지 않는다 — 서버가 보낼 말을 고르는 규율과 같다).
  if      (!strcmp(s, "config"))                          cfgWanted = true;
  else if (!strcmp(s, "enable") || !strcmp(s, "disable")) homePhoto();
  else if (!strcmp(s, "toggle") || !strcmp(s, "next"))    nextPhoto();
  else if (!strcmp(s, "prev"))                            prevPhoto();
}

// JSON 에 "uid" 가 있으면 NFC 알림, 아니면 대기 화면 전환 명령으로 본다.
static void handleCommand(const char *text, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, text, len) == DeserializationError::Ok) {
    if (!doc["bulk"].isNull()) {                        // 관리 화면의 일괄 지급·사용 — 명단을 한 번에
      showBulk(doc["bulk"]["headline"] | "", doc["bulk"]["names"]);
      return;
    }
    if (doc["uid"].is<const char *>()) {               // NFC 태그 — 리더는 UID 만 보낸다
      const char *uid = doc["uid"] | "";
      if (!uid[0]) return;
      // 이름·잔액이 함께 온 옛 규약(리더가 직접 조회하던 시절)도 그대로 받는다.
      // 그때는 이미 다 알고 있으니 서버에 다시 묻지 않고 바로 띄운다.
      if (doc["points"].is<long>() || doc["points"].is<int>()) {
        // 보내는 쪽이 기분을 적어 주면 그대로 띄운다(시험·브라우저용). 없으면 빈 값이다.
        // "moodIcon" 까지 적어 주면 캐릭터 그림도 띄운다 — 이쪽은 서버처럼 이름(label)을
        // 따로 주지 않으니, 적어 준 글을 그림 옆에 그대로 둔다.
        // "note" 는 '무엇을 해서 바뀌었나' 한 줄이다 — 관리 화면이 포인트를 조정하면
        // '+5P 출석' 처럼 적어 보낸다(funfunServer 의 composables/useNfcDisplay.js).
        // 없으면 빈 값이고, 그때는 예전처럼 이름·잔액만 뜬다.
        // 서버가 밀어 주는 것은 조회 응답과 **같은 모양**이다(`status: {icon,label,text}`) —
        // 그쪽과 같은 함수로 푼다. 한 줄로 적어 보내는 옛 길(moodIcon·mood)도 그대로 받는다.
        Mood m;
        if (!doc["status"].isNull()) moodFromJson(doc["status"], &m);
        else                         moodSet(&m, doc["moodIcon"] | "", "", doc["mood"] | "");
        showNfc(uid, doc["name"] | "", doc["points"] | 0L, true, doc["note"] | "", &m);
        return;
      }
      strncpy(pendingUid, uid, sizeof(pendingUid) - 1);   // 서버 왕복은 loop 에서
      pendingUid[sizeof(pendingUid) - 1] = '\0';
      return;
    }
    if (doc["enabled"].is<bool>()) { homePhoto(); return; }   // 비활성은 없앴다 — 어느 쪽이든 활성 화면
    handleWord(doc["state"] | "");
    return;
  }
  // JSON 이 아니면 그냥 글자 명령(WebSockets 라이브러리가 payload 를 널로 끝맺는다)
  handleWord(text);
}

static void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] #%u 붙음 %s\n", num, ip.toString().c_str());
      sendState(num);                        // 붙자마자 지금 상태를 알려 준다
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
      break;                                 // BIN·PING 등은 쓰지 않는다
  }
}

// ── 서버에서 온 것 ──────────────────────────────────────────────────
// 받는 말은 리더가 보내는 것과 같아 `handleCommand` 하나로 간다. 붙는 순간 지금 화면을
// 한 번 알려 준다 — 서버는 그때까지 이 기기가 무엇을 띄우고 있는지 알 길이 없다.
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
  char host[80]; uint16_t port; bool ssl;
  if (!hubTarget(host, sizeof(host), &port, &ssl)) {
    Serial.println("[허브] TALENT_SERVER 에서 주소를 읽지 못했다");
    return;
  }
  // 기기 키로 인증한다 — HTTP 로 조회·지급할 때 싣는 그 값이고, 서버는 그 키로
  // **어느 교회인지**까지 가른다(funfunServer 의 tenant/resolve.js 의 cidByKey).
  char headers[200];
  snprintf(headers, sizeof(headers), "x-talent-key: %s\r\nx-talent-device: %s",
           TALENT_DEVICE_KEY, TALENT_DEVICE_ID);
  hub.setExtraHeaders(headers);
  hub.setReconnectInterval(HUB_RETRY_MS);
  // 조용할 때 끊긴 줄 모르고 붙어 있는 일을 막는다. 느슨하게 잡은 까닭: 이 보드는
  // 사진을 받는 동안(수 초) loop 가 멈춰 있어, 빡빡하면 멀쩡한 연결을 제 손으로 끊는다.
  hub.enableHeartbeat(25000, 10000, 3);
  hub.onEvent(hubEvent);
  if (ssl) hub.beginSSL(host, port, TALENT_HUB_PATH);
  else     hub.begin(host, port, TALENT_HUB_PATH);
  hubBegun = true;
  Serial.printf("[허브] %s://%s:%u%s 에 붙는다\n", ssl ? "wss" : "ws", host, port, TALENT_HUB_PATH);
#endif
}

// ── 선(I2C)으로 리더에게 물어보기 ───────────────────────────────────
// 슬레이브는 먼저 말을 걸 수 없으니 이쪽이 주기적으로 "줄 것 있나" 하고 읽어 온다.
// 한 덩이는 [0]=글자 길이(0 = 줄 것 없음) · [1..]=글자 다. 리더가 답을 했다는 것
// 자체가 살아 있다는 증거라, 따로 ping 을 주고받지 않는다.
//
// 리더가 버스에 없을 때 requestFrom 을 부르면 코어가 실패마다 에러 두 줄을 시리얼에 쏟는다 —
//   [E][esp32-hal-i2c-ng.c:321] i2cRead(): i2c_master_receive failed: ESP_ERR_INVALID_STATE
//   [E][Wire.cpp:539] requestFrom(): i2cRead returned Error 259
// 이 줄은 코어 안에서 ets_printf 로 바로 나가는 것이라 esp_log_level_set 으로는 막히지 않는다
// (CORE_DEBUG_LEVEL 을 0 으로 낮추면 사라지지만, 그러면 코어의 다른 경고도 함께 사라진다).
// 그래서 **읽기 전에 주소만 두드려 본다**(linkPresent) — 대답이 없으면 requestFrom 을 아예
// 부르지 않는다. 두드리기는 실패해도 조용하다. 선을 안 문 기기는 시리얼이 깨끗하다.
// 한 줄에 ~9ms 씩 잡아먹던 것도 함께 없어져 loop·터치가 늘어지지 않는다. 그래도 헛되게
// 두드리는 횟수를 줄이려고, LINK_IDLE_AFTER 번 잇달아 답이 없으면 LINK_IDLE_POLL_MS 로
// 늦추고 답이 오면 바로 LINK_POLL_MS 로 돌아간다.
#if LINK_I2C_ENABLE
#define LINK_OUT_LEN 56
#define LINK_REC_LEN (1 + LINK_OUT_LEN)
static uint32_t linkPollAt = 0;
static uint32_t linkReadErrorAt = 0;
static uint8_t  linkMissCount = 0;         // 잇달아 답이 없던 횟수 — 차면 LINK_IDLE_POLL_MS 로 늦춘다

// 리더가 선 너머에 있나 — 주소만 두드려 보고 대답(ACK)이 오는지 본다.
// Wire.write() 를 하나도 하지 않고 endTransmission() 하면, 코어는 보낼 것이 없는 이 요청을
// '주소 두드리기'(i2c_master_probe)로 바꿔 보낸다. 그 길은 실패해도 자세히 로그(log_v)로만
// 적어 시리얼이 조용하다 — 읽기·쓰기는 실패마다 에러 줄을 찍는다.
static bool linkPresent() {
  Wire.beginTransmission((uint8_t)LINK_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

static void linkService() {
  if ((int32_t)(millis() - linkPollAt) < 0) return;
  linkPollAt = millis() + (linkMissCount >= LINK_IDLE_AFTER ? LINK_IDLE_POLL_MS : LINK_POLL_MS);

  if (!linkPresent()) {                            // 선에 리더가 없다 — 읽지 않고 조용히 나간다
    if (linkMissCount < 255) linkMissCount++;
    if (linkMissCount == LINK_IDLE_AFTER)          // 없다고 볼 때 한 줄만. 그 뒤로는 말없이 두드린다
      Serial.printf("[선] 리더(0x%02X) 가 선에 없다 — %dms 마다 두드려 본다\n",
                    LINK_I2C_ADDR, LINK_IDLE_POLL_MS);
    return;
  }

  const size_t received = Wire.requestFrom((uint8_t)LINK_I2C_ADDR, (size_t)LINK_REC_LEN);
  if (received != LINK_REC_LEN) {                  // 대답은 했는데 덩이가 덜 왔다 — 드문 일이라 알린다
    if (linkMissCount < 255) linkMissCount++;
    if (!linkReadErrorAt || millis() - linkReadErrorAt >= 5000) {
      Serial.printf("[I2C] 리더(0x%02X) 읽기 실패: %u/%u바이트\n",
                    LINK_I2C_ADDR, (unsigned)received, LINK_REC_LEN);
      linkReadErrorAt = millis();
    }
    while (Wire.available()) Wire.read();          // 덜 온 것을 치우고 나간다
    return;
  }
  uint8_t rec[LINK_REC_LEN];
  for (size_t i = 0; i < LINK_REC_LEN; i++) rec[i] = (uint8_t)Wire.read();
  linkAliveUntil = millis() + LINK_ALIVE_MS;       // 답을 했다 = 리더가 선 너머에 살아 있다
  if (linkMissCount >= LINK_IDLE_AFTER) Serial.println("[선] 리더가 돌아왔다 — 다시 촘촘히 묻는다");
  linkMissCount = 0;

  const uint8_t n = rec[0];
  if (!n || n > LINK_OUT_LEN) return;              // 줄 것이 없다(또는 길이가 엉뚱하다)
  char line[LINK_OUT_LEN + 1];
  memcpy(line, rec + 1, n);
  line[n] = '\0';
  Serial.printf("[선] 받음: %s\n", line);
  handleCommand(line, n);
}
#else
static void linkService() {}
#endif

#if ESPNOW_ENABLE
// 라디오로 온 것을 푼다 — 콜백은 베껴 두기만 하고 푸는 것은 여기(loop)다.
// 선 쪽 linkService 와 같은 자리·같은 일이고, 글자도 똑같은 JSON 이다.
static void espnowService() {
  if (readerPeerPending) {             // 콜백이 배워 둔 MAC 을 여기서 짝으로 등록한다
    readerPeerPending = false;
    espnowPeer(readerMac);
  }
  if (!espnowInReady) return;
  char line[sizeof(espnowIn)];
  strncpy(line, espnowIn, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  espnowInReady = false;               // **베낀 뒤에** 비운다 — 푸는 사이 다음 것이 덮지 않게

  // 짝 맺기 인사 — 리더가 채널을 돌며 부르는 소리다. 바로 답해 줘야 리더가 "여기구나"
  // 하고 그 채널에 자리 잡는다(리더 쪽 espnowHunt). 답이 늦으면 리더가 지나쳐 간다.
  if (strstr(line, "\"hi\"")) {
    espnowSend(readerMac, "{\"hello\":\"display\"}");
    Serial.printf("[라디오] 리더를 찾았다 %02X:%02X:%02X:%02X:%02X:%02X\n",
                  readerMac[0], readerMac[1], readerMac[2], readerMac[3], readerMac[4], readerMac[5]);
    return;
  }
  Serial.printf("[라디오] 받음: %s\n", line);
  handleCommand(line, strlen(line));
}
#else
static void espnowService() {}
#endif

// ── 와이파이 (아는 곳 여러 곳, creds.h) ────────────────────────────
// 목록(wifiAps)은 creds.h 가 짠다 — 폰으로 적어 넣은 것이 앞, 구울 때 넣은 네 벌이 뒤다.
// 앞선 순위부터 붙어 본다. 한 곳에 WIFI_TRY_MS 만큼 붙어 보고 안 되면 다음 순위로,
// 다 안 되면 처음으로 돌아가 계속 반복한다(루프). WiFi.begin() 은 기다리지 않고
// 바로 돌아오므로 붙는 동안에도 loop 가 멈추지 않는다 — 터치로 그림이 바뀌어야 하기 때문이다.

// ── 둘러보고 나서 건다 (2026-10-18) ───────────────────────────────
// 처음에는 목록의 네 곳을 차례로 begin() 해 봤다. 그런데 **없는 AP 에 걸어 보는
// 것이 모든 말썽의 뿌리**였다. 코어의 disconnect() 는 붙어 있던 것이 떨어지기만
// 기다리므로(STA.cpp: connected() 가 거짓이면 즉시 반환) 한 번도 못 붙은 자리에서는
// 곧바로 돌아오고, IDF 는 아직 connecting 이다. 그 위에 begin() 이
// esp_wifi_set_config 를 걸면 "sta is connecting, cannot set config"
// (ESP_ERR_WIFI_STATE) 로 거부된다.
//
// ★ DISCONNECTED 이벤트를 기다려도 소용없다 — 실기에서 확인했다. 그 이벤트는 한
//   번의 붙어 보기가 끝났다는 뜻일 뿐이고, IDF 는 그 뒤에도 connecting 으로 남아
//   있어 바로 건 begin() 이 또 거부된다. 이벤트를 "이제 걸어도 된다" 는 신호로
//   삼았던 판은 틀렸다.
//
// 그래서 **먼저 둘러본다**(스캔). 눈에 보이는 AP 중에서 목록에서 가장 앞선 것
// 하나만 고르고, 끊기를 건 뒤 넉넉히 틈을 두고 begin() 을 **한 번** 건다. 없는
// 곳에는 아예 걸지 않으니 거부가 생길 일이 거의 없다.
//
// ★ 아는 AP 가 하나도 안 보이면 **라디오를 가만히 둔다.** 둘러보는 동안에도
//   채널이 움직여 리더의 ESP-NOW 가 흔들리므로, 헛걸음이 거듭될수록 쉬는 시간을
//   늘린다(15초 → 두 배씩 → 최대 2분). 공유기를 못 찾는 자리에서 태깅만이라도
//   살아 있게 하려는 것이다 — 채널이 가만히 있어야 리더가 자리를 잡는다.
static const uint32_t WIFI_TRY_MS     = 15000;   // 한 번 걸어 보고 이만큼 기다린다
static const uint32_t WIFI_SETTLE_MS  = 1000;    // 끊기를 건 뒤 begin() 까지 두는 틈
static const uint32_t WIFI_REST_MIN_MS = 15000;  // 헛걸음 뒤 다시 둘러보기까지
static const uint32_t WIFI_REST_MAX_MS = 120000; // 거듭되면 여기까지 늘린다
static const uint32_t WIFI_SCAN_MAX_MS = 12000;  // 스캔이 이만큼 넘게 안 끝나면 접는다
static const int      WIFI_REJECT_MAX  = 8;      // 거부가 이어서 이만큼이면 라디오를 껐다 켠다
// 라디오를 껐다 켜도 안 풀릴 때 보드를 다시 세운다. 다만 **부팅 직후에는 하지
// 않는다** — 그랬다가 20초마다 다시 세우는 맴돌이에 빠진 적이 있다(실기).
static const int      WIFI_RADIO_RESET_MAX = 3;
static const uint32_t WIFI_REBOOT_AFTER_MS = 300000;   // 켠 지 5분은 지나야 다시 세운다

enum WifiPhase : uint8_t { WP_REST, WP_SCAN, WP_SETTLE, WP_CONNECT };
static WifiPhase wifiPhase   = WP_REST;
static uint32_t  wifiPhaseAt = 0;
static uint32_t  wifiRestMs  = WIFI_REST_MIN_MS;
static int       wifiPick    = -1;      // 스캔에서 고른 순위(-1 = 아직 없음)
static int       wifiRejects = 0;       // 이어서 거부된 횟수
static int       wifiRadioResets = 0;   // 라디오를 껐다 켠 횟수(붙으면 0 으로)
static bool      wifiEverOnline  = false;  // 부팅한 뒤 한 번이라도 붙어 봤나(설정 모드 자동 진입 조건)
static int       wifiNoApRounds  = 0;      // 둘러봐도 아는 이름이 없던 바퀴 수(붙으면 0 으로)

// ── 이어서 실패한 곳은 건너뛴다 (2026-10-26) ───────────────────────
// 고르는 규칙은 '보이는 것 중 가장 앞선 순위' 하나다. 그런데 그 한 곳의 비밀번호가
// 틀리면(공유기에서 비밀번호를 바꿨다거나) 그곳이 계속 뽑혀, 아래 순위에 멀쩡히 맞는
// 것이 있어도 영영 가 보지 못한다 — 폰으로 적어 넣은 것이 늘 1순위라 더 잘 생긴다.
// 그래서 이어서 WIFI_BAD_MAX 번 실패한 자리는 잠깐 건너뛴다. 보이는 곳이 모두
// 건너뛸 자리가 되면 셈을 모두 지우고 처음부터 다시 돈다(영영 포기하지는 않는다 —
// 공유기가 돌아오는 중일 수도 있다).
static const int WIFI_BAD_MAX = 3;
static uint8_t   wifiBad[sizeof(wifiAps) / sizeof(wifiAps[0])] = { 0 };
static volatile bool wifiStaBusy = false;   // begin() 뒤 결판을 기다린다(이벤트가 내린다)
static volatile uint8_t wifiLastReason = 0; // 마지막으로 끊긴 까닭(0 = 아직 없다)

// 붙어 보기의 결판이 여기로 온다. **와이파이 일꾼 위에서** 불리므로 깃발만 내린다.
// 이것은 "걸어도 된다" 는 신호가 아니라 "이번 붙어 보기는 끝났다" 는 신호다 —
// 15초를 다 기다리지 않고 다음 바퀴로 넘어가는 데만 쓴다(까닭은 위 머리말에).
//
// ★ CONNECTED 는 **결판이 아니다**(2026-10-24). 공유기와 연결은 맺었지만 아직 주소를
//   못 받은 자리다 — 코어는 그때 상태를 WL_IDLE_STATUS 로만 놓고 WL_CONNECTED 는
//   GOT_IP 에서야 세운다(STA.cpp). 그런데 여기서 깃발을 내렸더니 WP_CONNECT 가 DHCP 가
//   끝나기도 전에 "못 붙었다" 로 접고, 멀쩡히 붙고 있던 것을 disconnect() 로 끊어
//   버렸다. 연결이 맺어지는 자리일수록 꼬박꼬박 걸렸다. 결판은 DISCONNECTED·STOP 둘뿐이다.
//
// 끊긴 까닭(reason)도 받아 둔다 — AUTH_FAIL(비밀번호) 과 NO_AP_FOUND(채널·5GHz) 과
// HANDSHAKE_TIMEOUT(멀다) 은 손볼 자리가 저마다 달라, 그냥 "못 붙었다" 로는 모른다.
static void onWifiEvent(WiFiEvent_t ev, WiFiEventInfo_t info) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiLastReason = info.wifi_sta_disconnected.reason;
      wifiStaBusy = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      wifiStaBusy = false;
      break;
    default: break;
  }
}

static void wifiRest() {                 // 한 바퀴 헛걸음 — 쉬러 간다
  wifiPhase   = WP_REST;
  wifiPhaseAt = millis();
}

// 라디오를 껐다 켠다 — IDF 가 connecting 에서 끝내 나오지 못할 때의 마지막 수단.
// 드라이버를 내리면 ESP-NOW 도 함께 내려가므로 내렸다가 다시 세운다.
static void wifiRadioReset() {
  wifiRadioResets++;
  Serial.printf("[와이파이] 라디오가 엉켰다 — 껐다 켠다 (%d/%d)\n",
                wifiRadioResets, WIFI_RADIO_RESET_MAX);
  if (wifiRadioResets > WIFI_RADIO_RESET_MAX && millis() > WIFI_REBOOT_AFTER_MS) {
    Serial.println("[와이파이] 그래도 안 풀린다 — 보드를 다시 세운다");
    Serial.flush();
    ESP.restart();
  }
  WiFi.scanDelete();
  espnowEnd();                     // 드라이버가 내려가기 전에 가지런히
  WiFi.disconnect(true, false);    // true = 드라이버까지 내린다
  WiFi.mode(WIFI_OFF);
  delay(200);                      // 내려가는 틈(delay 는 쉬는 것이라 그동안 일꾼이 돈다)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  espnowBegin();                   // 드라이버가 새로 올라왔으니 라디오도 다시
  wifiStaBusy = false;
  wifiRejects = 0;
  wifiRest();
}

// ── 설정 모드 ──────────────────────────────────────────────────────
// 여기에 끼운 까닭: 설정 모드는 **위에 있는 것들을 그대로 쓴다** — 화면 그리는 손
// (drawStatus·drawCenteredUTF8·blitFrame·drawIdle), 끊긴 까닭(wifiLastReason·wifiStaBusy),
// 그리고 creds.h 의 목록이다. 그래서 그것들이 다 정해진 뒤, wifiService 바로 앞에 온다
// (아래 loop 가 둘 중 하나만 돌린다 — 둘이 함께 돌면 라디오를 다툰다).
#include "portal.h"

static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) {      // 붙었다 — 셈을 모두 처음으로
    wifiStaBusy     = false;
    wifiRejects     = 0;
    wifiRadioResets = 0;
    wifiNoApRounds  = 0;
    wifiEverOnline  = true;
    if (wifiPick >= 0) wifiBad[wifiPick] = 0;
    wifiRestMs      = WIFI_REST_MIN_MS;
    wifiPhase       = WP_REST;
    wifiPhaseAt     = millis();
    return;
  }

  switch (wifiPhase) {
    case WP_REST:
      if (wifiPhaseAt && millis() - wifiPhaseAt < wifiRestMs) return;
      // 이번 쉬는 시간은 다 썼다 — 다음 헛걸음은 더 길게 쉰다(최대 WIFI_REST_MAX_MS)
      wifiRestMs = (wifiRestMs * 2 > WIFI_REST_MAX_MS) ? WIFI_REST_MAX_MS : wifiRestMs * 2;
      WiFi.scanDelete();
      if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) { wifiRest(); return; }
      wifiPhase   = WP_SCAN;
      wifiPhaseAt = millis();
      return;

    case WP_SCAN: {
      const int16_t n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        if (millis() - wifiPhaseAt >= WIFI_SCAN_MAX_MS) { WiFi.scanDelete(); wifiRest(); }
        return;                                   // 아직 둘러보는 중
      }
      if (n < 0) { WiFi.scanDelete(); wifiRest(); return; }

      // 눈에 보이는 것 중에서 목록에서 가장 앞선(= 순위가 높은) 것을 고른다.
      // 이어서 여러 번 실패한 자리는 건너뛴다(머리말의 wifiBad). 보이기는 하는데
      // 모두 건너뛸 자리면 셈을 지우고 한 번 더 돈다.
      bool sawKnown = false;
      for (int pass = 0; pass < 2 && !sawKnown; pass++) {
        wifiPick = -1;
        for (int i = 0; i < wifiApCount; i++) {
          bool visible = false;
          for (int16_t j = 0; j < n; j++)
            if (!strcmp(WiFi.SSID(j).c_str(), wifiAps[i].ssid)) { visible = true; break; }
          if (!visible) continue;
          sawKnown = true;
          if (wifiBad[i] >= WIFI_BAD_MAX) continue;      // 여기는 잠깐 건너뛴다
          wifiPick = i;
          break;
        }
        if (sawKnown && wifiPick < 0) {                  // 보이는 곳이 모두 건너뛸 자리였다
          Serial.println("[와이파이] 아는 곳이 다 말썽이었다 — 셈을 지우고 다시 돈다");
          memset(wifiBad, 0, sizeof(wifiBad));
          sawKnown = false;                              // 한 바퀴 더 돌려 다시 고른다
        }
      }

      if (wifiPick < 0) {
        wifiNoApRounds++;
        Serial.printf("[와이파이] %d곳이 보이는데 아는 곳은 없다 — %lu초 뒤에 다시 둘러본다 (%d바퀴)\n",
                      (int)n, (unsigned long)(wifiRestMs / 1000), wifiNoApRounds);
        WiFi.scanDelete();
        wifiRest();
        return;
      }
      Serial.printf("[와이파이] %d순위 '%s' 이 보인다 — 붙어 본다\n",
                    wifiPick + 1, wifiAps[wifiPick].ssid);
      WiFi.scanDelete();
      WiFi.disconnect();                          // connecting 을 비우고 틈을 둔다
      wifiPhase   = WP_SETTLE;
      wifiPhaseAt = millis();
      return;
    }

    case WP_SETTLE:
      if (millis() - wifiPhaseAt < WIFI_SETTLE_MS) return;
      if (WiFi.begin(wifiAps[wifiPick].ssid, wifiAps[wifiPick].pass) == WL_CONNECT_FAILED) {
        // 아직 connecting 에서 안 빠졌다 — 틈을 더 두고 다시. 거부가 거듭되면 라디오를 껐다 켠다.
        if (++wifiRejects >= WIFI_REJECT_MAX) { wifiRadioReset(); return; }
        WiFi.disconnect();
        wifiPhaseAt = millis();
        return;
      }
      wifiRejects    = 0;
      wifiLastReason = 0;            // 이번 붙어 보기의 까닭만 본다
      wifiStaBusy    = true;
      wifiPhase      = WP_CONNECT;
      wifiPhaseAt = millis();
      return;

    case WP_CONNECT:
      // 결판이 났거나(붙지 못함) 오래 걸리면 접는다. 붙었으면 맨 위에서 걸러진다.
      if (wifiStaBusy && millis() - wifiPhaseAt < WIFI_TRY_MS) return;
      if (wifiLastReason)
        Serial.printf("[와이파이] %d순위 '%s' 에 못 붙었다 — %s (%u)\n",
                      wifiPick + 1, wifiAps[wifiPick].ssid,
                      WiFi.disconnectReasonName((wifi_err_reason_t)wifiLastReason),
                      (unsigned)wifiLastReason);
      else
        Serial.printf("[와이파이] %d순위 '%s' 에 못 붙었다 — %lu초를 기다렸는데 아무 말이 없다\n",
                      wifiPick + 1, wifiAps[wifiPick].ssid,
                      (unsigned long)(WIFI_TRY_MS / 1000));
      if (wifiPick >= 0 && wifiBad[wifiPick] < 255) wifiBad[wifiPick]++;
      WiFi.disconnect();
      wifiRest();
      return;
  }
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
  hubStart();                    // 서버에 붙어 변화를 받아 온다(client 쪽)
  Serial.printf("[WS] 서버 열림 — ws://%s.local:%d/  (또는 ws://%s:%d/)\n",
                MDNS_HOSTNAME, WS_PORT, WiFi.localIP().toString().c_str(), WS_PORT);
  serversUp = true;
}

// ── 터치 (v1.0 과 동일) ─────────────────────────────────────────────
static void touchBegin() {
  bus->pinMode(1, OUTPUT);       // 확장칩 1번 = 터치 리셋(TP_RES)
  bus->digitalWrite(1, 0);
  delay(30);
  bus->digitalWrite(1, 1);
  delay(200);

  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0xFE);              // 자동 잠들기 끄기
  Wire.write(0x01);
  Serial.printf("터치 칩 %s\n", Wire.endTransmission() == 0 ? "준비됨" : "응답 없음");
}

// 손가락이 닿아 있으면 true. 좌표는 쓰지 않는다(화면 어디를 눌러도 된다).
static bool touching() {
  static uint32_t readErrorAt = 0;
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);              // 손가락 수부터 읽는다
  if (Wire.endTransmission() != 0) return false;
  const size_t received = Wire.requestFrom(TOUCH_ADDR, 5);
  if (received != 5) {
    if (!readErrorAt || millis() - readErrorAt >= 5000) {
      Serial.printf("[I2C] 터치(0x%02X) 읽기 실패: %u/5바이트\n",
                    TOUCH_ADDR, (unsigned)received);
      readErrorAt = millis();
    }
    return false;
  }

  const uint8_t fingers = Wire.read();
  for (int i = 0; i < 4; i++) Wire.read();   // X·Y 네 바이트는 버린다
  return fingers == 1;
}

// 켤 때 화면을 누른 채로 두고 있나 — 설정 모드로 들어가는 길이다(config.h 의 PORTAL_BOOT_HOLD_MS).
// 평소 터치(짧게 = 다음 사진, 길게 = 사진 동기화)와 겹치지 않게 부팅 때로 뺀 것이다.
// **끝까지 누르고 있어야** 한다 — 켤 때 손이 스친 것으로 설정 모드에 들어가지 않게.
static bool portalBootHold() {
#if PORTAL_ENABLE
  if (!PORTAL_BOOT_HOLD_MS || !touching()) return false;
  drawStatus("그대로 누르고 계세요");
  const uint32_t until = millis() + PORTAL_BOOT_HOLD_MS;
  while (millis() < until) {
    if (!touching()) { Serial.println("[설정] 손을 뗐다 — 평소대로 켠다"); drawIdle(); return false; }
    delay(20);
  }
  return true;
#else
  return false;
#endif
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // 이 보드의 Serial 은 UART 가 아니라 USB(CDC)다. 시리얼 모니터를 닫으면 PC 가 글자를
  // 걷어 가지 않는데, 케이블은 그대로 꽂혀 있어 코어는 "아직 붙어 있다" 로 보고 보낼 자리가
  // 날 때까지 기다린다 — 한 번 찍을 때마다 최대 2초(100ms × 20번, HWCDC.cpp write)를 선다.
  // 조회·출석·설정 같은 서버 호출 길목마다 Serial.printf 가 있어, 모니터를 닫으면
  // 그 기다림이 그대로 얹혀 느려진다(열어 두면 바로 걷어 가니 빠르다).
  // 기다리지 말고 자리가 없으면 그 줄을 버리게 한다 — 모니터를 열어 두면 그대로 다 보인다.
  Serial.setTxTimeoutMs(0);
#endif
  Serial.println("\n=== nfcProject — WebSocket 디스플레이 ===");

#if LINK_I2C_ENABLE
  // 리더는 이 버스(터치·확장칩과 같은 버스)에 슬레이브로 붙는다. 결과 한 덩이가
  // 기본 128 바이트를 넘길 수 있어 버퍼를 늘려 둔다 — 잘리면 JSON 이 깨진다.
  Wire.setBufferSize(256);
#endif
  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000)) {
    Serial.println("[I2C] 버스 초기화 실패 — SDA/SCL 핀을 확인하세요");
    return;
  }
#if LINK_I2C_ENABLE
  Serial.printf("[선] I2C 마스터 — 리더 0x%02X 를 %dms 마다 물어본다\n", LINK_I2C_ADDR, LINK_POLL_MS);
#endif

  // 1~3) 확장칩 → RGB 패널 → 초기화 표. begin() 한 번이 다 한다.
  if (!gfx->begin()) {
    Serial.println("화면 시작 실패 — PSRAM(OPI) 설정을 확인하세요");
    return;
  }

  frame = (uint16_t *)ps_malloc(480 * 480 * sizeof(uint16_t));
  Serial.printf("합쳐 그리기 버퍼 %s\n", frame ? "준비됨(PSRAM 460KB)" : "못 잡음 — 아이콘/띠가 깜빡일 수 있다");

  photoBuf = (uint16_t *)ps_malloc(480 * 480 * sizeof(uint16_t));   // SD 사진을 읽어 둘 자리
  Serial.printf("사진 버퍼 %s\n", photoBuf ? "준비됨(PSRAM 460KB)" : "못 잡음 — SD 사진을 못 쓸 수 있다");

  sdReady = sdBegin();           // microSD (config.h 의 SD_* 핀). 없으면 내장 사진만 쓴다
  listSD();                      // SD 에 들어 있는 사진 목록을 시리얼에 찍는다(확인용)
  scanPhotos();                  // 터치로 넘겨 볼 사진 목록

  drawIdle();                    // 기본 화면 — 활성 그림, 아이콘 없음(아직 안 붙었다)

  // 서버를 기다리는 동안 테두리 고리를 돌릴 일꾼. loop 와 같은 심(1)에 같은 순위로 둔다 —
  // loop 가 소켓을 기다리며 쉬는 틈에만 돌아, 평소 화면 그리기를 밀어내지 않는다.
  xTaskCreatePinnedToCore(spinnerTask, "spinner", 4096, nullptr, 1, nullptr, 1);

  // 4) 백라이트를 마지막에 켠다 — 먼저 켜면 그리는 동안 빈 화면이 번쩍인다.
  pinMode(PIN_BL, OUTPUT);
  backlightOn(cfgBacklight);     // 서버에 붙기 전까지는 펌웨어 기본값(BL_LEVEL)
  Serial.printf("밝기 %d/16 단계 (서버 설정을 받으면 바뀐다)\n", cfgBacklight);

  touchBegin();                  // 화면을 켠 뒤에 — 확장칩(bus)이 begin() 에서 준비된다

  credsLoad();                   // 아는 와이파이 목록 — 폰으로 넣은 것(NVS)이 앞, 구울 때 넣은 것이 뒤

  // 와이파이 설정을 플래시(NVS)에 저장하지 않는다 — **화면 밀림의 주범**이다.
  // WiFi.begin() 은 기본값으로 SSID·비밀번호를 NVS 에 적는데, 플래시에 쓰는 동안에는
  // 캐시가 잠깐 꺼진다. 이 칩의 RGB 패널 갱신 인터럽트는 IRAM 에 없어서(코어 기본 설정)
  // 그 사이 중계 버퍼를 못 채우고, 패널로 가는 그림이 한 줄씩 밀린다.
  // 공유기를 못 찾아 네 곳을 번갈아 걸 때마다 쓰기가 나므로 그때 자주 밀린다.
  // 설정은 어차피 config.h 에 있어 저장할 이유가 없다.
  WiFi.persistent(false);
  // 붙어 보기의 결판(DISCONNECTED/STOP)과 끊긴 까닭을 여기서 받는다 — wifiService 는
  // 그 신호가 온 뒤에만 다음 begin() 을 건다(까닭은 wifiService 머리말에).
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // 서버라 잠들지 않게 — 명령을 놓치지 않는다
  espnowBegin();                 // 라디오가 올라온 뒤에. 공유기에 붙기 전에도 리더와는 말이 된다

  // 켤 때 화면을 누르고 있으면 설정 모드로 — 폰으로 와이파이를 적어 넣는 길이다(portal.h).
  if (portalBootHold()) portalStart("켤 때 화면을 누르고 있었다");
  else                  wifiService();     // 붙어 보기 시작(기다리지 않는다)
}

void loop() {
  // ── 설정 모드일 때 ─────────────────────────────────────────────
  // 그동안에는 와이파이·태깅 일을 모두 쉰다 — 라디오를 저쪽(AP·DNS·웹)이 쓰고,
  // 공유기에 붙어 있지 않아 서버에 물을 수도 없다. 둘을 함께 돌리면 wifiService 가
  // 채널을 옮기고 끊어 버려 폰이 설정 창을 쓰는 중에 떨어진다.
  // 시리얼 명령은 세 글자만 받는다 — 뜻은 평소와 같다(아래 switch 와 짝을 맞춘 것이다):
  // 'r' 다시 세우기 · 'w' 설정 모드 걷기 · 'W' 폰으로 넣은 와이파이 지우기.
  if (portalActive()) {
    portalService();
    while (Serial.available()) {
      const int c = Serial.read();
      if (c == 'r' || c == 'R') { Serial.println("[명령] 재부팅합니다..."); Serial.flush(); ESP.restart(); }
      else if (c == 'w')        portalStop();
      else if (c == 'W')        credsClear();
    }
    delay(5);
    return;
  }

  linkService();                 // 선으로 온 명령 먼저 — 공유기를 안 거쳐 가장 빠르다
  espnowService();               // 선이 없을 때 리더가 라디오로 보낸 것

  // 와이파이 — 붙거나 끊기는 순간에만 다시 그린다(매번 그리면 화면이 계속 쓸린다)
  wifiService();
  const bool nowOnline = (WiFi.status() == WL_CONNECTED);
  if (nowOnline != online) {
    online = nowOnline;
    if (online) {
      // 채널을 함께 찍는다 — 리더가 ESP-NOW 로 찾아와야 하는 바로 그 채널이다.
      // 리더 쪽 `status` 가 말하는 채널과 다르면 아직 못 찾은 것이고, 공유기가 채널을
      // 옮기면 이 줄의 숫자도 바뀐다(그때 리더가 다시 찾아 나선다).
      uint8_t ch = 0; wifi_second_chan_t s2;
      esp_wifi_get_channel(&ch, &s2);
      Serial.printf("[와이파이] 연결됨 %s (%s) · 채널 %u\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), ch);
      startServers();            // 붙은 뒤에 mDNS·WebSocket 을 켠다
      fetchConfig();             // 밝기·출석모드를 받아 온다(못 받으면 들고 있던 값으로 돈다)
      if (!bootSyncDone) {       // 부팅 후 처음 붙었을 때 서버에서 사진을 모두 받아 SD 에 저장
        bootSyncDone = true;
        syncPhotos();
      }
    } else {
      Serial.println("[와이파이] 끊김");
#if SOUND_SAY_WIFI
      // 한 번만 — 다시 붙기는 혼자 되풀이하지만(wifiService) 말은 끊긴 그 순간에 한 마디다.
      // 이 자리에 둔 까닭: 와이파이가 없으면 조회가 안 되는데, 그걸 모르는 자원봉사자는
      // 카드를 계속 대 본다. 부팅할 때는 말하지 않는다 — 그때는 리더가 아직 선 너머에
      // 살아 있지 않아 어차피 못 가고, 리더가 제 '리더 준비 중' 을 이미 읽는다.
      readerSay("와이파이 붙는 중");
#endif
    }
    if (mode == MODE_IDLE) drawIdle();   // 대기 화면일 때만 아이콘을 갱신(NFC 화면은 건드리지 않는다)
    sendState();
  }

  // 아는 와이파이가 아예 안 보이면 설정 모드로 — 새 교회에 가져다 켠 자리다.
  // **한 번이라도 붙었던 뒤에는 들어가지 않는다**: 공유기를 잠깐 껐을 때 기기가
  // 설정 모드로 달아나면 안 된다(돌아오면 wifiService 가 혼자 다시 붙는다).
#if PORTAL_ENABLE
  if (PORTAL_AUTO_ROUNDS && !wifiEverOnline && wifiNoApRounds >= PORTAL_AUTO_ROUNDS) {
    portalStart("아는 와이파이가 보이지 않는다");
    return;
  }
#endif

  if (serversUp) webSocket.loop();   // WebSocket 을 굴린다(받은 것을 처리)
  if (hubBegun) hub.loop();          // 서버와의 연결도 함께(다시 붙기·heartbeat 가 여기서 돈다)

  // 리더가 넘긴 태그 — 조회·출석 지급은 수백 ms 가 걸려 WebSocket 콜백 밖(여기)에서 한다.
  if (pendingUid[0]) {
    char uid[32];
    strncpy(uid, pendingUid, sizeof(uid) - 1); uid[sizeof(uid) - 1] = '\0';
    pendingUid[0] = '\0';            // 처리 중에 다음 태그가 들어와도 덮이지 않게 먼저 비운다
    processTag(uid);
  }

  // 설정 다시 받기 — 관리자 화면에서 밝기·출석모드를 바꾸면 이만큼 뒤에 반영된다
  if (online && (cfgWanted || (int32_t)(millis() - cfgNextAt) >= 0)) {
    cfgWanted = false;                   // 관리 화면이 민 것이든 주기든, 받는 길은 하나다
    fetchConfig();
  }

  // 들고 있는 연결을 살려 둔다 — **대기 화면일 때만**. 태깅을 처리하는 중에 끼어들면
  // 아이 앞에서 왕복이 하나 늘어난다. 조용한 사이에 미리 찔러 두는 것이 요지다.
  if (online && mode == MODE_IDLE && (int32_t)(millis() - httpWarmAt) >= 0) httpWarm();

  // client 연결 상태 점 — client 가 붙거나 떨어져 색이 바뀔 때만 다시 그린다(화면 갱신 없이도)
  if (linkState() != shownLink) drawLinkDot();

  // 시리얼 명령 — 한 글자씩(줄바꿈은 무시). 화면 넘기기는 터치와 똑같이 동작한다.
  while (Serial.available()) {
    const int c = Serial.read();
    switch (c) {
      case 'n': case 'N': Serial.println("[명령] 다음 사진"); if (mode == MODE_NFC || mode == MODE_BULK) goIdle(); else nextPhoto(); break;
      case 'p': case 'P': Serial.println("[명령] 이전 사진"); prevPhoto(); break;
      case 'h': case 'H': Serial.println("[명령] 활성 화면"); homePhoto(); break;
      case 's': case 'S': listPhotos(); break;
      case 'l': case 'L': listSD(); break;
      case 'c': case 'C':
        Serial.println("[명령] 설정 다시 받기");
        // 손으로 부른 것만 읽어 준다 — 주기(CONFIG_TTL_MS)마다 읽으면 5분에 한 번씩
        // 혼잣말하는 상자가 된다. 화면을 길게 누르는 쪽(사진 동기화)은 조용하다:
        // 그 일에 맞는 토막이 없다(필요해지면 /01/014 에 얹고 여기처럼 한 줄 부르면 된다).
        readerSay("설정 다시 받는 중");
        fetchConfig();
        break;
      case 'v': case 'V':                          // 스피커·SD 가 살아 있나(리더에서 소리가 난다)
        Serial.println("[명령] 리더에게 한 마디 — '카드를 올려 주세요'");
        readerSay("카드를 올려 주세요");
        break;
      case 'i': case 'I':
        Serial.printf("[상태] 기기 %s · 밝기 %d/16 · 출석모드 %s · 출석 카드 %s · 설정 %s\n",
                      TALENT_DEVICE_ID, cfgBacklight, cfgAttendMode ? "켜짐" : "꺼짐",
                      cfgAttendCard[0] ? cfgAttendCard : "없음",
                      cfgGotOnce ? "받음" : "못 받음(기본값으로 동작)");
        Serial.printf("[상태] 서버 연결 %s\n", TALENT_HUB_ENABLE
                      ? (hubUp ? "붙어 있음" : (hubBegun ? "끊김(다시 붙는 중)" : "아직 안 켬"))
                      : "꺼 둠");
        Serial.printf("[상태] 선 %s · ws client %u곳 · 와이파이 %s\n",
                      LINK_I2C_ENABLE ? (linkAlive() ? "리더가 살아 있음" : "조용함") : "꺼 둠",
                      serversUp ? webSocket.connectedClients() : 0,
                      online ? WiFi.localIP().toString().c_str() : "끊김");
        break;
      case 't': case 'T': showTestScreen(); break;
      case 'g': case 'G':                          // 고리가 도는지 눈으로 보기(서버 없이)
        Serial.println("[명령] 불러오는 중 고리 3초");
        spinStart(); delay(3000); spinStop();      // delay 는 쉬는 것이라 그동안 일꾼이 돈다
        break;
      case 'w':           portalStart("시리얼 명령"); return;      // 설정 모드 — 이 바퀴는 여기서 접는다
      case 'W':           credsClear(); Serial.println("[명령] 다시 세우면 반영됩니다"); break;
      case 'r': case 'R': Serial.println("[명령] 재부팅합니다..."); Serial.flush(); ESP.restart(); break;
      case '?':           Serial.println("[명령] n 다음 · p 이전 · h 활성 · s 사진 목록 · l SD 목록 · c 설정 받기 · i 상태 · t 시험 화면 · v 리더 소리 · g 고리 · w 와이파이 설정 모드 · W 폰으로 넣은 와이파이 지우기 · r 재부팅"); break;
      default: break;
    }
  }

  // NFC·일괄 화면은 얼마 뒤 자동으로 대기 화면으로 돌아간다
  if ((mode == MODE_NFC || mode == MODE_BULK) && (int32_t)(millis() - nfcUntil) >= 0) goIdle();

  // 터치 — 짧게: (NFC 화면이면 대기로 / 대기 화면이면 다음 사진),  길게: 서버에서 사진 동기화
  const bool down = touching();
  if (down && !wasDown) { downAt = millis(); longFired = false; }     // 누르기 시작
  if (down && !longFired && (millis() - downAt) >= LONG_PRESS_MS) {
    longFired = true;                                                 // 누르는 동안 한 번만
    syncPhotos();
  }
  if (!down && wasDown && !longFired) {                               // 짧게 눌렀다 뗌
    Serial.println("[터치] 짧게 누름");
    if (mode == MODE_NFC || mode == MODE_BULK) goIdle();
    else                                       nextPhoto();
  }
  wasDown = down;

  delay(5);                      // WebSocket 반응을 위해 v1.0(20ms)보다 촘촘히 돈다
}
