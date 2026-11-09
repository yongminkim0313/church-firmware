#pragma once
// simple_v1 설정 — 이 파일만 고치면 된다(코드는 건드리지 않는다).
// 보드: Guition JC3248W535 (ESP32-S3 · AXS15231B QSPI 320×480 · 정전식 터치 · TF 슬롯 · I2S 앰프)
// funfunFirmware_v3 의 두 보드(화면 T-RGB + 리더 DevKitC)가 하던 일을 이 보드 한 장이 다 한다 —
// 서버·그림은 v3 화면(display)의 값을, 카드·소리는 v3 리더(reader)의 값을 가져와 이 보드의 핀으로 바꿨다.

// ── 인증정보는 여기 없다 ──────────────────────────────────────────
// 와이파이(SSID·비밀번호 네 벌)와 펀펀포인트 기기 키는 세 프로젝트가 함께 보는
// secrets/ChurchSecrets.h 에 있다. **그 파일은 깃에 올라가지 않는다**(.gitignore) —
// 기기 키가 새면 그 교회의 포인트를 올릴 수 있어서다.
//
// 받아 간 뒤 한 번:  cp secrets/ChurchSecrets.example.h secrets/ChurchSecrets.h
// 들어오는 길은 platformio.ini 의 build_flags = -I ... 다(없으면 여기서 멈춘다).
#include <ChurchSecrets.h>

// 다른 교회(시험용 등)의 기기 키로 잠깐 굽고 싶을 때 — **깃에 적지 않고** 빌드할 때만 넣는다:
//   PLATFORMIO_BUILD_FLAGS='-DTALENT_DEVICE_KEY_OVERRIDE=\"<키>\"' pio run -t upload
// 넣지 않으면 위 공용 파일의 키 그대로다. 그 교회의 명단·잔액·설정이 통째로 그쪽 것이 된다.
// ── 이 기기가 붙는 교회 ──────────────────────────────────────────
// 2026-11-07: **주주꿈교회**(운영 · jesusdream)에 붙인다 — 실제 아이들의 명단·잔액·출석이다.
//   (v2 에서 가져올 때는 테스트교회1 이었다. 시험으로 돌리려면 아래 두 이름을 TALENT_KEY_TESTCHURCH1 로.)
// 키 값은 여기 없다 — ChurchSecrets.h 의 TALENT_KEY_JESUSDREAM 이라는 **이름**만 가리킨다.
#ifndef TALENT_DEVICE_KEY_OVERRIDE
#ifndef TALENT_KEY_JESUSDREAM
#error "ChurchSecrets.h 에 TALENT_KEY_JESUSDREAM 이 없다 — 받아 적거나, 이 블록을 지워 공용 키로 굽는다"
#endif
#define TALENT_DEVICE_KEY_OVERRIDE TALENT_KEY_JESUSDREAM
#endif

#ifdef TALENT_DEVICE_KEY_OVERRIDE
#undef TALENT_DEVICE_KEY
#define TALENT_DEVICE_KEY TALENT_DEVICE_KEY_OVERRIDE
#endif

// ── mDNS ──────────────────────────────────────────────────────────
// 붙은 뒤 http/ws 로 이 이름으로 찾을 수 있다: lcdtest.local
// ★ 운영 중인 nfc-display 와 **다른 이름**을 쓴다 — 같은 망에서 이름이 겹치면
//   어느 기기가 잡히는지 들쭉날쭉해진다.
#define MDNS_HOSTNAME "funfun-s1"

// ── WebSocket 서버 포트 ───────────────────────────────────────────
// 외부(브라우저·폰·리더)가 여기에 붙어 화면 상태를 보낸다: ws://lcdtest.local:81/
#define WS_PORT 81

