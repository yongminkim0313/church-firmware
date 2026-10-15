# church-firmware — 펀펀포인트 현장 기기 펌웨어

교회 현장에 서 있는 NFC 키링 리더·디스플레이의 ESP32 펌웨어다. 전부 PlatformIO 프로젝트이고,
서버는 [funfunServer](https://github.com/yongminkim0313/church-funfunserver)
(`https://funfun.youthvision.co.kr`)를 본다.

아두이노 IDE 로 쓰던 옛 판(`.ino`)은 [church-arduino](https://github.com/yongminkim0313/church-arduino)
의 `nfcProject/`·`nfcProjectClient/` 에 그대로 있다 — **여기 있는 것이 현장에 올라가는 쪽**이다.

## 프로젝트

| 폴더 | 보드 | 무엇 |
|---|---|---|
| `nfcProject/master` | LILYGO T-RGB 2.1" 원형 (ESP32-S3 · ST7701S 480×480 · CST820 터치) | **화면만** 맡는다. 카드는 건너편 리더(`slave`)가 읽어 선(I2C)이나 라디오(ESP-NOW)로 UID 를 넘긴다 |
| `nfcProject/slave` | ESP32-C3 SuperMini + PN532(SPI) + 부저 | **리더만** 맡는다. 화면이 없고, 읽은 UID 를 넘기고 소리로 결과를 알린다 |
| `nfcProject/single` | LILYGO T-RGB 2.1" 원형 + PN532(I2C) + 스피커 | **master + slave 를 한 보드로 합친 것.** 읽고·묻고·그리고·소리까지 혼자 한다 |
| `LCDTest` | ESP32-S3-N16R8 + GC9A01 240×240 원형(SPI) + PN532(SPI) | `master` 를 **사진 없이 글자만**으로 옮긴 판. microSD 도 터치도 없다(BOOT 버튼으로 대신) |

세 판(`master`·`single`·`LCDTest`)이 서버와 직접 말한다 — 조회·지급, 설정 받기,
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

## 굽기

```bash
cd nfcProject/master    # 또는 single · slave · LCDTest
pio run                 # 빌드만
pio run -t upload       # 구워 넣기
pio device monitor      # 115200
```

포트는 번호가 아니라 **보드의 시리얼**로 찾는다(`find_port.py` · `monitor_port = hwgrep://…`) —
화면 보드와 리더 보드의 USB ID 가 같아서, 둘을 같이 꽂으면 자동 고르기가 엉뚱한 쪽을 잡는다.
보드를 바꾸면 그 두 자리의 MAC 을 함께 고칠 것.

## 서버와 맞물린 자리

고칠 때 funfunServer 쪽도 함께 봐야 하는 것들:

| 자리 | 서버의 짝 |
|---|---|
| `GET /api/talent/config/v2` 로 받는 설정 | `talent/talentConfigV2.js` — 기본값이 바뀌면 `config.h` 의 `BL_LEVEL` 등도 맞출 것 |
| `{"state":"config"}` 를 받으면 설정을 다시 받는다 | `talent/deviceHub.js` 의 `COMMANDS.config` — 값이 아니라 "다시 물어라" 만 온다 |
| 상태 알림(`buildState`)의 `attend`·`mode`·`note` | 관리 화면 상단 바의 기기 풍선(`NfcDisplayChip.vue`)이 그대로 그린다 |
| 사진·캐릭터 그림의 크기(`TALENT_PX`·`MOOD_ICON_PX`) | `talent/talentPhoto.js` 의 `DEVICE_SIZES`·`CHAR_PX` — 어긋나면 기기가 그림을 버린다 |
| RGB565 바이트 순서 | `talentArt.js` 가 `writeUInt16LE` 로 쓴다 — 한쪽만 바꾸면 그림이 지글거린다 |
