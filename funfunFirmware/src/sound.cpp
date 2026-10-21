// 소리 — DFPlayer Mini(YX5200) 를 UART 로 몰고, 토막을 줄에 세워 차례로 읽힌다.
// 무엇을 왜 읽는지는 sound.h 머리말에 있다. 이 파일은 그것을 어떻게 하는지만 담는다.

#include "sound.h"

#if SOUND_ENABLE

// ── 모듈과 말하는 법 ─────────────────────────────────────────────
// 프레임은 열 바이트로 고정이다:
//   7E FF 06 CMD ACK PARAM_H PARAM_L SUM_H SUM_L EF
// 합(SUM)은 FF 부터 PARAM_L 까지를 더해 부호를 뒤집은 16비트다.
//
// ★ ACK(응답 요구)를 0 으로 둔다. 1 로 두면 명령마다 0x41 을 기다려야 하는데, 그
//   기다림이 본 고리 안에 들어오면 태깅이 그만큼 늦는다. 대신 **다 읽었다는 알림**
//   (0x3D)은 요구하지 않아도 저절로 오므로, 다음 토막을 밀어 넣는 데는 그것을 쓴다.
// ★ 받은 프레임의 합은 보지 않는다 — YX5200 짝퉁 가운데 합을 엉터리로 보내면서도
//   알림은 제대로 보내는 것이 흔하다. 머리(7E)와 끝(EF)만 보고 받는다.
#define DF_SERIAL Serial1

#define DF_CMD_PLAY_FOLDER 0x0F  // PARAM_H=폴더 PARAM_L=파일
#define DF_CMD_VOLUME 0x06       // PARAM_L=0~30
#define DF_CMD_RESET 0x0C
#define DF_CMD_STOP 0x16
#define DF_CMD_SOURCE 0x09  // PARAM_L=0x02 (microSD)

#define DF_NOTIFY_FINISH 0x3D  // SD 한 곡을 다 읽었다(PARAM 에 파일 번호)
#define DF_NOTIFY_READY 0x3F   // 모듈이 섰다(PARAM 에 어느 저장소가 꽂혔는지)
#define DF_NOTIFY_ERROR 0x40   // PARAM_L 에 까닭(0x06 = 그 파일이 없다)

// 전원이 들어온 뒤 모듈이 스스로 설 때까지. 더 일찍 말을 걸면 그 명령은 사라진다.
#define DF_BOOT_MS 1500
// 리셋을 보낸 뒤 '섰다'(0x3F)를 기다리는 한도. 안 보내 주는 짝퉁이 있어 시간으로도 넘어간다.
#define DF_RESET_MS 2000
// 명령과 명령 사이 쉼 — 몰아 보내면 뒤의 것을 흘린다.
#define DF_CMD_GAP_MS 50
// 한 토막이 이만큼 지나도 '다 읽었다' 가 안 오면 끝난 것으로 본다. 알림을 흘렸거나
// 파일이 없는 경우에 줄이 영영 멈춰 서지 않게 하는 빗장이다. 가장 긴 토막이 1.5초쯤이다.
#define DF_CLIP_MAX_MS 5000

// ── 폴더 02 — 숫자 토막 ─────────────────────────────────────────
// 한자말 수 읽기(일·이·삼…)로 이어 붙인다. 번호는 tools/make_voices.sh 와 같다.
#define SNDN_FOLDER 2
#define SNDN_ZERO 1    // 영
#define SNDN_TEN 11    // 십
#define SNDN_HUNDRED 12  // 백
#define SNDN_THOUSAND 13  // 천
#define SNDN_MAN 14    // 만
#define SNDN_DIGIT(d) ((uint8_t)((d) + 1))  // 0→1(영) 1→2(일) … 9→10(구)

#define SND_FOLDER 1  // 고정 문구

// ── 줄 ───────────────────────────────────────────────────────────
// 12345 는 '만·이·천·삼·백·사·십·오' 로 여덟 토막이다. 앞뒤('출석 완료'·'포인트')까지
// 넉넉히 들어가게 둔다. 넘치면 뒤를 버린다 — 읽다 마는 쪽이 엉뚱한 수를 읽는 것보다 낫다.
#define SND_QUEUE_MAX 24

struct SndItem {
  uint8_t folder;
  uint8_t track;
};
static SndItem queue[SND_QUEUE_MAX];
static uint8_t qHead = 0, qCount = 0;
static SndItem cur = {0, 0};  // 지금 읽고 있는 토막 — 오류가 어느 파일에서 났는지 찍으려고 들고 있다