// ── 펀펀포인트 서버 ───────────────────────────────────────────────
//   운영: "https://funfun.youthvision.co.kr"
//   개발: "http://192.168.0.10:8200"  (자기 PC IP)
// ★ 서버를 바꾸면 보는 자료(잔액·명단)가 통째로 바뀐다 — RTDB 가 서버마다 갈려 있다.
//   저쪽에만 있는 아이는 그 자리에서 '등록되지 않은 키링' 이 된다.
#define TALENT_SERVER "https://funfun.youthvision.co.kr"

// 이 기기의 이름. 관리자 화면 '리더 설정 → v2.0' 에서 이 ID 로 기기별 설정을 덮어쓸 수 있고,
// 포인트 내역에도 어느 기기에서 올라온 것인지 이 이름으로 남는다.
// 영문·숫자·_·- 만 쓴다(서버가 그 밖의 글자를 지운다).
// ★ 운영 기기(nfc-display)와 다른 이름을 쓴다 — 설정과 내역이 섞이지 않게.
#define TALENT_DEVICE_ID "fun-s1"  // v2(fun-2) · v3(fun-3) 기기와 함께 켜도 관리 화면에서 겹치지 않게

// ── 서버에 붙어 변화 받기 ─────────────────────────────────────────
// 이 보드가 서버에 **client 로 붙어 있는다**. 관리 화면에서 포인트를 조정하거나
// 앱·다른 리더에서 변화가 생기면 서버가 그 아이를 이 화면에 띄우라고 밀어 준다.
// 주소는 따로 적지 않는다 — TALENT_SERVER 에서 호스트·포트·ssl 을 떼어 쓴다.
// 0 으로 두면 전처럼 혼자 돈다(조회·지급은 그대로 — 밀어 주는 것만 안 온다).
#define TALENT_HUB_ENABLE 1
#define TALENT_HUB_PATH "/ws/device"
#define HUB_RETRY_MS 5000UL

// ── 설정 다시 받기 ────────────────────────────────────────────────
// 관리자 화면에서 밝기·출석모드를 바꾸면 이 주기로 기기에 반영된다.
// 와이파이에 붙는 순간에도 한 번 받는다. 시리얼 'c' 로 즉시 받을 수 있다.
#define CONFIG_TTL_MS 300000UL  // 5분

// ── 출석모드 ──────────────────────────────────────────────────────
// 같은 키링이 이 시간 안에 또 오면 출석으로 세지 않는다(화면에는 "이미 출석했어요").
// 기기에 시계(RTC)가 없어 "하루 한 번" 을 알 수 없다 — 시간 간격으로 막는다.
#define ATTEND_COOLDOWN_MS 600000UL  // 10분
#define ATTEND_RECENT_MAX 32         // 최근 출석한 키링을 이만큼 기억한다

// ── 밝기 기본값 ───────────────────────────────────────────────────
// 서버에 못 붙었을 때 쓰는 값(1~16). 붙으면 관리 화면(리더 설정 → v2.0)의 밝기가 이긴다.
#define BL_LEVEL 16

// ── 밝기를 서버 설정으로 바꾸나 ───────────────────────────────────
// 이 보드의 백라이트는 GPIO1 하나다 — LEDC PWM 으로 서버의 1~16 단계를 그대로 밝기로 옮긴다
// (main.cpp 의 backlightOn). 0 이면 켤 때 BL_LEVEL 로 한 번 켜고 서버 값은 받지 않는다.
#define TFT_BLK_ENABLE 1

// ── 메모리 기록 ───────────────────────────────────────────────────
// 이 주기마다 시리얼에 메모리 한 줄을 찍는다 — 내부 RAM · DMA · PSRAM 의 남은 양·켠 뒤 최저·
// 가장 큰 덩이, 그리고 일꾼 스택 여유. 시리얼 'm' 은 주기와 상관없이 전부(태스크마다)를 찍는다.
// 0 으로 두면 주기 기록만 끈다('m' 은 그대로).
// ★ 내부 RAM 의 '최저' 를 본다 — TLS 핸드셰이크가 잠깐 40KB 쯤을 더 먹는다. 남은 양이 넉넉해도
//   최저가 바닥에 닿으면 그때 https 가 끊긴다(mbedTLS 는 내부 RAM 만 쓴다).
#define MEM_LOG_MS 10000UL

