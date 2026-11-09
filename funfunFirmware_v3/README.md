# funfunFirmware_v3 — 480 원형 화면 + 리더, 두 보드 판

v2(`../funfunFirmware_v2` — 데브킷 한 보드 · 원형 240)를 **LILYGO T-RGB 480×480 원형**(nfcProject/master 의
그 화면)으로 옮긴 판이다. T-RGB 는 밖으로 나온 핀이 Grove 의 I2C 두 가닥(GPIO8·48)뿐이라 PN532·SD·앰프를
달 수 없다. 그래서 둘로 나눴다 — 구조와 규약은 nfcProject 의 master(화면) ↔ slave(리더)와 같다.

| 폴더 | 보드 | 하는 일 |
|---|---|---|
| `display/` | LILYGO T-RGB 2.1" (ESP32-S3 · ST7701S 480×480 · CST820 터치) | 서버와 말한다(조회·출석 지급·이름표·사진·대기 화면 숫자·허브) · 그린다 · 터치 |
| `reader/` | ESP32-S3-DevKitC-1 N16R8 + PN532 + microSD + MAX98357A | 카드를 읽어 화면에 넘긴다 · 소리를 낸다(삑 · 안내 · 숫자 · 이름) |

리더는 서버와 말하지 않는다 — 한 태깅을 두 곳에서 처리하면 출석이 두 번 올라간다.

## 배선

그림: https://claude.ai/artifact/FT2UEbWpkSMQuXLxhsBx4j

리더 보드는 **v2 배선에서 GC9A01 화면만 뗀 것**이다(PN532·SD·앰프 선은 그대로).
화면을 뗀 자리(GPIO4·5)로 T-RGB 와 잇는다.

```
T-RGB Grove 4핀          리더 (DevKitC)
  GND  ───────────────── GND
  GPIO8  (SDA) ────────── GPIO4
  GPIO48 (SCL) ────────── GPIO5
  3V3  ─ 잇지 않는다 (두 보드 모두 제 USB 로 먹는다)
```

| 리더의 모듈 | 핀 |
|---|---|
| PN532 (하드웨어 SPI · FSPI) | VCC 3V3 · SCK 9 · MISO 10 · MOSI 11 · SS 12 · DIP 는 SPI (SW1 OFF · SW2 ON) |
| microSD (HSPI) | VCC 5V · CS 42 · SCK 41 · MOSI 40 · MISO 39 |
| MAX98357A (I2S) | VIN 5V(+470µF) · BCLK 17 · LRC 18 · DIN 8 · GAIN·SD 미연결 · +/− 스피커 4~8Ω |

v2 는 PN532 를 소프트웨어 SPI 로 돌렸다(FSPI 를 화면이 쥐어서). 화면을 뗐으니 하드웨어 SPI 로 돌아왔다 — 선은 같다.
선이 빠지면 리더가 채널을 돌며 화면을 찾아 **ESP-NOW** 로 말한다(공유기를 거치지 않는다).

## 굽기

두 보드는 USB ID 가 같다(303A:1001). 각 폴더의 `find_port.py` 가 **시리얼(MAC)** 로 제 보드를 찾고,
못 찾으면 업로드를 멈춘다 — 엉뚱한 보드를 덮어쓰지 않게. 보드를 바꾸면 그 파일의 `BOARD_SERIAL` 한 줄을 고친다.

```bash
cd display && pio run -t upload    # 화면 — T-RGB  68:EE:8F:47:30:FC
cd reader  && pio run -t upload    # 리더 — DevKitC A4:CB:8F:D3:52:20
```

리더 SD 카드: FAT32 로 포맷하고 `reader/voices/` 의 `01 02 03` 을 뿌리에 복사한다(`reader/tools/make_voices.sh` 가 만든다).
**v2 에서 쓰던 SD 카드를 그대로 꽂아도 된다** — 짜임새가 같고, v2 가 받아 둔 이름 소리(`/09`)와 고정 토막 판까지 그대로 읽힌다.

## 화면 — v2 와 같은 코드

그림은 v2 처럼 **240 캔버스**에 그리고, 패널로 보낼 때 가로세로 두 배로 늘린다(`display/src/main.cpp` 의 `Panel2x`).
그래서 `home.h` · `pages.h` · NFC 화면 · 목록 굴리기 · 설정 모드 QR 이 v2 와 **한 줄도 다르지 않다.**
v2 의 그림·서버 코드를 고치면 같은 곳을 그대로 옮겨 오면 된다(다른 곳은 아래 표의 하드웨어 층뿐).

| | v2 (데브킷 한 보드) | v3 화면 |
|---|---|---|
| 패널 | GC9A01 240 (SPI) | ST7701S 480 (RGB) — 240 을 두 배로 |
| 카드 | PN532 를 직접(nfcTask) | 리더가 넘긴다 — `linkService` · `espnowService` → `handleCommand` |
| 소리 | SD + I2S 를 직접(`sound.cpp`) | 글자로 리더에게 — `readerResult` · `readerSay`(`sound.cpp` 는 얇은 겉봉) |
| 버튼 | BOOT + GPIO1 터치 패드 | BOOT + 화면 터치(CST820) — 같은 한 단추 |
| 백라이트 | LEDC PWM(BLK 선이 있으면) | GPIO46 을 흔든 횟수로 16단계 |
| 상태 불 | WS2812(GPIO48) | 없다 — GPIO48 이 I2C SCL 이다 |
| 기기 이름 | `fun-2` | `fun-3` · mDNS `funfun-v3` |

## 둘 사이에 오가는 것 (JSON 한 덩이)

| 쪽 | 글자 | 뜻 |
|---|---|---|
| 리더 → 화면 | `{"uid":"04CE1B53D12A81"}` | 카드를 댔다 |
| 화면 → 리더 | `{"uid","name","points","known","note","snd"}` | 태깅 결과 — 리더가 삑을 가르고 note·points 를 읽는다. `snd` 는 이름 소리 키(SD `/09/<snd>.wav`) |
| 화면 → 리더 | `{"say":"설정 다시 받는 중"}` (`"more":true` 면 앞의 말 뒤에 잇는다) | 화면 제 쪽 사정 |
| 화면 → 리더 | `{"name":"<키>"}` · `{"test":"voice"}` | 시험 — 화면 시리얼 `a` · `v` |

★ 화면이 보내는 문구는 리더의 표(`reader/src/sound.cpp` 의 `NOTE_CLIPS`)와 **한 자도** 틀리면 안 된다 —
틀리면 그 문구만 조용해진다(리더 시리얼에 "읽을 토막이 없다").

## 다음 걸음 — 이름 소리 · 고정 토막 판 받기

v2 는 서버가 합성한 이름 소리와 고정 토막 판을 **받아 제 SD 에 써 두었다.** v3 는 받는 쪽(서버와 말하는 화면)과
두는 쪽(SD 가 달린 리더)이 갈라져, 둘 사이에 파일을 건네는 길이 있어야 한다. 아직 없다:

- 지금: 화면은 이름표의 이름 소리 키를 결과에 실어 보내고, 리더는 **SD 에 있으면** 읽는다.
  새로 등록한 아이의 이름 소리는 v2 기기로 받아 둔 카드를 옮겨 꽂거나, 다음 걸음을 기다린다.
- 길 하나: 화면이 받은 WAV 를 선(I2C)으로 조각내 넘기고 리더가 SD 에 쓴다. 한 이름(30~40KB)에 몇 초,
  고정 토막 판(5MB)은 수 분 — 대기 화면일 때 뒤에서 돈다. 화면 `sound.cpp` 의 `soundStore*` 자리를 채우면 된다.