enum SndState : uint8_t {
  ST_BOOT,   // 모듈이 설 때까지 기다린다
  ST_RESET,  // 리셋을 보내고 '섰다' 를 기다린다
  ST_SOURCE, // 저장소를 SD 로
  ST_VOLUME, // 음량
  ST_READY,  // 조용하다 — 줄에 있으면 바로 밀어 넣는다
  ST_PLAY,   // 한 토막을 읽고 있다
};
static SndState state = ST_BOOT;
static uint32_t stepAt = 0;   // 이 상태로 들어온 때(또는 마지막 명령을 보낸 때)
static uint32_t playAt = 0;   // 지금 토막을 밀어 넣은 때
static uint32_t endAt = 0;    // 앞 토막이 끝난 때 — 쉼(SOUND_GAP_MS)을 재는 데 쓴다
static bool readySeen = false;  // 0x3F 를 받았나
static uint8_t volume = SOUND_VOLUME;
static uint32_t playedCount = 0;
static uint32_t missedCount = 0;  // 알림 없이 빗장(DF_CLIP_MAX_MS)으로 넘어간 수

static uint8_t rx[10];
static uint8_t rxLen = 0;

// ── 프레임 보내기 ────────────────────────────────────────────────
static void dfCmd(uint8_t cmd, uint8_t ph, uint8_t pl) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, ph, pl, 0x00, 0x00, 0xEF};
  const uint16_t sum = (uint16_t)(-(0xFF + 0x06 + cmd + 0x00 + ph + pl));
  f[7] = (uint8_t)(sum >> 8);
  f[8] = (uint8_t)(sum & 0xFF);
  DF_SERIAL.write(f, sizeof(f));
  stepAt = millis();
}

// ── 프레임 받기 ──────────────────────────────────────────────────
static void dfFrame(uint8_t cmd, uint8_t ph, uint8_t pl) {
  switch (cmd) {
    case DF_NOTIFY_READY:
      readySeen = true;
      Serial.printf("[소리] 모듈이 섰다 (저장소 0x%02X)\n", pl);
      break;
    case DF_NOTIFY_FINISH:
      if (state == ST_PLAY) {
        state = ST_READY;
        endAt = millis();
      }
      break;
    case DF_NOTIFY_ERROR:
      // 가장 흔한 것은 0x06 — 그 번호의 파일이 SD 에 없다. 토막을 늘리고 SD 에 복사하는
      // 것을 잊으면 여기로 온다. 줄은 그대로 밀고 나간다(다음 토막은 있을 수 있다).
      Serial.printf("[소리] 오류 0x%02X — /%02d/%03d 가 SD 에 있는지 보세요\n", pl, cur.folder,
                    cur.track);
      if (state == ST_PLAY) {
        state = ST_READY;
        endAt = millis();
      }
      break;
    default:
      break;
  }
  (void)ph;
}

static void dfFeed() {
  while (DF_SERIAL.available()) {
    const uint8_t b = (uint8_t)DF_SERIAL.read();
    if (rxLen == 0 && b != 0x7E) continue;  // 머리를 못 만났으면 버린다
    rx[rxLen++] = b;
    if (rxLen < sizeof(rx)) continue;
    rxLen = 0;
    if (rx[9] == 0xEF) dfFrame(rx[3], rx[5], rx[6]);
  }
}

// ── 줄에 세우기 ──────────────────────────────────────────────────
static void enq(uint8_t folder, uint8_t track) {
  if (qCount >= SND_QUEUE_MAX) return;  // 넘치면 뒤를 버린다
  const uint8_t i = (uint8_t)((qHead + qCount) % SND_QUEUE_MAX);
  queue[i].folder = folder;
  queue[i].track = track;
  qCount++;
}

static void qClear() {
  qHead = 0;
  qCount = 0;
}

// 한자말로 수를 읽는다. 1 은 자리 이름만 읽는다 — '일백' 이 아니라 '백' 이다.
static void enqGroup(long n) {  // 1~9999
  const int th = (int)(n / 1000), hu = (int)((n / 100) % 10), te = (int)((n / 10) % 10),
            on = (int)(n % 10);
  if (th) {
    if (th > 1) enq(SNDN_FOLDER, SNDN_DIGIT(th));
    enq(SNDN_FOLDER, SNDN_THOUSAND);
  }
  if (hu) {
    if (hu > 1) enq(SNDN_FOLDER, SNDN_DIGIT(hu));
    enq(SNDN_FOLDER, SNDN_HUNDRED);
  }
  if (te) {
    if (te > 1) enq(SNDN_FOLDER, SNDN_DIGIT(te));
    enq(SNDN_FOLDER, SNDN_TEN);
  }
  if (on) enq(SNDN_FOLDER, SNDN_DIGIT(on));
}

