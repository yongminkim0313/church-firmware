// 소리 — DFPlayer Mini(YX5200) 를 UART 로 몰고, 토막을 줄에 세워 차례로 읽힌다.
// 무엇을 왜 읽는지는 sound.h 머리말에 있다. 이 파일은 그것을 어떻게 하는지만 담는다.
//
// ★ funfunFirmware/src/sound.cpp 와 **거의 같은 파일**이다. 다른 데는 한 곳 —
//   아래 NOTE_CLIPS 표가 더 길다. 저쪽은 소리를 거는 곳이 번호(SndClip)를 알고
//   부를 수 있지만, 이쪽은 디스플레이가 **글자만** 보내 오기 때문이다(sound.h 머리말).
//   한쪽을 고치면 다른 쪽도 보라 — 특히 번호·폴더 짜임새는 같은 SD 카드를 쓴다.

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
// 리셋을 보낸 뒤 **반드시** 기다리는 시간. 모듈이 다시 서는 데 1.5~3초가 걸리고 그 사이에
// 받은 명령은 사라진다 — 하필 그것이 '저장소를 SD 로'(0x09)와 음량이라, 놓치면 되돌릴
// 자리가 없다(둘 다 아래 상태 전이 **한 번**에만 나가고 다시 보내는 곳이 없다).
#define DF_RESET_SETTLE_MS 1500
// 그 뒤로 '섰다'(0x3F)를 더 기다리는 한도. 안 보내 주는 짝퉁이 있어 시간으로도 넘어간다.
// ★ 2000 에서 3000 으로 늘렸다 — 한도가 모듈이 서는 시간보다 짧으면 깃발을 제대로
//   내려 두어도 결국 재부팅 중에 0x09 를 보내게 된다(늘어나는 것은 부팅 안내뿐이다).
#define DF_RESET_MS 3000
// 명령과 명령 사이 쉼 — 몰아 보내면 뒤의 것을 흘린다.
#define DF_CMD_GAP_MS 50
// 한 토막이 이만큼 지나도 '다 읽었다' 가 안 오면 끝난 것으로 본다. 알림을 흘렸거나
// 파일이 없는 경우에 줄이 영영 멈춰 서지 않게 하는 빗장이다. 가장 긴 토막이 1.5초쯤이다.
#define DF_CLIP_MAX_MS 5000

// ── 숫자 — 두 갈래 ───────────────────────────────────────────────
// 폴더 03 : 0~100 을 **한 토막씩** 통째로. 번호가 '수 + 1' 이다(0→001 … 100→101).
//           소리로 나가는 수는 거의 다 이 구간이다 — 출석 포인트와 웬만한 잔액.
// 폴더 02 : 조립용 토막(일·이·삼…십·백·천·만). 100 을 넘는 잔액에만 쓴다.
//
// ★ 왜 둘인가 — 이어 붙이면 '오십칠' 이 '오 · 십 · 칠' 로 띄엄띄엄 들린다(토막마다
//   앞뒤 쉼이 붙어 세 토막이면 2.3초, 통째면 1.0초다). 그래서 쓰는 구간만 통째로 굽고,
//   100 을 넘는 수는 **되돌아갈 자리**로 조립을 남긴다 — 아이들이 모아 둔 잔액은 네
//   자리까지 가는데(1205·12345), 그때 소리가 통째로 비면 그게 더 나쁘다.
#define SNDN_WHOLE_FOLDER 3
#define SNDN_WHOLE_MAX 100  // 여기까지는 통째로 — 넘으면 아래 조립으로 간다
#define SNDN_FOLDER 2
// 이 폴더에서 실제로 쓰는 것은 **이~구 · 백 · 천 · 만**뿐이다. 영(001)·일(002)·십(011)은
// 폴더 03 이 통째로 맡아 쓸 일이 없지만 그 자리를 비우지 않는다 — 아래 SNDN_DIGIT 의
// '수 + 1' 이 그 칸들을 세고 있다.
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
  ST_RESET,  // '섰다' 를 기다린다 — 켤 때의 리셋 뒤, 그리고 모듈이 제멋대로 다시 섰을 때
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
// 모듈이 제멋대로 다시 선 수. 0 이 아니면 전원을 봐야 한다 — 틀려는 순간 전압이
// 주저앉는다는 뜻이고, 그때마다 한마디가 끊긴다(모듈 VCC 옆 100µF · 5V 선 굵기).
static uint32_t rebootCount = 0;

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
static void qClear();  // 아래 '줄에 세우기' 에 있다 — 다시 선 모듈을 치울 때 쓴다

