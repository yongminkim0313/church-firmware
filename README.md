# church-firmware — 펀펀포인트 현장 기기 펌웨어

교회 현장에 서 있는 NFC 키링 리더·디스플레이의 ESP32 펌웨어다. 전부 PlatformIO 프로젝트이고,
서버는 [funfunServer](https://github.com/yongminkim0313/church-funfunserver)
(`https://funfun.youthvision.co.kr`)를 본다.

아두이노 IDE 로 쓰던 옛 판(`.ino`)은 [church-arduino](https://github.com/yongminkim0313/church-arduino)
의 `nfcProject/`·`nfcProjectClient/` 에 그대로 있다 — **여기 있는 것이 현장에 올라가는 쪽**이다.

## 프로젝트

| 폴더 | 보드 | 무엇 |
|---|---|---|
| `nfcProject/master` | LILYGO T-RGB 2.1" 원형 (ESP32-S3 · ST7701S 480×480 · CST820 터치) | **화면만** 맡는다. 카드는 건너편 리더(`slave`)가 읽어 선(I2C)이나 라디오(ESP-NOW)로 UID 를 넘긴다. **소리도 리더가 낸다** — 이 보드가 밖으로 내주는 선은 I2C 뿐이다 |
| `nfcProject/slave` | ESP32-C3 SuperMini + PN532(SPI) + 부저 + DFPlayer Mini(UART) | **리더만** 맡는다. 화면이 없고, 읽은 UID 를 넘긴 뒤 결과를 **부저와 한국어 말**로 알린다 |
| `nfcProject/single` | LILYGO T-RGB 2.1" 원형 + PN532(I2C) + 스피커 | **master + slave 를 한 보드로 합친 것.** 읽고·묻고·그리고·소리까지 혼자 한다 |
| `funfunFirmware` | ESP32-S3-N16R8 + GC9A01 240×240 원형(SPI) + PN532(SPI) + DFPlayer Mini(UART) | `master` 를 **사진 없이 글자만**으로 옮긴 판. microSD 도 터치도 없다(BOOT 버튼으로 대신) |

세 판(`master`·`single`·`funfunFirmware`)이 서버와 직접 말한다 — 조회·지급, 설정 받기,
그리고 `/ws/device` 로 붙어 변화를 받는다. `slave` 는 서버를 모른다.

## 받아 간 뒤 한 번 — 인증정보

와이파이 비밀번호와 펀펀포인트 기기 키는 **이 저장소에 없다.** 세 프로젝트가 함께 보는
한 파일에 두고 깃에서 뺐다(`.gitignore`) — 기기 키가 새면 그 교회의 포인트를 올릴 수 있다.

```bash
cp secrets/ChurchSecrets.example.h secrets/ChurchSecrets.h
# 그다음 secrets/ChurchSecrets.h 를 열어 제 망·제 교회의 값으로 채운다
```

안 하면 빌드가 `fatal error: ChurchSecrets.h: No such file or directory` 로 그 자리에서
멈춘다 — 조용히 빈 값으로 구워져 현장에서 "왜 안 붙지" 가 되지 않게 한 것이다.

프로젝트마다 한 벌씩 두지 않은 까닭: 같은 망·같은 서버·같은 기기 키라, 나눠 두면 비밀번호를
바꿀 때 한 곳을 빠뜨리고 **그 기기만 조용히 못 붙는다.** 들어가는 길은 각 `platformio.ini` 의
`build_flags = -I ${PROJECT_DIR}/…/secrets` 다.

기기마다 다른 값(`TALENT_DEVICE_ID`·`MDNS_HOSTNAME`·핀·화면 크기)은 각 프로젝트의
`src/config.h` 에 그대로 있다 — 그쪽은 비밀이 아니다.

**`nfcProject/master` 는 와이파이를 현장에서 폰으로도 넣을 수 있다**(2026-10-26) — 화면에
뜬 QR 을 찍어 설정 창에서 고르면 플래시(NVS)에 적힌다. 여기 네 벌은 그대로 남아 뒤 순위로
쓰이니, 새 교회에 가져갈 때마다 이 파일을 고쳐 다시 구울 일은 없다.
자세한 것은 `nfcProject/master/src/README.md` 의 '와이파이를 폰으로 바꾸기'.

## 굽기

```bash
cd nfcProject/master    # 또는 single · slave  (funfunFirmware 는 저장소 바로 아래다)
pio run                 # 빌드만
pio run -t upload       # 구워 넣기
pio device monitor      # 115200
```

포트는 번호가 아니라 **보드의 시리얼**로 찾는다(`find_port.py` · `monitor_port = hwgrep://…`) —
화면 보드와 리더 보드의 USB ID 가 같아서, 둘을 같이 꽂으면 자동 고르기가 엉뚱한 쪽을 잡는다.
보드를 바꾸면 그 두 자리의 MAC 을 함께 고칠 것.

## 소리 — funfunFirmware · nfcProject

안내문을 읽는다. 합성이 아니라 **미리 구워 둔 토막**을 이어 붙이는 것이다 — DFPlayer Mini
(YX5200)가 제 microSD 에 든 mp3 를 번호로 틀어 주는 것뿐이라, 읽을 수 있는 말은 카드에
들어 있는 것뿐이다. 어떤 토막이 있어야 하는지는 `funfunFirmware/src/sound.h` 의 표가 정한다.

**모듈이 어느 보드에 붙나** — `funfunFirmware` 는 한 보드라 그 자리에 붙는다. `nfcProject` 는
**리더(`slave`)에 붙는다**(2026-10-21): DFPlayer 는 UART 로만 말을 듣는데 화면 보드(T-RGB)가
밖으로 내주는 것은 4핀 커넥터의 I2C 뿐이라, 저쪽에 달려면 I2C→UART 다리칩을 끼워야 한다 —
리더(C3)에는 쓰지 않는 핀이 그냥 남아 있고, 덤으로 스피커가 아이가 카드를 대는 자리에 온다.
화면 보드는 읽을 말을 **글자로** 건넨다(태깅 답의 `note`·`points`, 또는 `{"say":"…"}`) —
번호를 두 저장소에 나눠 들고 있지 않으려는 것이다. `nfcProject/single` 의 '스피커' 는
패시브 부저(삑·삐-삐)이고 말은 하지 않는다.

**표와 SD 는 두 프로젝트가 함께 쓴다** — 카드 한 장을 두 기기에 돌려 끼울 수 있다.

```bash
cd funfunFirmware
./tools/make_voices.sh                      # 맥의 say(Yuna) + ffmpeg → voices/ (128개)
cp -R voices/01 voices/02 voices/03 /Volumes/<SD>/   # FAT32 microSD 뿌리에
```

폴더는 `01` 고정 문구 13개 · `02` 숫자 조립 토막 14개 · `03` **0~100 통째** 101개다.
수는 거의 다 100 아래(출석 포인트·웬만한 잔액)라 그 구간은 한 토막으로 읽는다 — 이어
붙이면 '오십칠' 이 '오·십·칠' 로 띄엄띄엄 들린다(2.3초 → 1.0초).

100 을 넘으면 **앞자리만** `02` 토막으로 세우고 **끝 두 자리는 다시 `03` 에서 통째로**
가져온다 — `125` 는 백 + 이십오 두 토막이고, `12345` 는 만·이·천·삼·백·사십오 여섯
토막이다. 그래서 `02` 에서 실제로 쓰는 것은 이~구·백·천·만뿐이다(영·일·십은 번호
자리만 지킨다).

폴더 이름은 꼭 두 자리(`01`·`02`·`03`)로 두고, 파일은 `001.mp3` 꼴로 둔다 — 모듈이 폴더·파일
**번호**로 찾는다. 번호를 옮기려면 `funfunFirmware/src/sound.h`(SndClip)·`src/sound.cpp`(SNDN_\*)·
`tools/make_voices.sh` 와 **`nfcProject/slave/src/sound.{h,cpp}`** 를 함께 고칠 것.
어긋나면 엉뚱한 말이 나온다.

구운 파일도 저장소에 함께 둔다(128개 · 1.1MB) — 맥이 없는 자리에서도 SD 를 다시 채울 수 있게.

못 읽는 말이 셋 있다: **아이 이름**·**'지금 기분'**·**서버가 보낸 거절 안내**
('화요일은 출석을 안 받아요'). 교회 설정과 아이가 쓴 글에 딸려 바뀌어 미리 구울 수 없다 —
화면에는 그대로 뜨고 소리만 빈다. 서버가 합성해 보내 주는 길 셋은 `sound.h` 의 '다음 걸음' 에.

배선은 각 `src/config.h` 의 `DF_TX_PIN`·`DF_RX_PIN` 이다 — `funfunFirmware` 는 17·18,
`nfcProject/slave` 는 **0·1**(PN532 가 4~7, 부저가 10, 선이 20·21 을 쓰고 남은 자리다).
**`DF_TX` 가 모듈의 RX** 로 가고(이름이 엇갈린다), 그 선에 1kΩ 을 끼운다.
모듈을 안 달고 구울 때는 `SOUND_ENABLE 0` — 부르는 자리를 그대로 두고 아무 일도 안 한다.

화면 문구와 토막은 **글자로 맞물려 있다**: `nfcProject` 에서 화면에 띄우는 한 줄을 고치면
`slave/src/sound.cpp` 의 `NOTE_CLIPS` 표도 함께 고칠 것 — 어긋나면 그 문구만 조용해진다
(틀리게 읽지는 않는다. 리더 시리얼에 "읽을 토막이 없다" 가 찍힌다).

## 서버와 맞물린 자리

고칠 때 funfunServer 쪽도 함께 봐야 하는 것들:

| 자리 | 서버의 짝 |
|---|---|
| `GET /api/talent/config/v2` 로 받는 설정 | `talent/talentConfigV2.js` — 기본값이 바뀌면 `config.h` 의 `BL_LEVEL` 등도 맞출 것 |
| `POST /api/talent/earn` 이 **403** 이면 안내문을 띄운다 | 서버의 `attendanceWhat` — 출석 가능 시간대(요일·시간) 밖이거나 포인트 없는 구간이다. 그 `message` 가 그대로 화면에 뜬다(`nfcNote` 는 64바이트 = 한글 스무 자쯤) |
| `{"state":"config"}` 를 받으면 설정을 다시 받는다 | `talent/deviceHub.js` 의 `COMMANDS.config` — 값이 아니라 "다시 물어라" 만 온다 |
| 상태 알림(`buildState`)의 `attend`·`mode`·`note` | 관리 화면 상단 바의 기기 풍선(`NfcDisplayChip.vue`)이 그대로 그린다 |
| 사진·캐릭터 그림의 크기(`TALENT_PX`·`MOOD_ICON_PX`) | `talent/talentPhoto.js` 의 `DEVICE_SIZES`·`CHAR_PX` — 어긋나면 기기가 그림을 버린다 |
| RGB565 바이트 순서 | `talentArt.js` 가 `writeUInt16LE` 로 쓴다 — 한쪽만 바꾸면 그림이 지글거린다 |
