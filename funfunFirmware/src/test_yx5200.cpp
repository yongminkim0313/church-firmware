// ─────────────────────────────────────────────────────────────────
// YX5200(DFPlayer Mini) 하나만 달고 돌리는 시험판 — 배선과 SD 를 눈과 귀로 가른다.
//
// 본 판(main.cpp)으로는 이것을 할 수 없다. 그 판은 PN532 를 못 찾으면 그 자리에서
// 서고(치명 화면), 화면·와이파이가 먼저라 모듈이 조용할 때 그 까닭이 소리 쪽인지
// 그 앞에서 멈춘 것인지 가려지지 않는다. 그래서 소리만 남긴 판을 따로 굽는다.
//
//   pio run -e yx5200test -t upload && pio device monitor -e yx5200test
//   되돌리기:  pio run -t upload        (기본 env 가 본 판이다)
//
// ── 배선 (핀 번호는 config.h 의 값을 그대로 쓴다) ────────────────
//   ESP32 GPIO17 (TX) ──1kΩ──▶ 모듈 RX    ★ 이름이 엇갈린다 — TX 가 모듈 RX 로 간다
//   ESP32 GPIO18 (RX) ◀─────── 모듈 TX
//   VCC=5V · GND 공통 · VCC 옆에 100µF · SPK_1/SPK_2 에 꼬마 스피커
//
// ── 무엇을 보고 판단하나 ────────────────────────────────────────
//   · '←' 줄이 하나라도 뜨면 **모듈 TX → GPIO18** 이 살아 있다.
//   · 소리가 나면 **GPIO17 → 모듈 RX** 가 살아 있다.
//   · 소리는 나는데 '←' 가 없다 → RX 선만 빠졌다. 본 판에서는 다 읽었다는 알림(0x3D)을
//     못 받아 토막마다 5초를 기다리므로 말이 뚝뚝 끊긴다(sound.h 의 그 경고가 이 상태다).
//   · '←' 는 오는데 조용하다 → SD·스피커·음량을 본다(0x40 줄의 까닭을 읽을 것).
//   · 둘 다 없다 → 전원(5V·GND)이거나 두 선이 서로 바뀌었다. 시리얼 'w' 로 바꿔 본다.
//
// ★ 이 판은 일부러 **막고 기다린다**(waitAny). 본 판은 태깅이 늦지 않게 상태기계로
//   돌지만, 시험은 '보낸 것에 무엇이 돌아왔나' 가 한 줄로 보여야 한다.
// ─────────────────────────────────────────────────────────────────

#include <Arduino.h>

#include "config.h"  // DF_TX_PIN · DF_RX_PIN · SOUND_VOLUME — 본 판과 같은 값을 본다

#define DF Serial1

// 프레임: 7E FF 06 CMD ACK PARAM_H PARAM_L SUM_H SUM_L EF
#define C_PLAY_FOLDER 0x0F  // PARAM_H=폴더 PARAM_L=파일
#define C_VOLUME 0x06
#define C_RESET 0x0C
#define C_STOP 0x16
#define C_SOURCE 0x09  // PARAM_L=0x02 (microSD)
#define C_Q_STATUS 0x42
#define C_Q_VOLUME 0x43
#define C_Q_SD_FILES 0x48
#define C_Q_SD_TRACK 0x4C
#define C_Q_FOLDER_FILES 0x4E  // PARAM_L=폴더
#define C_Q_FOLDERS 0x4F

#define N_FINISH 0x3D  // SD 한 곡을 다 읽었다
#define N_READY 0x3F   // 모듈이 섰다
#define N_ERROR 0x40

static uint8_t pinTx = DF_TX_PIN;  // ESP32 TX → 모듈 RX
static uint8_t pinRx = DF_RX_PIN;  // ESP32 RX ← 모듈 TX
static uint8_t vol = SOUND_VOLUME;

static uint32_t rxBytes = 0;   // 모듈에서 온 바이트 — 0 이면 RX 선이 죽은 것이다
static uint32_t rxFrames = 0;  // 그 가운데 온전한 프레임
static uint32_t finishes = 0;  // 0x3D
static uint32_t errors = 0;    // 0x40

static uint8_t rx[10];
static uint8_t rxLen = 0;
static uint8_t lastCmd = 0;
static uint16_t lastParam = 0;