// 0x40 이 싣고 온 까닭. 번호마다 손볼 자리가 달라서, 한 문장으로 뭉쳐 두면 엉뚱한 데를
// 뒤지게 된다 — 0x01 은 모듈 사정이고 0x06 은 카드 사정이다.
static const char *dfErrText(uint8_t pl) {
  switch (pl) {
    case 0x01:
      return "모듈이 아직 준비 안 됨 — 틀려는 순간 전압이 주저앉았는지(VCC 옆 100µF) 보세요";
    case 0x02: return "모듈이 잠들어 있다";
    case 0x03: return "직렬 수신 오류 — 선이 잡음을 탔다(모듈 RX 에 1kΩ)";
    case 0x04: return "체크섬 안 맞음";
    case 0x05: return "트랙 번호가 범위를 넘음";
    case 0x06: return "그 번호의 파일이 SD 에 없다 — 폴더가 두 자리('01')인지 보세요";
    case 0x07: return "끼워 넣기 오류";
    case 0x08: return "SD 를 읽지 못했다 — 카드가 빠졌거나 상했다";
    default: return "모르는 까닭";
  }
}

static void dfFrame(uint8_t cmd, uint8_t ph, uint8_t pl) {
  switch (cmd) {
    case DF_NOTIFY_READY:
      readySeen = true;
      Serial.printf("[소리] 모듈이 섰다 (저장소 0x%02X)\n", pl);
      // ★ 이 줄이 **켤 때가 아니라 한참 뒤에** 오면 모듈이 제멋대로 다시 선 것이다 —
      //   거의 틀려는 순간 전압이 주저앉아서다. 다시 선 모듈은 **저장소 선택과 음량을
      //   잊는다.** 그대로 두면 그 뒤로 영영 조용하다 — 0x09 와 음량을 보내는 곳이
      //   아래 상태 전이 한 번뿐이라, ESP 를 다시 켜야 소리가 돌아온다.
      //   그래서 초기화 자리로 되돌린다. **리셋은 보내지 않는다** — 모듈은 방금 섰고,
      //   정착 시간만 지켜 0x09·음량을 다시 보내면 된다.
      //   줄은 비운다. 끊긴 그 한마디는 이미 중간에서 잘렸고, 다시 세워 두면 전압이
      //   계속 주저앉는 상자에서 같은 말을 끝없이 더듬는다 — 다음 태깅부터 제대로
      //   읽히는 쪽이 낫다.
      if (state == ST_READY || state == ST_PLAY) {
        rebootCount++;
        Serial.println("[소리] 모듈이 다시 섰다 — 저장소·음량을 다시 보낸다 (전원을 보세요)");
        qClear();
        state = ST_RESET;
        stepAt = millis();
      }
      break;
    case DF_NOTIFY_FINISH:
      if (state == ST_PLAY) {
        state = ST_READY;
        endAt = millis();
      }
      break;
    case DF_NOTIFY_ERROR:
      // 줄은 그대로 밀고 나간다(다음 토막은 있을 수 있다).
      // ★ 0x01 뒤에 0x06 이 따라오는 것은 **카드 탈이 아니다** — 초기화가 안 끝난 모듈은
      //   파일 색인이 없어 무엇을 물어도 '없다' 고 한다. 그때는 곧 0x3F 가 따라오고,
      //   위 READY 가 초기화를 다시 돌린다.
      Serial.printf("[소리] 오류 0x%02X (/%02d/%03d) — %s\n", pl, cur.folder, cur.track,
                    dfErrText(pl));
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

// 받아 둔 바이트를 통째로 버린다 — 리셋을 보내기 **전에** 묵은 알림을 치우는 데 쓴다.
static void dfDrain() {
  while (DF_SERIAL.available()) DF_SERIAL.read();
  rxLen = 0;
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

// 0~100 한 토막. 번호가 '수 + 1' 이다(0→001 … 100→101).
static void enqWhole(long v) { enq(SNDN_WHOLE_FOLDER, (uint8_t)(v + 1)); }

// 1~9999 를 읽는다(100 을 넘는 수에만 온다).
//
// ★ **끝의 두 자리는 쪼개지 않는다** — 폴더 03 에서 통째로 가져온다.
//     125  → [백][이십오]          ('백·이·십·오' 네 토막이 아니라 두 토막)
//     225  → [이][백][이십오]
//     1205 → [천][이][백][오]
//     2026 → [이][천][이십육]
//   앞자리(천·백)만 토막을 세우고 뒤는 한 입에 읽는 것이, 사람이 읽는 꼴에도 가깝다.
//   십 자리를 따로 세우지 않으니 폴더 02 의 '십' 토막은 이제 쓰이지 않는다.
//
// 1 은 자리 이름만 읽는다 — '일백' 이 아니라 '백' 이다.
static void enqGroup(long n) {
  const int th = (int)(n / 1000), hu = (int)((n / 100) % 10);
  const int rest = (int)(n % 100);  // 끝 두 자리 — 통째로 읽는다
  if (th) {
    if (th > 1) enq(SNDN_FOLDER, SNDN_DIGIT(th));
    enq(SNDN_FOLDER, SNDN_THOUSAND);
  }
  if (hu) {
    if (hu > 1) enq(SNDN_FOLDER, SNDN_DIGIT(hu));
    enq(SNDN_FOLDER, SNDN_HUNDRED);
  }
  if (rest) enqWhole(rest);
}

// 0 ~ 99,999,999. 그보다 큰 수는 읽지 않는다(토막을 더 굽지 않고 조용히 지나간다 —
// 포인트가 억을 넘는 일은 없고, 넘었다면 읽는 쪽이 아니라 서버를 봐야 한다).
static void enqNumber(long v) {
  if (v < 0) v = -v;  // 부호는 읽지 않는다 — 소리로 읽는 자리는 지급(+)뿐이다
  if (v <= SNDN_WHOLE_MAX) {
    enqWhole(v);  // 100 까지는 한 토막
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
      // ★ 묵은 알림을 버리고 깃발을 내린 **뒤에** 리셋을 보낸다. 모듈은 켜질 때 제
      //   스스로 0x3F 를 한 번 보내는데, 그 바이트가 FIFO 에 앉아 있다가 이 함수 맨
      //   앞의 dfFeed() 에서 읽히면 readySeen 이 리셋을 보내기도 전에 서 버린다.
      //   그러면 아래 ST_RESET 이 '벌써 섰다' 로 보고 **기다리지 않고** 지나가, 0x09·
      //   음량·첫 토막이 재부팅 중인 모듈에 떨어져 통째로 사라진다(그 셋은 다시
      //   보내는 곳이 없다). setup 이 soundLoop 을 돌리지 않는 쪽에서는 그 묵은
      //   바이트가 **늘** 쌓여 있으므로 어쩌다 한 번이 아니라 매번 그렇게 된다.
      dfDrain();
      readySeen = false;
      dfCmd(DF_CMD_RESET, 0, 0);
      state = ST_RESET;
      return;
    case ST_RESET:
      // 정착 시간은 '섰다' 가 와도 지킨다 — 그 알림은 모듈이 SD 를 다 읽기 전에 오는
      // 것이 있어, 그것만 믿고 지나가면 0x09 가 또 허공에 떨어진다.
      if ((int32_t)(now - stepAt) < DF_RESET_SETTLE_MS) return;
      // 그 뒤로는 '섰다'(0x3F)가 오면 바로, 안 보내 주는 짝퉁이면 한도에서 넘어간다.
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
// 디스플레이가 보낸 글자를 그대로 받아 어느 토막을 읽을지 고른다. **이 리더는 번호를
// 고를 재료가 없다** — 서버에 묻지도 않고 화면도 없어, 무슨 일이 일어났는지 아는 것은
// 저쪽이고 이쪽에 오는 것은 그 한 줄뿐이다. 그래서 표가 하나뿐인 문이다.
//
// ★ 여기 글자는 디스플레이(master/src/main.cpp)가 화면에 그리는 글자와 **한 자도**
//   틀리면 안 된다. 틀리면 그 문구는 조용해진다(틀리게 읽지는 않는다 — 모르는 글자는
//   건너뛴다). 시리얼에 "읽을 토막이 없다" 가 찍히면 거기를 보라.
// ★ 아래 셋(카드를 올려 주세요·설정 다시 받는 중·와이파이 붙는 중)은 태깅 답이 아니라
//   디스플레이가 {"say":"…"} 로 보내 오는 제 쪽 사정이다. funfunFirmware 는 한 보드라
//   그 자리에서 soundSay(번호)를 불렀다 — 여기서는 선을 건너와야 해서 글자로 온다.
static const char NOTE_ATTEND_PREFIX[] = "출석 완료 +";

struct NoteClip {
  const char *note;
  uint8_t clip;
};
static const NoteClip NOTE_CLIPS[] = {
    {"이미 출석했어요", SND_ALREADY},
    {"등록되지 않은 키링", SND_UNKNOWN_CARD},
    {"서버에 물어보지 못했어요", SND_NO_SERVER},
    {"카드를 올려 주세요", SND_IDLE_PROMPT},
    {"설정 다시 받는 중", SND_CONFIG},
    {"와이파이 붙는 중", SND_WIFI},
    {"리더 준비 중", SND_READER_INIT},
    {"리더를 찾을 수 없어요", SND_READER_FAIL},
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

  // 안내문 칸이 비었다 = 그냥 조회다(출석모드가 아니거나 이미 받아 간 아이).
  // 디스플레이는 '잔여 포인트' 와 숫자를 띄우고 있다 — 그것을 읽는다.
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
  // 고정 문구 13개를 돈 뒤 수 두 개 — 통째(폴더 03)와 조립(폴더 02) 두 길을 다 들어 본다.
  static uint8_t step = 0;
  const uint8_t steps = SND_PEOPLE + 2;
  if (step < SND_PEOPLE) {
    const uint8_t clip = (uint8_t)(step + 1);
    Serial.printf("[소리] 시험 %u/%u — /%02d/%03d\n", step + 1, steps, SND_FOLDER, clip);
    soundSay(clip);
  } else if (step == SND_PEOPLE) {
    Serial.printf("[소리] 시험 %u/%u — 57 (0~100 통째)\n", step + 1, steps);
    soundStop();
    enqNumber(57);
  } else {
    Serial.printf("[소리] 시험 %u/%u — 1234 (토막 조립)\n", step + 1, steps);
    soundStop();
    enqNumber(1234);
  }
  step = (uint8_t)((step + 1) % steps);
}

void soundReport() {
  static const char *NAMES[] = {"준비중", "서는중", "저장소", "음량", "조용", "읽는중"};
  Serial.printf("[소리] %s · 줄 %u토막 · 읽은 토막 %lu · 알림 못받음 %lu · 다시 섬 %lu · "
                "음량 %d/30\n",
                NAMES[state], qCount, (unsigned long)playedCount, (unsigned long)missedCount,
                (unsigned long)rebootCount, volume);
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
