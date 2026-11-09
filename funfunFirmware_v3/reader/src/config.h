#pragma once
// funfunFirmware_v3 · 리더(reader) 설정 — 이 파일만 고치면 된다.
// 보드: ESP32-S3-DevKitC-1 N16R8 — v2 의 그 보드에서 **GC9A01 화면만 뺀** 배선이다.
//   PN532(카드) · microSD(소리 토막) · MAX98357A(앰프) + 화면(T-RGB)으로 가는 I2C 선 하나.
// 서버와는 말하지 않는다 — 조회·지급은 화면이 한다(그래서 기기 키·와이파이 비밀번호가 없다).
// 규약은 nfcProject/slave 와 같다 — 이 펌웨어는 그것을 이 보드·이 소리(SD+I2S)로 옮긴 것이다.

// ── 화면으로 가는 두 갈래 길 ──────────────────────────────────────
// 1순위 선(I2C) — 화면(T-RGB) 옆면 Grove 4핀 커넥터에 세 가닥:
//   리더 GPIO4 ─ 화면 GPIO8(SDA) · 리더 GPIO5 ─ 화면 GPIO48(SCL) · GND ─ GND
//   3V3 은 잇지 않는다 — 두 보드가 제 USB 로 먹는다. 풀업은 화면 보드에 있다.
// 이 리더가 그 버스의 **슬레이브(0x30)** 다. 화면이 LINK_POLL_MS(50ms)마다 "줄 것 있나" 하고 읽어 간다.
// ★ 4·5 는 v2 에서 화면(GC9A01)의 CS·DC 였던 자리다 — 화면을 뗀 자리를 그대로 쓴다.
#define LINK_I2C_ENABLE 1
#define PIN_LINK_SDA 4
#define PIN_LINK_SCL 5
#define LINK_I2C_ADDR 0x30   // 화면 config.h 의 LINK_I2C_ADDR 과 같아야 한다. 0x15(터치)·0x20(확장칩)과 안 겹친다
#define LINK_I2C_HZ 400000   // 화면(마스터)의 Wire.begin 과 같은 값
#define LINK_ALIVE_MS 3000   // 이 시간 안에 화면이 말을 걸었으면 선이 살아 있다고 본다
#define LINK_WIFI_FALLBACK 1 // 1 = 선이 죽으면 라디오(ESP-NOW)로 넘어간다 / 0 = 선만 쓴다
#define LINK_GRACE_MS 8000   // 선이 이만큼 조용하면 그제서야 라디오를 켠다

// 2순위 라디오(ESP-NOW) — 선이 빠졌을 때. 채널 1~13 을 돌며 화면을 불러 찾고 그 채널에서 말한다.
// 공유기를 거치지 않는다 — 공유기에 붙지도 않는다.
#define ESPNOW_ENABLE 1
#define ESPNOW_MAGIC "FFP1"     // 화면 config.h 와 같아야 한다
#define ESPNOW_HUNT_MS 400      // 한 채널에서 답을 기다리는 시간
#define ESPNOW_CH_MAX 13        // 쓸 수 있는 가장 높은 채널(한국·유럽 13)
#define ESPNOW_PING_MS 3000     // 자리 잡은 뒤에도 이 주기로 안부를 묻는다
#define ESPNOW_ALIVE_MS 10000   // 이 시간 안에 답이 없으면 자리를 뜨고 다시 찾는다

// 화면에 UID 를 넘긴 뒤 답(이름·잔액)을 기다리는 시간 — 넘으면 실패음과 '서버에 물어보지 못했어요'
#define ACK_WAIT_MS 4000