// 0x40 이 싣고 온 까닭. 번호마다 손볼 자리가 다르다 — 0x01 은 모듈 사정이고
// 0x06 은 카드 사정이다(sound.cpp 의 표와 같은 말로 둔다).
static const char *errText(uint8_t pl) {
  switch (pl) {
    case 0x01: return "모듈이 아직 준비 안 됨 — 틀려는 순간 전압이 주저앉았는지(VCC 옆 100uF)";
    case 0x02: return "모듈이 잠들어 있다";
    case 0x03: return "직렬 수신 오류 — 선이 잡음을 탔다(모듈 RX 에 1kOhm)";
    case 0x04: return "체크섬 안 맞음";
    case 0x05: return "트랙 번호가 범위를 넘음";
    case 0x06: return "그 번호의 파일이 SD 에 없다 — 폴더가 두 자리('01')인지 보세요";
    case 0x07: return "끼워 넣기 오류";
    case 0x08: return "SD 를 읽지 못했다 — 카드가 빠졌거나 상했다";
    default: return "모르는 까닭";
  }
}

static const char *cmdName(uint8_t c) {
  switch (c) {
    case 0x3A: return "저장소가 꽂혔다";
    case 0x3B: return "저장소가 빠졌다";
    case N_FINISH: return "다 읽었다";
    case N_READY: return "모듈이 섰다";
    case N_ERROR: return "오류";
    case 0x41: return "받았다(ACK)";
    case C_Q_STATUS: return "지금 상태";
    case C_Q_VOLUME: return "음량";
    case C_Q_SD_FILES: return "SD 파일 수";
    case C_Q_SD_TRACK: return "지금 트랙";
    case C_Q_FOLDER_FILES: return "그 폴더의 파일 수";
    case C_Q_FOLDERS: return "폴더 수";
    default: return "";
  }
}

static void openUart() {
  DF.end();
  DF.begin(9600, SERIAL_8N1, pinRx, pinTx);
  rxLen = 0;
  Serial.printf("[UART] 9600 8N1 — ESP32 TX=GPIO%u(→모듈 RX) · ESP32 RX=GPIO%u(←모듈 TX)\n",
                pinTx, pinRx);
}

static void drain() {
  while (DF.available()) DF.read();
  rxLen = 0;
}

static void send(uint8_t c, uint8_t ph, uint8_t pl) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, c, 0x00, ph, pl, 0x00, 0x00, 0xEF};
  const uint16_t sum = (uint16_t)(-(0xFF + 0x06 + c + 0x00 + ph + pl));
  f[7] = (uint8_t)(sum >> 8);
  f[8] = (uint8_t)(sum & 0xFF);
  DF.write(f, sizeof(f));
  DF.flush();
  Serial.printf("→ 0x%02X %-14s param %02X %02X\n", c, cmdName(c), ph, pl);
}

// 받은 바이트를 프레임으로 모아 찍는다. 합(SUM)은 보지 않는다 — 짝퉁 가운데 합을
// 엉터리로 보내면서도 알림은 제대로 보내는 것이 흔하다(본 판과 같은 판단).
static void feed() {
  while (DF.available()) {
    const uint8_t b = (uint8_t)DF.read();
    rxBytes++;
    if (rxLen == 0 && b != 0x7E) {
      Serial.printf("←(머리 아님) %02X\n", b);  // 보드율이 틀렸거나 선이 잡음을 탄다
      continue;
    }
    rx[rxLen++] = b;
    if (rxLen < sizeof(rx)) continue;
    rxLen = 0;
    if (rx[9] != 0xEF) {
      Serial.println("←(끝이 EF 가 아님) 버린다");
      continue;
    }
    rxFrames++;
    lastCmd = rx[3];
    lastParam = (uint16_t)((rx[5] << 8) | rx[6]);
    Serial.printf("← 0x%02X %-14s param %u (0x%04X)", lastCmd, cmdName(lastCmd), lastParam,
                  lastParam);
    if (lastCmd == N_ERROR) {
      errors++;
      Serial.printf("  ※ %s", errText(rx[6]));
    } else if (lastCmd == N_FINISH) {
      finishes++;
    }
    Serial.println();
  }
}

// a 나 b 가 올 때까지 기다린다. 돌아온 값은 만난 명령(못 만나면 0).
static uint8_t waitAny(uint8_t a, uint8_t b, uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    feed();
    if (lastCmd == a || (b && lastCmd == b)) return lastCmd;
    delay(5);
  }
  return 0;
}

static void initModule() {
  drain();
  lastCmd = 0;
  send(C_RESET, 0, 0x00);
  // 리셋 뒤 모듈이 다시 서는 데 1.5~3초가 걸리고 그 사이에 받은 명령은 사라진다 —
  // 하필 그것이 저장소 선택과 음량이라, 놓치면 그 뒤로 영영 조용하다.
  const bool ready = waitAny(N_READY, 0, 3000) == N_READY;
  if (!ready) Serial.println("※ '섰다'(0x3F) 가 안 왔다 — 안 보내는 짝퉁도 있다. 그대로 계속한다");
  delay(1500);  // 정착
  send(C_SOURCE, 0, 0x02);
  delay(80);
  send(C_VOLUME, 0, vol);
  delay(80);
  feed();
}