// 0 ~ 99,999,999. 그보다 큰 수는 읽지 않는다(토막을 더 굽지 않고 조용히 지나간다 —
// 포인트가 억을 넘는 일은 없고, 넘었다면 읽는 쪽이 아니라 서버를 봐야 한다).
static void enqNumber(long v) {
  if (v < 0) v = -v;  // 부호는 읽지 않는다 — 소리로 읽는 자리는 지급(+)뿐이다
  if (v == 0) {
    enq(SNDN_FOLDER, SNDN_ZERO);
    return;
  }
  if (v > 99999999L) return;
  const long man = v / 10000, rest = v % 10000;
  if (man) {
    if (man > 1) enqGroup(man);
    enq(SNDN_FOLDER, SNDN_MAN);
  }
  if (rest) enqGroup(rest);
}

// ── 바깥에서 부르는 것들 ─────────────────────────────────────────
void soundBegin() {
  DF_SERIAL.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
  state = ST_BOOT;
  stepAt = millis();
  readySeen = false;
  qClear();
  // 여기서는 찍지 않는다 — setup 이 USB CDC 를 기다리기 **전에** 부르는 자리라
  // 그 줄이 버려진다(Serial.setTxTimeoutMs(0)). 배선 요약은 setup 이 찍는다.
}

void soundLoop() {
  dfFeed();
  const uint32_t now = millis();

  switch (state) {
    case ST_BOOT:
      // 전원이 들어온 뒤 모듈이 설 때까지. 그 전에 보낸 명령은 사라진다.
      if ((int32_t)(now - stepAt) < DF_BOOT_MS) return;
      dfCmd(DF_CMD_RESET, 0, 0);
      state = ST_RESET;
      return;
    case ST_RESET:
      // '섰다'(0x3F)가 오면 바로, 안 보내 주는 짝퉁이면 시간으로 넘어간다.
      if (!readySeen && (int32_t)(now - stepAt) < DF_RESET_MS) return;
      dfCmd(DF_CMD_SOURCE, 0, 0x02);  // microSD
      state = ST_SOURCE;
      return;
    case ST_SOURCE:
      if ((int32_t)(now - stepAt) < DF_CMD_GAP_MS) return;
      dfCmd(DF_CMD_VOLUME, 0, volume);
      state = ST_VOLUME;
      return;
    case ST_VOLUME:
      if ((int32_t)(now - stepAt) < DF_CMD_GAP_MS) return;
      state = ST_READY;
      endAt = now;
      return;
    case ST_PLAY:
      // 알림을 흘렸거나 파일이 없어 아무 말도 안 온 경우 — 줄이 영영 멈춰 서지 않게.
      if ((int32_t)(now - playAt) > DF_CLIP_MAX_MS) {
        missedCount++;
        state = ST_READY;
        endAt = now;
      }
      return;
    case ST_READY:
      break;
  }

  if (!qCount) return;
  if ((int32_t)(now - endAt) < SOUND_GAP_MS) return;  // 토막 사이 쉼

  cur = queue[qHead];
  dfCmd(DF_CMD_PLAY_FOLDER, cur.folder, cur.track);
  qHead = (uint8_t)((qHead + 1) % SND_QUEUE_MAX);
  qCount--;
  playAt = now;
  playedCount++;
  state = ST_PLAY;
}

void soundPump(uint32_t ms) {
  const uint32_t until = millis() + ms;
  do {
    soundLoop();
    delay(5);
  } while ((int32_t)(millis() - until) < 0);
}

void soundStop() {
  qClear();
  if (state == ST_PLAY) {
    dfCmd(DF_CMD_STOP, 0, 0);
    state = ST_READY;
    endAt = millis();
  }
}

void soundSay(uint8_t clip) {
  if (!clip) return;
  soundStop();
  enq(SND_FOLDER, clip);
}

void soundVolume(uint8_t vol) {
  if (vol > 30) vol = 30;
  volume = vol;
  // 아직 서는 중이면 보내지 않는다 — ST_VOLUME 이 어차피 이 값으로 보낸다.
  if (state == ST_READY || state == ST_PLAY) dfCmd(DF_CMD_VOLUME, 0, volume);
}