// ── PN532 — 하드웨어 SPI(FSPI) ───────────────────────────────────
// 선은 v2 그대로다. v2 는 FSPI 를 화면이 쥐어 소프트웨어 SPI 였지만, 화면을 뗐으니 하드웨어로 돌린다.
//   VCC→3V3 · GND · SCK→9 · MISO→10 · MOSI→11 · SS→12 · 모듈 DIP 를 SPI 로(SW1 OFF · SW2 ON)
// ★ PN532 VCC·GND 바로 옆에 100µF 전해 + 0.1µF 세라믹 — 없으면 태그 순간 리셋된다.
#define PIN_NFC_SCK 9
#define PIN_NFC_MISO 10
#define PIN_NFC_MOSI 11
#define PIN_NFC_SS 12
#define NFC_POLL_EVERY_MS 120  // 폴링 사이 쉬는 시간 — 쉬는 만큼 모듈이 덜 뜨겁다
#define NFC_RETRIES 0x05       // 한 번 훑는 길이
#define SAME_TAG_COOLDOWN_MS 3000

// ── 부저 (없어도 된다) ─────────────────────────────────────────────
// -1 이면 부저 없이 **앰프로 '삑'** 을 낸다(sound.cpp 의 soundTone). 부저를 따로 달면 그 핀 번호를.
#define PIN_BUZZER -1
#define BUZZER_ACTIVE 0  // 0 = 수동(패시브) 부저 — 음 높이를 낸다 / 1 = 능동 — 켜고 끄기만

#define CPU_MHZ 160  // 240 → 160. I2S·SD 에 넉넉하고 덜 뜨겁다(80 은 USB 시리얼이 위태롭다)

// ── 소리 (microSD + MAX98357A · I2S) — v2 와 같은 배선 ─────────────
#define SOUND_ENABLE 1

// 앰프 — VIN=5V(옆에 470µF) · GND · GAIN·SD 미연결 · +/− 에 4~8Ω 3W 스피커
// ★ 앰프는 꼭 **5V 핀**에서 먹인다(3V3 에 물리면 큰 소리에서 보드가 리셋된다).
#define I2S_BCLK_PIN 17
#define I2S_LRC_PIN 18
#define I2S_DIN_PIN 8

// microSD — HSPI(SPI3). VCC=5V(레귤레이터 있는 판 · 없으면 3V3, 옆에 100µF) · GND
// ★ PN532 선에 얹지 않는다 — 흔한 모듈의 버퍼(74LVC125)는 CS 를 놓아도 MISO 를 붙잡고 있다.
#define SD_CS_PIN 42
#define SD_SCK_PIN 41
#define SD_MOSI_PIN 40
#define SD_MISO_PIN 39
#define SD_SPI_HZ 20000000  // 점퍼선이 길어 카드를 못 읽으면 4000000 으로

#define SOUND_VOLUME 22      // 0~30 — 소프트웨어로 줄인다. 앰프 GAIN 핀과 함께 맞춘다
#define SOUND_GAP_MS 60      // 토막과 토막 사이 쉼
#define SOUND_SAY_BOOT 1     // 켜져서 PN532 가 서면 '리더 준비 중'
#define SOUND_SAY_BALANCE 1  // 그냥 조회에서 잔액까지 읽는다

// 이름 소리 — 화면이 태깅 결과에 키(snd)를 실어 보내면 SD 의 /09/<키>.wav 를 앞에 읽는다.
// ★ v3 는 그 파일을 **받아 오지 않는다**(서버와 말하는 쪽이 화면이라). v2 기기에서 쓰던 SD 카드를
//   그대로 꽂으면 v2 가 받아 둔 이름 소리가 그대로 읽힌다(README '다음 걸음').
#define NAME_VOICE_ENABLE 1
#define NAME_VOICE_MAX 1024     // SD 에서 훑어 기억하는 이름 소리 수
#define SOUND_SAY_NAME 1
#define SOUND_NAME_GAP_MS 150   // 이름 뒤 쉼

// 삑을 말과 함께 낼까 — 삑은 말보다 빨라 아이는 '읽혔다' 를 먼저 안다. 0 이면 말만.
// (★ SOUND_ENABLE 0 으로 소리를 끈 판에서는 앰프 삑도 꺼진다 — 그때는 PIN_BUZZER 에 부저를 달 것)
#define SOUND_KEEP_BEEP 1
#define SOUND_REPEAT_MS 3000  // 같은 답이 두 길로 겹쳐 와도 이 안에는 한 번만 읽는다