static void help() {
  Serial.println();
  Serial.println("── 시리얼 명령 ──────────────────────────────────");
  Serial.println("  t  자동 시험 한 바퀴 (리셋 → SD → 음량 → 파일 수 → /01/001 재생)");
  Serial.println("  1  /01/001 재생 (고정 문구 '카드를 올려 주세요')");
  Serial.println("  2  /02/001 재생 (숫자 토막)");
  Serial.println("  3  /03/001 재생 (0~100 통째 — 001 이 '영')");
  Serial.println("  n  /01/ 토막을 하나씩 차례로 (001~013)");
  Serial.println("  + -  음량 (0~30)");
  Serial.println("  s  멈춤   r  리셋+초기화   q  상태 물어보기   f  폴더별 파일 수");
  Serial.println("  w  RX·TX 핀 바꿔 끼우기 (선을 거꾸로 꽂았을 때)");
  Serial.println("  d  지금까지의 셈   h  이 도움말");
  Serial.println("─────────────────────────────────────────────────");
}

static void report() {
  Serial.printf("[셈] 받은 바이트 %lu · 프레임 %lu · 다 읽었다 %lu · 오류 %lu · 음량 %u\n",
                (unsigned long)rxBytes, (unsigned long)rxFrames, (unsigned long)finishes,
                (unsigned long)errors, vol);
  if (rxBytes == 0)
    Serial.printf("  ※ 모듈에서 한 바이트도 안 왔다 — 모듈 TX → GPIO%u 선과 공통 GND 를 보세요\n",
                  pinRx);
}

static void selfTest() {
  Serial.println();
  Serial.println("══ 자동 시험 ══════════════════════════════════");
  const uint32_t beforeBytes = rxBytes;
  initModule();

  lastCmd = 0;
  send(C_Q_SD_FILES, 0, 0);
  const bool gotFiles = waitAny(C_Q_SD_FILES, N_ERROR, 1500) == C_Q_SD_FILES;
  const uint16_t files = gotFiles ? lastParam : 0;

  // /01/ 의 **마지막** 토막까지 닿나 — 걸어 보고 판단한다.
  // ★ 세어 묻지 않는 까닭: 폴더 수(0x4F)는 이 모듈이 오류로 답하고, 폴더 안의 파일 수
  //   (0x4E)는 13 개가 멀쩡히 든 카드에 2 를 돌려준다(짝퉁마다 다르다). 두 수를 그대로
  //   찍으면 아무 탈도 없는 자리를 고장으로 읽게 된다. 없는 파일을 걸면 0x40(0x06 —
  //   그 번호의 파일이 없다)이 곧바로 오므로, 그 침묵이 '닿는다' 는 증거다.
  lastCmd = 0;
  send(C_PLAY_FOLDER, 1, 13);
  const bool last13 = waitAny(N_ERROR, 0, 600) != N_ERROR;
  send(C_STOP, 0, 0);
  delay(150);
  feed();

  Serial.println("— /01/001 을 틀어 본다 (스피커에서 '카드를 올려 주세요' 가 나와야 한다)");
  lastCmd = 0;
  finishes = 0;
  send(C_PLAY_FOLDER, 1, 1);
  // 걸린 시간을 잰다. '다 읽었다' 는 다 **읽고 나서** 오므로 토막 길이만큼 걸려야 한다 —
  // 곧바로 돌아오면 소리는 안 내고 넘어간 것이다(귀로 듣지 않고도 가려지는 자리다).
  const uint32_t playFrom = millis();
  const uint8_t done = waitAny(N_FINISH, N_ERROR, 6000);
  const uint32_t took = millis() - playFrom;

  const bool rxAlive = rxBytes > beforeBytes;
  Serial.println();
  Serial.println("══ 판정 ═══════════════════════════════════════");
  Serial.printf("  RX 선 (모듈 TX → GPIO%u) : %s\n", pinRx,
                rxAlive ? "살아 있다" : "★ 아무것도 안 온다");
  if (gotFiles) Serial.printf("  SD 파일 수              : %u 개 (voices/ 를 다 넣었으면 128)\n", files);
  Serial.printf("  /01/013 (마지막 토막)   : %s\n",
                last13 ? "닿는다 — 폴더 이름이 두 자리로 제대로 들어갔다" : "★ 그 파일이 없다");
  if (done == N_FINISH && took >= 150)
    Serial.printf("  /01/001                 : %lu ms 만큼 읽었다 — TX 선·SD·재생 모두 정상\n",
                  (unsigned long)took);
  else if (done == N_FINISH)
    Serial.printf("  /01/001                 : ★ %lu ms 만에 끝났다 — 소리를 내지 않고 넘어갔다\n",
                  (unsigned long)took);
  else if (done == N_ERROR)
    Serial.println("  /01/001                 : ★ 오류 — 위 0x40 줄의 까닭을 보세요");
  else
    Serial.println("  /01/001                 : ★ '다 읽었다' 가 안 왔다 (소리가 났는지 들어 보세요)");

  if (!rxAlive) {
    Serial.printf("  → 먼저 볼 것: 모듈 TX ↔ GPIO%u · 공통 GND · 모듈 VCC 5V.\n", pinRx);
    Serial.println("     두 선을 거꾸로 꽂았을 수도 있다 — 'w' 로 바꿔 끼워 다시 't'.");
  } else if (gotFiles && files == 0) {
    Serial.println("  → SD 가 비었거나 못 읽는다. FAT32 · 폴더 이름은 두 자리('01').");
    Serial.println("     voices/ 의 01·02·03 을 카드 뿌리에 그대로 복사한다.");
  } else if (!last13) {
    Serial.println("  → SD 는 읽히는데 /01/013 이 없다. 폴더 이름을 '1' 이 아니라 '01' 로,");
    Serial.println("     파일 이름은 세 자리('013')로 둘 것 — voices/01 을 그대로 복사한다.");
  } else if (done == N_FINISH && took >= 150) {
    Serial.println("  → 소리까지 들렸으면 본 판으로 넘어가도 된다:  pio run -t upload");
  }
  Serial.println("═══════════════════════════════════════════════");
  help();
}

