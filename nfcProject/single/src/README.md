# nfcSingle — PN532 를 직접 읽는 한 보드짜리 NFC 디스플레이

`master`(화면)와 `slave`(리더)를 **한 보드로 합친 것**입니다.
예전에는 ESP32-C3 리더가 PN532 를 읽어 선(I2C)이나 라디오(ESP-NOW)로 UID 를 넘겼습니다.
이제 그 보드가 없습니다 — **이 한 보드가 카드를 읽고, 서버에 묻고, 화면을 그리고, 소리를 냅니다.**

## 하드웨어

| 것 | 무엇 |
|----|------|
| 보드 | **LILYGO T-RGB 2.1" 원형** — ESP32-S3 · ST7701S RGB 480×480 · CST820 터치 |
| 리더 | **PN532 모듈** — Grove 4핀 커넥터에 **I2C** 로 (주소 `0x24`) |
| 소리 | **패시브(수동) 스피커** — GPIO0 (BOOT 패드에 납땜) |
| 저장 | microSD (사진) — 그대로 씁니다 |

### 배선

```
T-RGB Grove 4핀            PN532 모듈
  3V3   ───────────────     VCC
  GND   ───────────────     GND
  GPIO8  (SDA) ───────      SDA
  GPIO48 (SCL) ───────      SCL

T-RGB BOOT 버튼 패드        패시브 스피커
  GPIO0 ──[100Ω]───────     (+)
  GND   ───────────────     (−)
```

* **PN532 모듈 딥스위치를 I2C 로** 놓아야 합니다(Elechouse V3 는 보통 `1:ON 2:OFF` — 보드 뒷면 표로 확인).
  SPI 로 두면 SDA 가 조용해 못 찾습니다.
* PN532 의 VCC·GND 바로 옆에 **100µF 전해 + 0.1µF 세라믹**을 병렬로. 없으면 태그 순간 리셋됩니다.
* IRQ·RSTPD_N 은 **잇지 않습니다**(남는 핀이 없습니다). 라이브러리에 `255` 를 넘겨 그 둘을
  안 쓰는 길로 돕니다 — 준비 여부는 I2C 로 읽고, 하드웨어 리셋은 건너뜁니다.
* 스피커의 100Ω 은 **부팅을 지키려고** 둡니다. GPIO0 은 부팅 모드를 정하는 스트래핑 핀이라,
  스피커 코일(보통 8Ω)이 그 핀을 GND 로 끌어내리면 부팅할 때 BOOT 를 누른 것처럼 보일 수 있습니다.

### ★ 왜 SPI 가 아니라 I2C 인가

**이 보드에는 남는 GPIO 가 없습니다.** 라일리고 공식 문서에 그대로 적혀 있습니다 —
*"The Grove port has been set to I2C, sharing the bus with the touch and cannot be used for
other purposes"*, *"T-RGB has no free GPIO and cannot be expanded"*.

| 쓰는 곳 | 핀 |
|---------|-----|
| RGB 패널 | 2·3·5·6·7·9~18·21·41·42·45·47 |
| OPI PSRAM | 35~37 |
| microSD (SDMMC) | 38·39·40 |
| 백라이트 | 46 |
| USB | 19·20 |
| I2C (터치 0x15 · 확장칩 XL9535 0x20) | **8 (SDA) · 48 (SCL)** ← 밖으로 나온 둘뿐 |
| 터치 INT | 1 (터치칩이 끌어내리는 선 — 우리가 밀 수 없다) |
| 배터리 전압 | 4 (분압에 물려 있다) |
| UART0 | 43·44 (커넥터에 나오지 않는다) |

밖으로 나온 GPIO 는 Grove 4핀의 **GPIO8·GPIO48 둘뿐이고 그것도 이미 I2C** 입니다 —
SPI 넉 선(SCK·MISO·MOSI·SS)을 뽑을 자리가 없습니다. 그래서 PN532 를 그 I2C 버스에 함께
태웠습니다(`0x24` 는 터치 `0x15`·확장칩 `0x20` 과 겹치지 않고, 풀업도 보드에 이미 있습니다).
선 네 가닥만 꽂으면 끝이고 **납땜은 스피커 한 곳뿐**입니다.

> **쓰던 모듈의 I2C 가 고장 났다면** — `slave` 머리말에 2026-09-24 에 그 모듈의 SCL 이
> 고장 난 것을 확인한 기록이 있습니다. 그 모듈로는 이 배선이 안 됩니다(멀쩡한 모듈이어야 합니다).
> PN532 를 못 찾으면 5초마다 다시 찾아보며 시리얼에 배선·딥스위치를 확인하라고 찍습니다.
> SPI 로 가야 한다면 microSD(38·39·40)를 포기하고 그 핀을 SPI 로 돌리는 수밖에 없는데,
> 그러면 사진 기능이 함께 사라집니다.

## 하는 일

1. `config.h` 의 와이파이에 붙는다 (붙을 때까지 화면은 계속 살아 있다)
2. `nfc-display.local` 로 자기 이름을 알린다 (mDNS)
3. 포트 **81** 에 WebSocket 서버를 연다 — 브라우저·폰은 예전처럼 여기로 붙는다
4. **PN532 로 `NFC_POLL_EVERY_MS`(120ms)마다 카드를 훑는다**
5. UID 를 읽으면 서버에 이름·잔액을 묻고(출석모드면 지급까지 하고) 그 아이 사진 위에 띄운다
6. **결과에 맞는 소리를 낸다** — 아는 키링이면 `삑`, 모르는 카드·서버에 못 물었으면 `삐-삐`
7. 대기 화면은 **사용 활성(useEnable)** 그림. 짧게 누르면 microSD 사진이 순서대로 넘어가고,
   길게 누르면 서버에서 사진을 다시 받는다