// ── PN532 (카드) — 이 보드에 직접 꽂는다 ─────────────────────────
// P3 커넥터(JST 1.25mm 4핀: GND · 3V3 · IO17 · IO18)에 PN532 를 **UART(HSU)** 로 꽂는다(nfc.h 머리말).
//   3V3 → VCC · GND → GND · IO17 → PN532 RXD · IO18 → PN532 TXD   ★ 송수신은 엇갈려 · 딥스위치는 HSU
// USB 옆 P1(5V · IO43 · IO44 · GND)에 꽂아도 켤 때 찾아낸다.
// v3 는 리더 보드가 읽어 선·라디오로 넘겼다 — 이 판은 nfcTask(코어 0)가 직접 읽어 본 고리에 넘긴다(v2 와 같다).
#define NFC_TX_PIN 17      // ESP 송신 → PN532 RXD
#define NFC_RX_PIN 18      // ESP 수신 ← PN532 TXD
#define NFC_BAUD 115200    // PN532 HSU 기본 속도
#define NFC_RETRIES 0x01   // 카드 찾기 한 번에 몇 번 훑나 — 적을수록 카드가 없을 때 빨리 돌아온다
#define NFC_SCAN_TIMEOUT_MS 60  // 한 번 찾기의 응답을 기다리는 시간(v2 의 SCAN_TIMEOUT_MS)

// ── 같은 카드를 두 번 세지 않기 (v2 와 같다) ──────────────────────
// NFC_GONE_MS: 이만큼 안 보였으면 카드가 리더에서 떠났다고 본다(올려 둔 채로도 한두 번은 못 읽는다).
// NFC_RETAG_MS: 센 뒤 적어도 이만큼은 같은 카드를 다시 세지 않는다(가장자리에 걸친 카드의 깜빡임).
#define NFC_GONE_MS 250
#define NFC_RETAG_MS 1000

// ── 리더(PN532)가 없어도 돌기 ─────────────────────────────────────
// 0(기본) — 켤 때 PN532 를 못 찾으면 빨간 화면으로 선다. 현장 기기는 이래야 고장이 보인다.
// 1 — 서지 않고 이어 간다. 리더를 떼어 놓고 화면·허브 알림만 시험할 때(-DPN532_OPTIONAL=1).
#ifndef PN532_OPTIONAL
#define PN532_OPTIONAL 0
#endif

// ── 버튼 · 터치 ───────────────────────────────────────────────────
// 화면(AXS15231B 터치)을 누르는 것과 보드의 BOOT 버튼이 **같은 일**을 한다:
//   짧게: 대기 중이면 다음 장 · NFC 화면이면 대기로 / 길게(1.2초, 떼면): 설정 다시 받기
//   5초 쥐고 있기: 와이파이 설정 모드
#define BTN_PIN 0  // BOOT 버튼 (누르면 LOW)
#define LONG_PRESS_MS 1200
#define TOUCH_POLL_MS 20    // 터치 칩(I2C 0x3B · SDA 4 · SCL 8 — lib/JC3248W535)을 묻는 간격

