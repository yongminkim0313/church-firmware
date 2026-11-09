# JC3248W535 보드 드라이버 (vendored)

가이션 JC3248W535C 보드용 디스플레이(AXS15231B / QSPI) + 터치(I2C) 드라이버.

- 출처: https://github.com/me-processware/JC3248W535-Driver (MIT, Processware)
- 가져온 시점: 2026-10-01

## 왜 lib_deps 가 아니라 lib/ 에 직접 넣었나

1. PlatformIO 레지스트리에 등록된 라이브러리가 아니다 (git URL 의존이 되는데, 업스트림
   HEAD 가 바뀌거나 저장소가 사라지면 빌드가 깨진다).
2. 핀 배치·회전·밝기 등을 이 보드에 맞춰 손볼 가능성이 높다.

`lib/` 안의 디렉토리는 PlatformIO 가 자동으로 라이브러리로 인식해 컴파일한다.
`#include <JC3248W535.h>` 로 쓴다.

## 핀 배치

| 용도 | GPIO |
|---|---|
| LCD CS / SCK | 45 / 47 |
| LCD D0~D3 | 21 / 48 / 40 / 39 |
| 백라이트 | 1 |
| 터치 I2C SDA / SCL | 4 / 8 (주소 `0x3B`) |

## 주의

- **PSRAM 필수.** 320x480x2바이트 = 307KB 프레임버퍼를 PSRAM 에 잡는다.
- 그린 뒤 반드시 `display.flush()` 를 호출해야 화면에 나간다 (QSPI 는 부분 갱신 불가).
- `TFT_eSPI` 는 이 패널을 구동하지 못한다.
