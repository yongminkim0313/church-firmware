# nfcProject — WiFi WebSocket 디스플레이 (CST820 원형)

`TalentNfcReader_CST820_v1.0` 의 화면·와이파이 코드를 토대로 만든, **화면 전용** 장치입니다.
NFC 를 직접 읽지 않습니다 — 외부(NFC 시스템)가 상태를 보내면 그 그림으로 바꿉니다.

리더(`nfcProjectClient`)는 **두 갈래로 온다**: 옆면 4핀 커넥터에 물린 **선(I2C)** 이 1순위,
**WebSocket** 이 2순위다. 브라우저·폰은 예전처럼 WebSocket 으로 붙는다.
두 길에 흐르는 글자는 같아서 받는 곳도 `handleCommand` 하나다 — 아래 [리더와 잇는 선](#리더와-잇는-선-i2c) 참고.

## 하드웨어
- **LILYGO T-RGB 2.1" 원형** — ESP32-S3 · ST7701S RGB 480×480 · CST820 터치 (v1.0 과 같은 보드/모듈)

## 하는 일
1. `config.h` 의 와이파이에 붙는다 (붙을 때까지 화면은 계속 살아 있다)
2. `nfc-display.local` 로 자기 이름을 알린다 (mDNS)
3. 포트 **81** 에 WebSocket 서버를 연다 (같은 망의 브라우저·폰이 붙는 자리)
3-1. **펀펀포인트 서버에는 제가 붙는다**(`wss://…/ws/device`) — 포인트가 바뀌면
   서버가 그 아이를 이 화면에 띄우라고 밀어 준다. 아래 [서버에 붙어 변화 받기](#서버에-붙어-변화-받기) 참고
4. 대기 화면은 항상 **사용 활성(useEnable)** 그림이다 (비활성 그림은 더 쓰지 않는다)
5. 화면을 짧게 누르면(또는 시리얼 `n`) microSD 에 받아 둔 사진이 순서대로 넘어간다 —
   활성 → 사진1 → … → 사진N → 다시 활성. 길게 누르면 서버에서 사진을 다시 받는다
   (이름표의 `img` 해시를 `/talent/<UID>.ver` 에 적어 두고, 같으면 받지 않는다 — 바뀐 사진만 받는다)
6. 와이파이에 붙어 있으면 오른쪽 위에 연결 아이콘을 겹친다
7. 리더가 선(I2C)에 물려 있으면 `LINK_POLL_MS`(50ms)마다 "줄 것 있나" 하고 읽어 온다
8. 리더(`nfcProjectClient`)가 `{"uid":"…"}` 를 보내면 **서버에 이름·잔액을 물어** 그 아이 사진 위에 띄운다.
   출석모드가 켜져 있으면 물어보는 대신 **바로 출석 포인트를 지급**한다

## 서버 설정 — 관리자 화면 '펀펀포인트 → NFC 관리 → 리더설정 → v2.0'

부팅해서 와이파이에 붙을 때, 그 뒤 `CONFIG_TTL_MS`(5분)마다 `GET /api/talent/config/v2?device=<TALENT_DEVICE_ID>` 로 받아 온다.
못 받으면 들고 있던 값으로 계속 돈다 — 와이파이가 흔들릴 때마다 밝기가 튀거나 출석모드가 꺼지면 현장이 더 혼란스럽다.

| 설정 | 하는 일 |
|------|---------|
| 화면 밝기 `backlight` | 1(어둡다)~16(밝다). 받는 즉시 백라이트에 적용 (펌웨어 기본값은 `BL_LEVEL`) |
| 출석모드 `attendanceMode` | 켜면 키링을 댄 그 자리에서 바로 출석 포인트를 지급한다 |
| 출석에 쓸 카드 `attendanceCard` | **얼마를 어떤 사유로** 줄지는 서버에 등록된 이 지급 카드가 정한다. 비우면 지급하지 않고 사진·잔액만 띄운다 |

금액을 기기가 보내지 않는 이유: 리더는 현장에 놓인 물건이라 요청을 흉내 내기 쉽다.
값이 서버에만 있으면 그래 봐야 관리자가 정한 범위를 벗어나지 못한다(v1.0 리더와 같은 규칙).

**서버와 이야기하는 쪽은 이 기기 하나다.** 리더는 읽은 UID 를 넘기기만 한다 —
한 태깅이 두 곳에서 처리되면 같은 출석이 두 번 올라간다.

같은 키링이 `ATTEND_COOLDOWN_MS`(10분) 안에 또 오면 `이미 출석했어요` 만 띄우고 주지 않는다.
기기에 시계(RTC)가 없어 "하루 한 번" 을 알 수 없어서 시간 간격으로 막는다.

## 설정 — `config.h`
```c
#define WIFI_SSID     "여기에_SSID"       // 2.4GHz 공유기
#define WIFI_PASSWORD "여기에_비밀번호"
#define MDNS_HOSTNAME "nfc-display"       // nfc-display.local
#define WS_PORT       81                  // ws://nfc-display.local:81/

#define TALENT_HUB_ENABLE 1               // 서버에 붙어 변화 받기 (0 = 끔)
#define TALENT_HUB_PATH   "/ws/device"    // 서버 주소는 TALENT_SERVER 에서 떼어 쓴다

#define LINK_I2C_ENABLE  1                // 리더와 잇는 선 (0 = WebSocket 만)
#define LINK_I2C_ADDR 0x30                // 리더의 I2C 주소 (리더 config.h 와 같아야 한다)
#define LINK_POLL_MS    50                // 리더에게 "줄 것 있나" 를 묻는 주기
```

## WebSocket 규약
접속: `ws://nfc-display.local:81/` (또는 시리얼에 찍히는 IP)

**보내는 쪽 → 보드** (아무 형식이나):
| 형식 | 예 |
|------|-----|
| JSON `state` | `{"state":"enable"}`(활성 화면으로) · `{"state":"next"}`·`{"state":"toggle"}`(다음 사진) · `{"state":"prev"}` |
| 그냥 글자 | `enable` · `next` · `toggle` · `prev` |
| NFC 알림 | `{"uid":"04A1B2C3"}` — **UID 하나면 된다**(이름·잔액은 이 보드가 서버에 묻는다) |
| 변화 알림 | `{"uid":"04A1B2C3","name":"홍길동","points":1205,"note":"+5P 출석","mood":"오늘 소풍 가요"}` |

예전 규약의 `disable`·`{"enabled":false}` 는 활성 화면으로 돌아가는 것으로 받는다.
`{"uid":…,"name":…,"points":…}` 처럼 이름·잔액이 함께 오면 묻지 않고 **그대로 띄운다**(서버
왕복이 없다). 옛 리더 호환으로 둔 자리인데, 지금은 **서버가 변화를 밀어 줄 때 쓴다** —
아래 [서버에 붙어 변화 받기](#서버에-붙어-변화-받기). 두 길(여기로 들어오는 것과 서버에서
오는 것)이 **같은 말**이라 받는 곳도 `handleCommand` 하나다.

| 칸 | 뜻 |
|----|-----|
| `uid` | 사진을 찾는 열쇠(SD `/talent/<uid>.565`). 그 사진이 없으면 기본 그림 위에 글자만 뜬다 |
| `note` | 하단 한 줄 안내 — `+5P 출석` 처럼. 칸이 **32바이트**라 넘치면 글자 경계에서 자른다 |
| `mood` | '지금 기분' 한 줄(아이가 적은 글). `moodIcon` 까지 주면 캐릭터 그림도 띄운다 |
| `status` | 서버가 미는 모양 — `{icon,label,text}`. 조회 응답(`/feed`)과 같아서 같은 함수로 푼다 |

**보드 → 붙은 쪽** (상태가 바뀌거나 새로 붙을 때마다):
```json
{"mode":"idle","photo":"enable","index":0,"count":52,"online":true}
{"mode":"nfc","uid":"04A1B2C3","name":"홍길동","points":1205,"known":true,"note":"출석 완료 +5P","online":true}
```
(`photo` 는 `enable` 또는 지금 보이는 사진의 UID · `note` 는 하단에 띄운 한 줄 안내)

리더는 이 응답으로 소리를 고른다 — 서버와 말하지 않으므로 결과를 알 길이 이것뿐이다.
부저(삑·삐-삐)뿐 아니라 **한국어 말**도 여기서 나온다. 아래 [소리](#소리--말은-리더가-한다) 참고.

## 서버에 붙어 변화 받기

이 보드는 WebSocket **서버**이면서(위) 펀펀포인트 서버에는 **client 로 붙어 있는다.**
관리 화면에서 포인트를 조정하거나 앱·다른 리더에서 변화가 생기면, 서버가 그 아이를
이 화면에 띄우라고 밀어 준다 — **아무도 관리 화면을 보고 있지 않아도 온다.**

| | |
|---|---|
| 어디로 | `TALENT_SERVER` 에서 호스트·포트·ssl 을 떼어 `TALENT_HUB_PATH`(`/ws/device`) |
| 무엇으로 | 헤더 둘 — `x-talent-key`(= `TALENT_DEVICE_KEY`) · `x-talent-device`(= `TALENT_DEVICE_ID`) |
| 받는 것 | 위 규약 그대로. `handleCommand` 가 푼다 |
| 보내는 것 | 화면이 바뀔 때마다 지금 상태(`buildState`) — 관리 화면이 "무엇을 띄우고 있나" 를 본다 |
| 끊기면 | `HUB_RETRY_MS`(5초)마다 다시 붙는다. 서버도 25초마다 ping 한다 |

**주소를 따로 적지 않는 까닭**: 두 곳에 적어 두면 한쪽만 고쳐져, 조회·지급은 새 서버로
가는데 화면은 옛 서버를 기다리는 꼴이 된다. `TALENT_SERVER` 한 줄이 기기의 서버다.

**기기 키가 곧 교회다.** 서버는 이 키로 어느 교회인지까지 가르고(여러 교회가 한 서버를
쓴다), 맞지 않으면 `4401` 로 닫는다. 키는 HTTP 로 조회·지급할 때 싣는 그 값 그대로다.

`TALENT_HUB_ENABLE 0` 으로 두면 이 길만 꺼진다 — 조회·지급·사진은 그대로 돌고,
**밀어 주는 것만 안 온다.**

> 서버 쪽은 funfunServer 의 `talent/deviceHub.js` 다. 왜 서버가 이 보드를 부르지 않고
> 이 보드가 붙는지(운영 서버는 이 망에 없다), 왜 브라우저가 붙던 길을 걷어냈는지는
> 그 파일 머리말과 funfunServer 의 CLAUDE.md 에 적혀 있다.

## 리더와 잇는 선 (I2C)

리더(`nfcProjectClient`)를 옆면 1.25mm **4핀 커넥터**에 물리면 공유기·mDNS 를 거치지 않는다 —
망이 죽어도 태깅이 되고, 왕복이 수백 ms 에서 수 ms 로 준다. **WebSocket 서버는 그대로 열려 있다**
(브라우저·폰은 계속 그쪽으로 붙는다).

| 4핀 커넥터 | 리더 (ESP32-C3) |
|------------|-----------------|
| GND | GND |
| GPIO8 (SDA) | GPIO20 |
| GPIO48 (SCL) | GPIO21 |
| 3V3 | **잇지 말 것** — 리더는 자기 USB 로 먹인다 |

리더는 이 버스의 **슬레이브(`0x30`)** 다. 터치(`0x15`)·화면 확장칩 XL9535(`0x20`)와 겹치지 않는다.
풀업은 이 보드에 이미 있다.

**UART 가 아닌 이유**: 이 보드가 밖으로 내주는 것은 그 커넥터 하나뿐이고, RGB 패널이
`2·3·5·6·7·9~18·21·41·42·45·47` 을, SD 가 `38·39·40` 을, 백라이트가 `46` 을, OPI PSRAM 이
`35~37` 을 먹는다. `43·44` 는 네이티브 USB 라 커넥터에 나오지 않고, `1` 은 터치 INT,
`4` 는 배터리 전압 측정이다 — UART 로 돌릴 핀이 남지 않는다.

**슬레이브는 먼저 말을 걸 수 없어** 이쪽이 마스터로 `LINK_POLL_MS`(50ms)마다 읽어 온다.
한 덩이는 `[0]=글자 길이(0 = 줄 것 없음) · [1..]=글자` 다. 결과는 이쪽이 써 넣는데,
I2C 한 덩이에 들어가야 해서 `mode`·`online` 을 뺀 짧은 꼴로 보낸다(`linkSendResult`).
리더는 `uid` 로 자기 답인지 가리므로 그래도 알아본다.

선이 없으면 `requestFrom` 이 빈손으로 돌아온다. 코어가 실패마다 에러 두 줄을 찍어 loop 가
늘어지므로, `LINK_IDLE_AFTER`(3)번 잇달아 답이 없으면 `LINK_IDLE_POLL_MS`(1초)마다만 묻고,
리더가 답하면 바로 50ms 로 돌아간다. 선을 안 쓰려면 `config.h` 의
`LINK_I2C_ENABLE` 을 `0` 으로 둔다.

상단 상태 점은 **선이 살아 있으면 와이파이가 없어도 초록**이다 — 망이 죽어도 태깅은 되기 때문이다.

## 소리 — 말은 리더가 한다

`funfunFirmware` 처럼 한국어로 말을 한다: "출석 완료 · 오 · 포인트", "잔여 포인트 · 백이십오".
미리 구워 둔 토막(mp3)을 이어 읽는 것이고 합성은 하지 않는다 — **읽을 수 있는 말은 SD 에
들어 있는 것뿐**이다(아이 이름·'지금 기분'·서버가 보낸 거절 안내는 못 읽는다. 그 자리는
조용히 지나간다 — 화면에는 그 글이 떠 있다).

**모듈(DFPlayer Mini)은 이 보드가 아니라 리더에 붙는다.** DFPlayer 는 UART 로만 말을 듣는데,
위 [리더와 잇는 선](#리더와-잇는-선-i2c) 의 'UART 가 아닌 이유' 가 그대로 걸린다 — 밖으로
나오는 선이 I2C 뿐이다. 저쪽에 달려면 I2C→UART 다리칩을 끼워야 하지만, 리더(C3)에는 쓰지
않는 핀이 그냥 남아 있다. 덤으로 스피커가 **아이가 카드를 대는 자리**에 온다.
배선·음량·SD 짜임새는 모두 `../slave/src/sound.h` 와 그 `config.h` 에 있다.

읽을 말을 고르는 재료는 **글자**다 — 이 보드가 화면에 띄우는 그 한 줄을 리더에게 그대로
보내고, 리더가 제 표에서 토막을 찾는다. 두 길로 간다:

| 무엇 | 어떻게 | 보기 |
|------|--------|------|
| 태깅 결과 | `linkSendResult()` 의 `note`·`points` — 소리를 달기 전부터 오가던 그 덩이다 | `"note":"출석 완료 +5P"` → [출석 완료][오][포인트] |
| 이 보드 사정 | `readerSay()` → `{"say":"…"}` | 와이파이가 끊긴 순간 · 시리얼 `c` 로 설정을 다시 받을 때 |

★ **화면 문구를 고치면 리더의 표(`../slave/src/sound.cpp` 의 `NOTE_CLIPS`)도 함께 고칠 것.**
어긋나면 그 문구만 조용해진다(틀리게 읽지는 않는다 — 리더 시리얼에 "읽을 토막이 없다" 가 찍힌다).

`config.h` 에 둔 것은 '언제 말을 걸까' 둘뿐이다 — `SOUND_SAY_WIFI`(끊긴 순간 한 번, 기본 켜짐)·
`SOUND_SAY_IDLE`(대기 화면마다, 기본 꺼짐 — 줄이 선 현장에서는 끊임없이 떠든다).
스피커가 살아 있나 보려면 시리얼 `v`.

## 시리얼 명령 (115200)
| 글자 | 하는 일 |
|------|---------|
| `n` | 다음 사진 (터치와 같다) |
| `p` | 이전 사진 |
| `h` | 활성 화면으로 |
| `s` | 넘겨 볼 사진 목록 |
| `l` | SD `/talent` 파일 목록 |
| `c` | 서버 설정(밝기·출석모드) 지금 다시 받기 (리더가 "설정 다시 받는 중" 을 읽는다) |
| `i` | 기기 ID·밝기·출석모드·출석 카드 · **서버 연결** · 선(리더)·ws client·와이파이 상태 |
| `v` | 리더에게 한 마디 읽어 달라고 보낸다 — 스피커·SD 가 살아 있나 본다 |
| `r` | 재부팅 |
| `?` | 도움말 |

브라우저에서 빠른 시험:
```js
const ws = new WebSocket("ws://nfc-display.local:81/");
ws.onmessage = e => console.log(e.data);
ws.onopen = () => ws.send('{"state":"enable"}');
```

## 빌드 (Arduino IDE 도구 메뉴)
- 보드: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM**
- Flash Size: **16MB**
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)** — 그림 두 장이 플래시에 들어간다
- USB CDC On Boot: **Enabled**
- 라이브러리: **GFX Library for Arduino** · **ArduinoJson** · **WebSockets** (by Markus Sattler / Links2004)

arduino-cli:
```bash
arduino-cli compile --fqbn \
  esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc \
  nfcProject
```

> LilyGo-T-RGB 라이브러리(`LV_Helper.h`·`LilyGo_RGBPanel.h`)는 **넣지 마세요** — lvgl/SensorLib
> 버전 충돌로 빌드가 깨집니다. 이 스케치는 Arduino_GFX 로 직접 그립니다(v1.0 과 같은 이유).

## 그림 바꾸기
`useEnable.h` 는 480×480 RGB565 이미지 헤더입니다(`useDisable.h` 는 더 쓰지 않아 빌드에 들어가지
않습니다). 자기 그림으로 바꾸려면 `tools/img2rgb565_dither.py` 로 다시 만들어 덮어쓰세요
(`--var` 이름은 `USEENABLE` 로 유지):
```bash
cd /Users/kimyongmin/workspace/arduino
python3 tools/img2rgb565_dither.py <활성그림> --width 480 --height 480 --fit cover \
    --var USEENABLE  -o nfcProject/useEnable.h
```

## 파일
| 파일 | 내용 |
|------|------|
| `nfcProject.ino` | 화면·와이파이·mDNS·WebSocket 서버 · 리더와 잇는 선(I2C 마스터) |
| `config.h` | 와이파이·mDNS·포트·선(I2C) 설정 (여기만 고치면 된다) |
| `useEnable.h` | 480×480 활성 그림 (대기 화면 첫 장) |
| `useDisable.h` | 쓰지 않음 (포함하지 않는다) |
| `connected.h` | 50×35 연결 아이콘 (투명색 0xF81F) |