// ── 와이파이 설정 모드 (폰으로 와이파이 적어 넣기, 2026-11-06) ───
// nfcProject/master 와 같은 길이다 — 보드가 스스로 와이파이(SoftAP)를 띄우고, 화면의 QR 을
// 폰 카메라로 찍으면 그 AP 에 붙고 설정 창이 저절로 뜬다. 자세한 것은 portal.h 머리말에.
// 받아 적은 것은 **붙어 보고 맞을 때만** 플래시(NVS)에 저장한다.
#define PORTAL_ENABLE 1
#define PORTAL_AP_PREFIX "funfun-setup"  // 띄우는 AP 이름 앞머리(뒤에 MAC 네 자리가 붙는다)
// 그 AP 의 비밀번호. **비밀이 아니다** — 화면과 QR 에 그대로 적혀 나온다. 아무나 붙어
// 와이파이를 바꾸지 못하게 하는 울타리일 뿐이다. WPA2 는 여덟 자 이상. "" = 비밀번호 없는 AP.
#define PORTAL_AP_PASS "funfun1234"
#define PORTAL_CHANNEL 1          // AP 채널
// AP 송신 출력(wifi_power_t 값, 0.25dBm 단위 — 34 = 8.5dBm). 0 = 손대지 않는다(최대 ~20dBm).
// ★ v2 보드(USB-C 둘 · N16R8 호환판)에서 폰이 AP 이름은 보는데 '네트워크에 연결할 수 없음'
//   으로 떨어졌다 — 기기는 붙었다는 이벤트조차 받지 못했다(2026-11-06). 값싼 S3·C3 판에서
//   흔한 일로, 최대 출력에서 안테나 정합이 틀어져 붙는 동안 오가는 프레임이 깨진다.
//   설정 창은 폰을 기기 앞에 두고 쓰는 것이라 8.5dBm 으로도 충분하다.
#define PORTAL_TX_POWER 34
#define PORTAL_IDLE_MS 300000UL   // 폰이 안 붙은 채로 이만큼 지나면 걷는다(5분) — 아는 곳이 없으면 안 걷는다
// 켠 뒤 **한 번도 못 붙었고** 목록을 이만큼 돌아도 못 붙으면 저절로 설정 모드로. 0 = 저절로 안 들어간다.
#define PORTAL_AUTO_ROUNDS 3
// BOOT 버튼을 이만큼 쥐고 있으면 설정 모드로. LONG_PRESS_MS(설정 다시 받기)보다 길어야 한다 —
// 길게 누름은 **손을 뗄 때** 걸리고, 이만큼 계속 쥐고 있으면 이쪽이 걸린다.
// ★ 켤 때 BOOT 를 쥐고 있는 길은 없다 — 그러면 칩이 굽기 모드로 들어간다.
#define PORTAL_HOLD_MS 5000
// 구울 때 넣은 와이파이(ChurchSecrets.h 의 WIFI_SSID_0~3)를 쓰나.
// 0 = 쓰지 않는다 — 폰으로 넣은 것만 안다. 처음 켜면 곧바로 설정 모드가 열려 QR 이 뜬다.
#ifndef WIFI_USE_BUILTIN  // 시험 때 빌드 플래그로 켤 수 있게(-DWIFI_USE_BUILTIN=1)
#define WIFI_USE_BUILTIN 0
#endif
#define WIFI_NVS_NS "nfcwifi2"    // 폰으로 넣은 와이파이를 적는 NVS 칸 — v3·nfcProject/master 와 같은 이름 · 같은 꼴

// ── 사진 ──────────────────────────────────────────────────────────
// 태깅 화면 한 장에 그 아이의 사진을 이름 위에 동그라미로 띄운다.
//   GET /api/talent/roster?px=240&all=1    → UID·학번마다 그림 파일 이름(img)
//   GET /api/talent/photo/device/<img>     → 240 정사각 RGB565(리틀엔디안, 115,200바이트)
// 받으면서 PHOTO_PX(96)로 줄여 PSRAM 에 담는다.
//
// ★ 받는 크기를 240 으로 고른 까닭 — 서버가 만들어 주는 것은 48·208·240·480 뿐이다
//   (talentPhoto.js 의 DEVICE_SIZES). 48 은 너무 작고, **208 은 사진을 올린 아이만** 그림이
//   있다(이름 그림 nm-… 이 48·240·480 에만 있다 — 사진 없는 아이는 빈 동그라미가 된다).
//   240 은 동그라미 마스크에 이름 그림까지 있어 모두에게 그림이 있다. 480 은 받는 데 네 배다.
// ★ 화면 크기 96 — 이름·지급/사용·사유·잔액이 둥근 화면(240 그림)에 함께 들어가는 한계다
//   (main.cpp 의 CARD_* 가 이 크기에 맞춰져 있다). 바꾸면 그 자리들도 함께 옮길 것.
#define TALENT_PX 240   // 서버에서 받는 크기
#define PHOTO_PX 96     // 화면에 띄우는 크기
#define PHOTO_ENABLE 1  // 0 = 사진 없이 글자만