8. 와이파이에 붙어 있으면 오른쪽 위에 연결 아이콘을 겹친다

## master·slave 에서 달라진 것

| | 달라진 점 |
|--|-----------|
| **없어진 것** | 선(I2C 슬레이브 링크) · ESP-NOW · 그 둘로 결과를 돌려주던 `linkSendResult` · 리더가 답을 기다리던 `ackService` · 채널 사냥(`espnowHunt`) · `radioGate` |
| **옮겨 온 것** | PN532 읽기(`nfcService`·`gotUid`·`uidToHex`·`nfcProbe`) · 소리(`beep`·`sndOk`·`sndFail`·`sndReady`) |
| **그대로인 것** | 화면·폰트·사진 동기화·터치·서버 설정(v2.0)·출석모드·WebSocket 규약·`handleCommand` |
| **한 줄만 바뀐 것** | 읽은 UID 를 '선·라디오로 보내던' 자리가 '`pendingUid` 에 적어 두는' 자리로. 그러면 `loop` 가 예전에 리더에게 받아 처리하던 그 흐름을 그대로 탄다 |
| **소리 고르는 곳** | 예전엔 리더가 디스플레이의 답(`handleDisplayReply`)으로 골랐다. 이제 `processTag` 가 화면에 띄운 판정(`nfcKnown`)이 그대로 소리다 |
| **상태 점의 뜻** | '붙은 client 가 있나' → **'내가 제구실을 하나'**. 초록 = PN532·와이파이 다 멀쩡 · 노랑 = 카드는 읽히는데 와이파이 없음 · 빨강 = PN532 없음 |
| **CPU 클럭** | `slave` 의 `CPU_MHZ 80` 은 **가져오지 않았다** — 이 보드는 RGB 패널을 돌려야 해서 240MHz 가 필요하다 |

## 설정 — `config.h`

```c
#define WIFI_SSID_0 "..."           // 2.4GHz 공유기 (0~3 순위)
#define MDNS_HOSTNAME "nfc-display" // nfc-display.local
#define WS_PORT       81            // ws://nfc-display.local:81/

#define PIN_BUZZER    0             // 패시브 스피커 (BOOT 패드)
#define BUZZER_ACTIVE 0             // 0 = 패시브(음을 낸다) / 1 = 액티브(켜고 끄기만)
#define NFC_POLL_EVERY_MS 120       // 폴링 사이 쉬는 시간 (PN532 발열)
#define NFC_RETRIES    0x05         // 한 번 훑는 길이
#define SAME_TAG_COOLDOWN_MS 3000   // 대고 있는 동안 같은 UID 는 한 번으로

#define TALENT_SERVER "https://funfun.youthvision.co.kr"
#define TALENT_DEVICE_ID "nfc-display"
```

## 시리얼 명령 (115200)

| 글자 | 하는 일 |
|------|---------|
| `n` | 다음 사진 (터치와 같다) |
| `p` | 이전 사진 |
| `h` | 활성 화면으로 |
| `s` | 넘겨 볼 사진 목록 |
| `l` | SD `/talent` 파일 목록 |
| `c` | 서버 설정(밝기·출석모드) 지금 다시 받기 |
| `i` | 기기 ID·밝기·출석모드·출석 카드 · **PN532**·ws client·와이파이 상태 |
| `t` | 시험 NFC 화면 (카드·서버 없이 글자를 본다 — 누를 때마다 다음 보기) |
| `g` | 불러오는 중 고리를 3초 돌려 본다 |
| `b` | **소리 시험** — 삑 · 삐-삐 · 삐리 |
| `f` | **PN532 로 8초 동안 직접 카드를 훑어 본다** (배선·카드 확인용) |
| `r` | 재부팅 |
| `?` | 도움말 |

## WebSocket 규약

`master` 와 **똑같습니다**. 접속: `ws://nfc-display.local:81/`

브라우저에서 카드 없이 시험:
```js
const ws = new WebSocket("ws://nfc-display.local:81/");
ws.onmessage = e => console.log(e.data);
ws.onopen = () => ws.send('{"uid":"04CE1B53D12A81"}');   // 카드를 댄 것처럼
```

## 빌드

```bash
cd single
pio run          # 빌드
pio run -t upload
pio device monitor
```

보드/PSRAM 설정은 `platformio.ini` 가 들고 있습니다(Arduino IDE 로 구울 때는 `master/src/README.md`
의 도구 메뉴 표를 그대로 쓰세요 — 보드 설정은 달라지지 않았습니다).

> LilyGo-T-RGB 라이브러리(`LV_Helper.h`·`LilyGo_RGBPanel.h`)는 **넣지 마세요** — lvgl/SensorLib
> 버전 충돌로 빌드가 깨집니다. 이 스케치는 Arduino_GFX 로 직접 그립니다.

## 파일

| 파일 | 내용 |
|------|------|
| `main.cpp` | 화면·와이파이·mDNS·WebSocket 서버 · **PN532 읽기** · **소리** |
| `config.h` | 와이파이·mDNS·포트·**PN532·스피커·폴링** 설정 (여기만 고치면 된다) |
| `useEnable.h` | 480×480 활성 그림 (대기 화면 첫 장) |
| `connected.h` | 50×35 연결 아이콘 (투명색 `0xF81F`) |