void setup() {
  Serial.begin(115200);
  // USB CDC 가 설 때까지 — 안 기다리면 첫 줄들이 통째로 사라진다.
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);

  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║  YX5200 (DFPlayer Mini) 시험판                ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.printf("배선: GPIO%u(ESP32 TX) ──1kOhm──▶ 모듈 RX   |   GPIO%u(ESP32 RX) ◀── 모듈 TX\n",
                pinTx, pinRx);
  Serial.println("      VCC=5V · GND 공통 · SPK_1/SPK_2 에 꼬마 스피커");

  openUart();
  // 전원이 들어온 뒤 모듈이 스스로 서는 데 1.5초쯤 걸린다. 더 일찍 말을 걸면 그 명령은 사라진다.
  Serial.println("모듈이 설 때까지 1.5초 기다린다…");
  const uint32_t w0 = millis();
  while (millis() - w0 < 1500) {
    feed();
    delay(5);
  }
  selfTest();
}

void loop() {
  feed();

  if (!Serial.available()) {
    delay(5);
    return;
  }
  const char c = (char)Serial.read();
  switch (c) {
    case 't': selfTest(); break;
    case '1': send(C_PLAY_FOLDER, 1, 1); break;
    case '2': send(C_PLAY_FOLDER, 2, 1); break;
    case '3': send(C_PLAY_FOLDER, 3, 1); break;
    case 'n': {
      static uint8_t t = 0;
      t = (uint8_t)(t % 13 + 1);  // 폴더 01 은 001~013
      Serial.printf("[다음] /01/%03u\n", t);
      send(C_PLAY_FOLDER, 1, t);
      break;
    }
    case '+':
      if (vol < 30) vol++;
      Serial.printf("[음량] %u\n", vol);
      send(C_VOLUME, 0, vol);
      break;
    case '-':
      if (vol > 0) vol--;
      Serial.printf("[음량] %u\n", vol);
      send(C_VOLUME, 0, vol);
      break;
    case 's': send(C_STOP, 0, 0); break;
    case 'r': initModule(); break;
    case 'q':
      send(C_Q_STATUS, 0, 0);
      delay(150);
      feed();
      send(C_Q_VOLUME, 0, 0);
      delay(150);
      feed();
      send(C_Q_SD_FILES, 0, 0);
      delay(150);
      feed();
      send(C_Q_SD_TRACK, 0, 0);
      delay(150);
      feed();
      break;
    case 'f':
      for (uint8_t fo = 1; fo <= 3; fo++) {
        Serial.printf("[폴더 %02u]\n", fo);
        send(C_Q_FOLDER_FILES, 0, fo);
        delay(200);
        feed();
      }
      break;
    case 'w': {
      const uint8_t tmp = pinTx;
      pinTx = pinRx;
      pinRx = tmp;
      Serial.println("[핀] RX·TX 를 바꿔 끼운다");
      openUart();
      initModule();
      break;
    }
    case 'd': report(); break;
    case 'h': help(); break;
    default: break;  // 줄바꿈 따위는 흘린다
  }
}