// 이름표(UID→사진 이름)를 들고 있는 칸 수. PSRAM 에 잡는다.
#define MAX_ROSTER 400
// ── 사진 캐시 (PSRAM) ─────────────────────────────────────────────
// 받은 사진을 PSRAM 에 들고 있다가 같은 아이가 또 오면 내려받지 않고 바로 깐다.
// 줄여 담은 한 장 = 96*96*2 = 18,432바이트이므로 200장 = 3,686,400바이트(약 3.5MB).
// 운영 교회가 74명(2026-11-05)이라 전원이 들어가고 반이 남는다. 칸이 차면 가장 오래 안 쓴
// 사진부터 밀어낸다(그 아이는 다음 태깅 때 다시 받는다 — 0.3초쯤).
// 0 으로 두면 캐시를 쓰지 않는다(태깅마다 내려받는다).
//
// ★ 칸은 **필요할 때 한 장씩** 잡는다 — 아이가 열 명인데 3.5MB 를 미리 묶어 두지 않는다.
// ★ 캐시 열쇠는 UID 가 아니라 **사진 파일 이름**이다. 이름에 사진 해시가 들어 있어
//   (ph-<해시>-240.565) 아이가 사진을 바꾸면 이름이 달라진다 — 옛 사진을 내보낼 길이 없다.
//   서버 쪽 주석의 "있다 = 최신" 과 같은 원리다.
#define PHOTO_CACHE_MAX 200

// 이름표를 다시 받는 주기. 아이가 사진을 바꾸면 img 이름이 달라지므로 이만큼 뒤에 반영된다.
// (서버가 {"state":"roster"} 를 밀어 주면 주기를 기다리지 않고 다음 대기 때 받는다.)
#define ROSTER_TTL_MS 600000UL  // 10분

// 홈 화면 숫자(등록 인원 · 총 포인트 · 오늘 +/-)를 다시 받는 주기. 포인트가 바뀌면
// (태깅 · 허브 알림) 주기를 기다리지 않고 대기 화면으로 돌아오자마자 받는다.
#define SUMMARY_TTL_MS 60000UL  // 1분

// ── 대기 화면 네 장 (pages.h) ─────────────────────────────────────
// 홈 → 최근 내역 → 오늘 출석 → 출석 랭킹 → 포인트 랭킹 → 홈 으로 돌아가며 띄운다.
// 받은 것이 없는 장(오늘 아무도 안 왔다 등)은 건너뛴다. BOOT 를 짧게 누르면 다음 장으로.
// 0 으로 두면 예전처럼 홈 한 장만 띄우고 알림판도 받지 않는다.
#define PAGES_ENABLE 1
#define PAGE_HOME_MS 10000UL  // 홈을 띄워 두는 시간
#define PAGE_MS 6000UL        // 나머지 장 하나를 띄워 두는 시간
// 최근 내역 · 두 랭킹이 원 안에 다 안 들면(최근 내역 넷 · 랭킹 다섯 넘게) 천천히 아래로 굴린다 —
// 위에서 HOLD 만큼 멈췄다가 초당 PX_S 픽셀로 끝까지, 끝에서 HOLD 만큼 멈춘 뒤 다음 장.
// 그동안 서버 받기는 쉰다. 열다섯 줄이면 최근 내역은 362px(18초 남짓) · 랭킹은 258px(13초).
#define PAGE_SCROLL_PX_S 20
#define PAGE_SCROLL_HOLD_MS 2500UL
// 알림판(최근 내역 · 오늘 출석 · 순위)을 다시 받는 주기. 포인트가 바뀌면 홈 숫자와 함께
// 주기를 기다리지 않고 받는다.
#define BOARD_TTL_MS 60000UL  // 1분