// ── 안내문 한 줄 → 토막 ──────────────────────────────────────────
// 화면에 뜬 글자를 그대로 받아 어느 토막을 읽을지 고른다. 소리 번호를 부르는 쪽마다
// 들고 다니지 않으려는 것이다 — 허브(서버)가 밀어 준 안내문도 같은 길로 읽힌다.
// ★ 여기 글자는 main.cpp 가 화면에 그리는 글자와 **같아야** 한다. 한쪽만 고치면 그
//   문구는 조용해진다(틀리게 읽지는 않는다).
static const char NOTE_ATTEND_PREFIX[] = "출석 완료 +";

struct NoteClip {
  const char *note;
  uint8_t clip;
};
static const NoteClip NOTE_CLIPS[] = {
    {"이미 출석했어요", SND_ALREADY},
    {"등록되지 않은 키링", SND_UNKNOWN_CARD},
    {"서버에 물어보지 못했어요", SND_NO_SERVER},
};

void soundSayNote(const char *note, long points) {
  soundStop();
  if (!note) note = "";

  // 지급됐다 — "출석 완료 +5P" 에서 수를 떼어 [출석 완료][오][포인트] 로 읽는다.
  const size_t pre = sizeof(NOTE_ATTEND_PREFIX) - 1;
  if (!strncmp(note, NOTE_ATTEND_PREFIX, pre)) {
    enq(SND_FOLDER, SND_ATTEND_OK);
    enqNumber(atol(note + pre));
    enq(SND_FOLDER, SND_POINT);
    return;
  }

  for (size_t i = 0; i < sizeof(NOTE_CLIPS) / sizeof(NOTE_CLIPS[0]); i++) {
    if (!strcmp(note, NOTE_CLIPS[i].note)) {
      enq(SND_FOLDER, NOTE_CLIPS[i].clip);
      return;
    }
  }

  // 안내문 칸이 비었다 = 그냥 조회다. 화면은 '잔여 포인트' 와 숫자를 띄우고 있다.
  if (!note[0]) {
#if SOUND_SAY_BALANCE
    enq(SND_FOLDER, SND_BALANCE);
    enqNumber(points);
#endif
    return;
  }

  // 모르는 안내문 — 서버가 보낸 거절 안내다('화요일은 출석을 안 받아요' 따위).
  // 교회 설정에 딸려 글자가 바뀌어 미리 구울 수 없다. 지금은 조용히 지나간다.
  // ★ 서버가 합성해 보내 주게 되면 **이 자리**에서 그 소리를 틀면 된다(sound.h 의
  //   '다음 걸음'). 화면에는 이미 그 글이 떠 있으므로, 소리만 비는 것이지 아이가
  //   까닭을 모르는 것은 아니다.
  Serial.printf("[소리] 읽을 토막이 없다: \"%s\"\n", note);
}

// ── 시험·상태 ────────────────────────────────────────────────────
void soundTestNext() {
  static uint8_t clip = SND_IDLE_PROMPT;
  // 숫자까지 한 번씩 들어 보게, 고정 문구를 다 돌면 수 하나를 읽는다.
  if (clip > SND_PEOPLE) {
    Serial.println("[소리] 시험 — 숫자 1234");
    soundStop();
    enqNumber(1234);
    clip = SND_IDLE_PROMPT;
    return;
  }
  Serial.printf("[소리] 시험 — /%02d/%03d\n", SND_FOLDER, clip);
  soundSay(clip);
  clip++;
}

void soundReport() {
  static const char *NAMES[] = {"준비중", "리셋중", "저장소", "음량", "조용", "읽는중"};
  Serial.printf("[소리] %s · 줄 %u토막 · 읽은 토막 %lu · 알림 못받음 %lu · 음량 %d/30\n",
                NAMES[state], qCount, (unsigned long)playedCount, (unsigned long)missedCount,
                volume);
}

#else  // SOUND_ENABLE 0 — 모듈 없이 구울 때. 부르는 자리를 그대로 두고 아무 일도 안 한다.

void soundBegin() {}
void soundLoop() {}
void soundPump(uint32_t ms) { delay(ms); }
void soundSay(uint8_t) {}
void soundSayNote(const char *, long) {}
void soundStop() {}
void soundVolume(uint8_t) {}
void soundTestNext() { Serial.println("[소리] 꺼 두었습니다 (SOUND_ENABLE 0)"); }
void soundReport() { Serial.println("[소리] 꺼 두었습니다 (SOUND_ENABLE 0)"); }

#endif