#define HTTP_TIMEOUT_MS 6000

// ── 소리 — microSD 의 WAV 를 보드의 I2S 앰프로 (v3 리더 · v2 와 같은 코드) ──
// v3 는 리더 보드가 냈다. 이 보드에는 TF 슬롯과 스피커 앰프(NS4168 · SPK 2핀 커넥터)가 붙어 있어
// sound.cpp(v3 리더의 것 그대로)가 직접 낸다 — 이름 소리·고정 토막 판을 **받아 SD 에 쓰는 것도** 다시 된다.
// 0 으로 두면 소리를 통째로 끈다(SD·앰프를 열지 않는다 — 삑도 없다).
#define SOUND_ENABLE 1

// 앰프(NS4168) — 보드에 붙어 있다. SPK 2핀(1.25mm)에 4~8Ω 1~2W 스피커.
// ★ BCLK 와 DIN 은 자료마다 41/42 가 엇갈려 있다 — 소리가 안 나면 둘을 맞바꿔 본다.
//   (heartHunter 가 같은 보드에서 BCLK=42 · WS=2 · DOUT=41 로 앰프를 열었다.)
#define I2S_BCLK_PIN 42
#define I2S_LRC_PIN 2   // WS · LRCLK
#define I2S_DIN_PIN 41  // 앰프로 들어가는 데이터(ESP 쪽 DOUT)

// TF(microSD) 슬롯 — 보드에 붙어 있다. SPI3(HSPI) — 화면 QSPI 가 SPI2 를 쓴다.
// FAT32 로 포맷하고 voices/ 의 01 02 03 을 뿌리에 복사한다(heartHunter 의 사진 폴더와 함께 있어도 된다).
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_SCK_PIN 12
#define SD_MISO_PIN 13
#define SD_SPI_HZ 20000000  // 카드를 못 읽으면 4000000 으로

// 소리 크기 0~30 — 소프트웨어로 줄인다.
// ★ 나중에 서버 설정(talentConfigV2)에 얹을 자리다 — 밝기(backlight)처럼 관리 화면에서
//   만지게 하려면 여기 기본값을 두고 cfg 로 덮으면 된다(sound.h 의 '다음 걸음' 참고).
// 굽기만 다르게 하려면(밤에 작게 시험 등) 빌드 플래그로: PLATFORMIO_BUILD_FLAGS=-DSOUND_VOLUME=3 pio run -t upload
#ifndef SOUND_VOLUME
#define SOUND_VOLUME 22
#endif

// 삑을 말과 함께 낼까 — 삑은 말보다 빨라 아이는 '읽혔다' 를 먼저 안다. 0 이면 말만.
#define SOUND_KEEP_BEEP 1

// 토막과 토막 사이 쉼. 0 으로 두면 숫자가 뭉개져 들린다.
#define SOUND_GAP_MS 60

// 켜질 때 "리더 준비 중 · 와이파이 붙는 중" 을 읽는다 — 상자가 살아 있다는 신호.
#define SOUND_SAY_BOOT 1
// 대기 화면으로 돌아갈 때 "카드를 올려 주세요" 를 읽는다.
// ★ 기본값 0 인 까닭: 태깅이 끝나고 6초 뒤마다 말을 걸어, 줄이 선 현장에서는 끊임없이
//   떠드는 상자가 된다. 조용한 교회에서 한 번씩 부르고 싶으면 1 로.
#define SOUND_SAY_IDLE 0
// 그냥 조회(출석모드가 아닐 때)에서 잔액까지 읽는다.
#define SOUND_SAY_BALANCE 1

// 아이 이름 소리 — 서버(talentVoice.js)가 만든 WAV 를 대기 때 하나씩 받아 SD 의 /09/<키>.wav 에
// 써 둔다(절차: NAME_VOICE.md). 0 이면 이름표에 ?snd=1 을 붙이지 않고 받지도 않는다.
#define NAME_VOICE_ENABLE 1
// SD 에 들고 있을 이름 소리 수(키 목록 — PSRAM 4KB). 명단(MAX_ROSTER)보다 넉넉히 —
// 이름을 바꾼 아이의 옛 소리도 지우기 전까지는 자리를 차지한다.
#define NAME_VOICE_MAX 1024
// 한 개 받을 버퍼(PSRAM). 서버가 4초(176KB)를 넘는 이름은 만들지 않는다(talentVoice MAX_SEC).
#define NAME_VOICE_BUF_MAX (192 * 1024)
// 받다 실패하면(503·끊김) 이만큼 쉬고 다음 이름으로 — 그 이름은 다음 이름표 때 다시 묻는다.
#define NAME_VOICE_BACKOFF_MS 60000UL
// 태깅할 때 안내문 앞에 이름을 읽는다(SD 에 받아 둔 아이만). 0 이면 받기는 그대로 하고 읽지만 않는다.
#define SOUND_SAY_NAME 1
// 이름과 다음 토막 사이 쉼. 서버가 이름 앞뒤 무음을 30ms 만 남기고 자르므로 토막 사이(60ms)와
// 같게 두면 '김하은출석 완료' 처럼 붙어 들릴 수 있다 — 들어 보고 맞춘다.
#define SOUND_NAME_GAP_MS 150
// 고정 토막(/01 · /02 · /03)도 서버의 같은 목소리로 — 서버의 '판'(talentVoicePack.js)이 SD 의
// /voice.ver 와 다르면 대기 때 하나씩 받아 갈아 끼운다(NAME_VOICE.md 5단계). 0 이면 SD 의 것을 그대로.
// 서버에서 이름 소리를 꺼 두었으면(TALENT_VOICE) 판도 없어 아무것도 바꾸지 않는다.
// ★ v3 는 받는 쪽(화면)과 두는 쪽(리더 SD)이 갈라져 이것을 꺼 두었다 — 이 판은 한 보드라 v2 처럼 켠다.
#define VOICE_PACK_ENABLE 1
#define VOICE_PACK_MAX 160                    // 판의 토막 수 상한(지금 128개)
#define VOICE_PACK_CHECK_MS 3600000UL         // 판을 묻는 간격(한 시간) — 목소리는 드물게 바뀐다
#define VOICE_PACK_BUILDING_MS 60000UL        // 서버가 만드는 중(202)이면 이만큼 뒤에 다시

// ── 상태 불 ───────────────────────────────────────────────────────
// 이 보드에는 RGB LED 가 없다. 그리고 데브킷의 LED 핀이던 GPIO48 이 **이 보드에서는 화면 QSPI 의 데이터 선**
// (SDIO1)이다 — 건드리면 화면이 깨진다. 그래서 불 쪽은 통째로 빈 함수다(led.cpp).
#define LED_ENABLE 0
#define LED_WIFI_GRACE_MS 60000UL

// ── 화면 (JC3248W535 · 320×480 세로) ─────────────────────────────
// 그림은 v2·v3 와 같은 **240 원형 캔버스**에 그린다. 그 캔버스는 240×360 판의 가운데에 놓이고
// 위아래 60 줄은 띠로 채운다. 판을 4/3 배(면적 섞기)로 늘리면 320×480 이 꼭 찬다
// (main.cpp 의 PanelJC · frameBands). 그래서 home.h · pages.h · NFC 화면이 v3 와 한 줄도 다르지 않다.
// 고를 값은 없다 — 2026-11-08 에 배율(PANEL_SCALE_*)을 걷었다(1/1 은 위아래를 채울 수 없다).
