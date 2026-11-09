// 소리 — microSD 의 WAV 토막을 줄에 세워 차례로 읽어 MAX98357A(I2S)로 낸다.
// v3 리더 — v2(../../funfunFirmware_v2/src/sound.cpp)에서 그대로 가져와 셋을 더했다:
//   · 화면이 {"say"} 로 보내는 다섯 문구를 표(NOTE_CLIPS)에 더했다(nfcProject/slave 와 같다)
//   · 짧은 음(soundTone) — 부저 없이 앰프로 '삑' 을 낸다(SND_TONE_NO)
//   · soundSayNote(…, keep) — 바로 앞에 세운 '삑' 을 끊지 않고 그 뒤에 이어 읽는다
// 무엇을 왜 읽는지는 sound.h 머리말에 있다. 이 파일은 그것을 어떻게 하는지만 담는다.
// 토막을 고르는 쪽(숫자 조립 · 안내문 표)은 v1(DFPlayer 판)과 같은 코드고, 트는 쪽만 다르다.

#include "sound.h"

#if SOUND_ENABLE

#include <ESP_I2S.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

// ── 트는 일꾼 ────────────────────────────────────────────────────
// 재생은 따로 선 태스크(soundTask)가 맡는다. 본 고리(loop)는 https 왕복으로 수백 ms 씩
// 멈추는데, 거기서 I2S 에 표본을 밀어 넣으면 그동안 소리가 끊긴다. 일꾼은 줄에서 토막을
// 하나 꺼내 파일을 열고, 1KB 씩 읽어 음량을 곱해 I2S 로 쓴다 — 쓰기가 DMA 자리가 날 때까지
// 기다려 주므로 속도는 저절로 맞는다.
// ★ 코어 1 · 우선순위 2 — 본 고리(코어 1 · 1)보다 높아 표본이 늦지 않고, 코어 0 의 와이파이
//   (ESP-NOW)는 건드리지 않는다. PN532 는 본 고리가 하드웨어 SPI(FSPI)로 읽는다 — SD 는 HSPI 라 버스가 다르다.
// ★ SD 는 이 일꾼만 만진다 — 카드를 여러 태스크가 나눠 쓰지 않게.
#define SND_TASK_STACK 4096
#define SND_TASK_PRIO 2
#define SND_TASK_CORE 1
#define SND_CHUNK 1024        // 한 번에 읽어 쓰는 바이트(22050Hz 모노면 23ms)
#define SND_DEFAULT_RATE 22050  // make_voices.sh 가 굽는 율 — 켤 때 앰프를 이것으로 연다

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
// 본 고리가 세우고 일꾼이 꺼낸다 — 둘 다 qMux 안에서만 만진다.
#define SND_QUEUE_MAX 24

// 이름 소리는 folder 가 SND_NAME_NO(9)이고 번호 대신 key(16진 8자를 uint32 로)를 쓴다 —
// 파일이 /09/<key>.wav 다. 나머지는 key 가 0 이고 /%02d/%03d.wav 다.
#define SND_NAME_NO 9
// 짧은 음은 folder 가 SND_TONE_NO 이고 key 에 (높이 Hz << 16 | 길이 ms) 를 싣는다 — 높이 0 은 쉼.
#define SND_TONE_NO 0xFE
struct SndItem {
  uint8_t folder;
  uint8_t track;
  uint32_t key;
};
static SndItem queue[SND_QUEUE_MAX];
static uint8_t qHead = 0, qCount = 0;
static portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;
// 끊기 세대 — soundStop() 이 하나 올린다. 일꾼은 토막을 꺼낼 때의 세대를 쥐고 있다가 그것이
// 달라지면 읽던 것을 그 자리에서 버린다. 꺼내기와 세대 읽기를 같은 잠금 안에서 해서,
// 끊기 직전에 꺼낸 토막(앞 아이의 잔액)이 끊긴 뒤에 읽히는 일이 없다.
static volatile uint32_t stopGen = 0;

static SPIClass sdSpi(HSPI);
static I2SClass i2s;
static TaskHandle_t sndTask = nullptr;
static bool sdOk = false;
static bool i2sOk = false;
static uint32_t i2sRate = 0;
static volatile uint8_t volume = SOUND_VOLUME;
static volatile bool playing = false;
static SndItem cur = {0, 0, 0};  // 지금 읽는 토막 — 상태 줄에 찍는다
// 셋은 일꾼만 올리고 상태 줄이 읽기만 한다 — 한 줄 찍는 값이라 잠그지 않는다
static uint32_t playedCount = 0;
static uint32_t missingCount = 0;  // SD 에 그 번호의 파일이 없었다
static uint32_t badCount = 0;      // 열었지만 읽을 수 없는 꼴(PCM 16비트가 아니다 등)

// ── 아이 이름 소리 (/09) ─────────────────────────────────────────
// 키(16진 8자)를 uint32 로 바꿔 목록에 든다. 목록은 일꾼이 켤 때 /09 를 훑어 채우고, 다 쓴 뒤
// 하나씩 더한다. 본 고리는 읽기만 한다 — 둘 다 nameMux 안에서.
// 쓰기는 한 칸짜리 자리(store*)로 넘겨받는다 — 본 고리가 하나를 받아 넘기면 일꾼이 줄이 빌 때
// 쓰고 자리를 비운다. 받기(https)가 쓰기보다 훨씬 느려 칸이 하나면 넉넉하다.
#define SND_NAME_FOLDER "/09"
#if NAME_VOICE_ENABLE
static uint32_t *nameKeys = nullptr;  // PSRAM
static int nameCount = 0;
static volatile bool namesReady = false;
static portMUX_TYPE nameMux = portMUX_INITIALIZER_UNLOCKED;

// 넘겨받는 것은 세 갈래다 — 이름 소리 · 고정 토막(5단계 · 서버가 만든 판) · 판 이름(/voice.ver).
enum StoreKind : uint8_t { STORE_NAME, STORE_CLIP, STORE_VER };
static volatile bool storePending = false;  // 본 고리가 세우고, 일꾼이 다 쓰면 내린다
static uint8_t storeKind = STORE_NAME;
static char storeKey[9];                     // 이름 키 · 판 이름(빈 값이면 /voice.ver 를 지운다)
static uint8_t storeFolder = 0, storeTrack = 0;  // 고정 토막
static uint8_t *storeBuf = nullptr;
static size_t storeLen = 0;
static uint32_t storedCount = 0, storeFailCount = 0;  // 이번에 켠 뒤로 — 상태 줄에 찍는다
static uint32_t clipStoredCount = 0;

// SD 의 고정 토막이 어느 판인가(/voice.ver 의 8자). 맥 스크립트로 넣은 카드는 파일이 없어 빈 값이다.
// 일꾼이 켤 때 읽고 판을 다 받은 뒤 고친다. 본 고리는 soundPackVer() 로 베껴 간다(nameMux 안).
#define SND_VER_FILE "/voice.ver"
static char packVer[9] = "";

// '0a1b2c3d' → 0x0a1b2c3d. 꼴이 아니면 false.
static bool keyOf(const char *s, uint32_t *out) {
  uint32_t v = 0;
  for (int i = 0; i < 8; i++) {
    const char c = s[i];
    uint32_t d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else return false;
    v = v << 4 | d;
  }
  *out = v;
  return true;
}

static bool nameHasLocked(uint32_t k) {
  for (int i = 0; i < nameCount; i++)
    if (nameKeys[i] == k) return true;
  return false;
}

static void nameAdd(uint32_t k) {
  portENTER_CRITICAL(&nameMux);
  if (nameKeys && nameCount < NAME_VOICE_MAX && !nameHasLocked(k)) nameKeys[nameCount++] = k;
  portEXIT_CRITICAL(&nameMux);
}

// 목록에서 뺀다 — 틀려고 보니 파일이 없거나 깨져 있었다(카드를 바꿔 꽂았다 등). 빼 두면 다음
// 이름표 때 본 고리가 다시 받아 온다. 순서는 상관없어 끝의 것을 그 자리로 옮긴다.
static void nameDrop(uint32_t k) {
  portENTER_CRITICAL(&nameMux);
  for (int i = 0; i < nameCount; i++) {
    if (nameKeys[i] == k) {
      nameKeys[i] = nameKeys[--nameCount];
      break;
    }
  }
  portEXIT_CRITICAL(&nameMux);
}

// 켤 때 한 번(일꾼 안). /09 가 없으면 만들고, 있는 .wav 를 목록에 든다.
// 쓰다 끊긴 .tmp(전원이 나갔다)는 지운다 — 남겨 두면 다음에 같은 이름으로 쓸 때 걸린다.
static void nameScan() {
  if (!nameKeys) nameKeys = (uint32_t *)ps_malloc(sizeof(uint32_t) * NAME_VOICE_MAX);
  if (!sdOk || !nameKeys) return;  // namesReady 를 세우지 않는다 — 받으러 가지 않는다
  // 고정 토막의 판
  if (File v = SD.open(SND_VER_FILE)) {
    char b[9] = "";
    const int n = v.read((uint8_t *)b, 8);
    v.close();
    uint32_t k;
    if (n == 8 && keyOf(b, &k)) memcpy(packVer, b, 9);
  }
  if (!SD.exists(SND_NAME_FOLDER)) SD.mkdir(SND_NAME_FOLDER);
  File dir = SD.open(SND_NAME_FOLDER);
  if (!dir || !dir.isDirectory()) return;
  char tmps[8][24];
  int tmpN = 0, over = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    char n[16];  // 바탕 이름('0a1b2c3d.wav') — close 뒤에는 f.name() 이 가리키는 자리가 없다
    strlcpy(n, f.name(), sizeof(n));
    const bool isDir = f.isDirectory();
    f.close();
    if (isDir || strlen(n) != 12) continue;
    uint32_t k;
    if (!keyOf(n, &k)) continue;
    if (!strcmp(n + 8, ".wav")) {
      if (nameCount < NAME_VOICE_MAX) nameAdd(k);
      else over++;
    } else if (!strcmp(n + 8, ".tmp") && tmpN < 8) {
      snprintf(tmps[tmpN++], sizeof(tmps[0]), SND_NAME_FOLDER "/%s", n);
    }
  }
  dir.close();
  for (int i = 0; i < tmpN; i++) SD.remove(tmps[i]);  // 훑기를 마친 뒤에 지운다
  namesReady = true;
  Serial.printf("[이름소리] SD 에 %d개%s%s · 고정 토막 판 %s\n", nameCount,
                tmpN ? " · 반쪽 파일 지움" : "",
                over ? " · ★ 목록이 넘쳐 일부를 못 셌다(NAME_VOICE_MAX)" : "",
                packVer[0] ? packVer : "없음(맥에서 구운 것이거나 빈 카드)");
}

// buf 를 tmp 에 다 쓴 뒤 fin 으로 이름을 바꾼다 — 쓰다 전원이 나가도 반쪽 파일이 fin 으로 남지 않게.
// (FILE_WRITE 는 있던 tmp 를 비우고 쓰므로 전에 남은 반쪽 tmp 는 그대로 덮인다)
static bool writeSwap(const char *tmp, const char *fin, const uint8_t *buf, size_t len) {
  bool ok = false;
  File f = SD.open(tmp, FILE_WRITE);
  if (f) {
    const size_t n = f.write(buf, len);
    f.close();
    if (n == len) {
      if (SD.exists(fin)) SD.remove(fin);
      ok = SD.rename(tmp, fin);
    }
    if (!ok) SD.remove(tmp);
  }
  return ok;
}

// 넘겨받은 것을 쓴다(일꾼 안 · 줄이 빌 때). .tmp 에 다 쓴 뒤 이름을 바꾼다 —
// 쓰다 전원이 나가도 반쪽 파일이 .wav 로 남아 틀리지 않게.
static void nameStore() {
  char tmp[24], fin[24];
  bool ok = false;
  if (storeKind == STORE_NAME) {
    snprintf(tmp, sizeof(tmp), SND_NAME_FOLDER "/%s.tmp", storeKey);
    snprintf(fin, sizeof(fin), SND_NAME_FOLDER "/%s.wav", storeKey);
    ok = writeSwap(tmp, fin, storeBuf, storeLen);
    uint32_t k;
    if (ok && keyOf(storeKey, &k)) {
      nameAdd(k);
      storedCount++;
    }
  } else if (storeKind == STORE_CLIP) {
    // 빈 카드에서 시작하면 폴더가 없다 — 판을 받으며 만든다
    char dir[8];
    snprintf(dir, sizeof(dir), "/%02u", storeFolder);
    if (!SD.exists(dir)) SD.mkdir(dir);
    snprintf(tmp, sizeof(tmp), "%s/%03u.tmp", dir, storeTrack);
    snprintf(fin, sizeof(fin), "%s/%03u.wav", dir, storeTrack);
    ok = writeSwap(tmp, fin, storeBuf, storeLen);
    if (ok) clipStoredCount++;
  } else {  // STORE_VER — 빈 값이면 지운다(판을 받기 시작할 때: 다 받기 전에 '같은 판' 으로 보이지 않게)
    snprintf(fin, sizeof(fin), "%s", SND_VER_FILE);
    if (!storeKey[0]) {
      ok = !SD.exists(fin) || SD.remove(fin);
    } else {
      ok = writeSwap("/voice.tmp", fin, (const uint8_t *)storeKey, 8);
    }
    if (ok) {
      portENTER_CRITICAL(&nameMux);
      memcpy(packVer, storeKey, sizeof(packVer));
      portEXIT_CRITICAL(&nameMux);
    }
  }
  if (!ok) {
    storeFailCount++;
    Serial.printf("[이름소리] %s 를 SD 에 못 썼다 — 카드가 꽉 찼거나 빠졌다\n", fin);
  }
  free(storeBuf);  // STORE_VER 는 nullptr — free 는 그냥 지나간다
  storeBuf = nullptr;
  storePending = false;
}
#endif

// ── 줄에 세우기 ──────────────────────────────────────────────────
static void enq(uint8_t folder, uint8_t track) {
  bool added = false;
  portENTER_CRITICAL(&qMux);
  if (qCount < SND_QUEUE_MAX) {  // 넘치면 뒤를 버린다
    const uint8_t i = (uint8_t)((qHead + qCount) % SND_QUEUE_MAX);
    queue[i].folder = folder;
    queue[i].track = track;
    queue[i].key = 0;
    qCount++;
    added = true;
  }
  portEXIT_CRITICAL(&qMux);
  if (added && sndTask) xTaskNotifyGive(sndTask);  // 자고 있으면 깨운다
}

#if NAME_VOICE_ENABLE
// 이름 소리 한 토막. 줄 맨 앞에 서도록 soundSayNote 가 줄을 비운 직후에 부른다.
static void enqName(uint32_t k) {
  bool added = false;
  portENTER_CRITICAL(&qMux);
  if (qCount < SND_QUEUE_MAX) {
    const uint8_t i = (uint8_t)((qHead + qCount) % SND_QUEUE_MAX);
    queue[i].folder = SND_NAME_NO;
    queue[i].track = 0;
    queue[i].key = k;
    qCount++;
    added = true;
  }
  portEXIT_CRITICAL(&qMux);
  if (added && sndTask) xTaskNotifyGive(sndTask);
}
#endif

static void qClear() {
  portENTER_CRITICAL(&qMux);
  qHead = 0;
  qCount = 0;
  stopGen = stopGen + 1;  // volatile 에 ++ 는 C++20 경고라 풀어 쓴다
  portEXIT_CRITICAL(&qMux);
}

static bool deq(SndItem *out, uint32_t *gen) {
  bool got = false;
  portENTER_CRITICAL(&qMux);
  if (qCount) {
    *out = queue[qHead];
    qHead = (uint8_t)((qHead + 1) % SND_QUEUE_MAX);
    qCount--;
    *gen = stopGen;
    got = true;
  }
  portEXIT_CRITICAL(&qMux);
  return got;
}

// ── WAV 한 토막 틀기 (일꾼 안) ───────────────────────────────────
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

// 음량 0~30 → 곱할 값(0~256). 제곱으로 둔다 — 귀는 크기를 로그로 듣는다. 22 → 137(약 −5dB).
static int32_t gainOf(uint8_t vol) { return (int32_t)vol * vol * 256 / 900; }

// 앰프를 그 율로. 모노 데이터를 좌우 두 칸에 똑같이 싣는다(I2S_STD_SLOT_BOTH) —
// MAX98357A 는 SD 핀을 비워 두면 좌우 평균을 내므로, 한쪽에만 실으면 소리가 반으로 준다.
static bool i2sRateTo(uint32_t rate) {
  if (i2sOk && rate == i2sRate) return true;
  const bool ok = i2sOk ? i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
                                          I2S_STD_SLOT_BOTH)
                        : i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT,
                                    I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH);
  if (ok) {
    i2sOk = true;
    i2sRate = rate;
  }
  return ok;
}

static void pathOf(const SndItem &it, char *path, size_t n) {
  if (it.folder == SND_NAME_NO)
    snprintf(path, n, SND_NAME_FOLDER "/%08lx.wav", (unsigned long)it.key);
  else
    snprintf(path, n, "/%02u/%03u.wav", it.folder, it.track);
}

static void playSilence(uint32_t ms);  // 아래 — 토막 사이 쉼

// 짧은 음 하나 — 사인파를 그 자리에서 지어 낸다(SD 를 안 읽어 '읽혔다' 가 말보다 빠르다).
// 끝을 5ms 씩 부드럽게 올리고 내린다 — 네모로 끊으면 '틱' 하는 잡음이 붙는다.
static void playTone(const SndItem &it, uint32_t gen) {
  const uint32_t hz = it.key >> 16, ms = it.key & 0xFFFF;
  if (!i2sOk) return;
  if (!hz) {
    playSilence(ms);
    return;
  }
  static int16_t buf[256];
  const uint32_t rate = i2sRate, total = rate * ms / 1000, ramp = rate / 200;  // 5ms
  const int32_t amp = (int32_t)(9000L * gainOf(volume) / 256);
  const float step = 6.2831853f * hz / rate;
  uint32_t n = 0;
  while (n < total && gen == stopGen) {
    size_t k = 0;
    for (; k < 256 && n < total; k++, n++) {
      int32_t a = amp;
      if (n < ramp) a = a * (int32_t)n / (int32_t)ramp;
      else if (total - n < ramp) a = a * (int32_t)(total - n) / (int32_t)ramp;
      buf[k] = (int16_t)(a * sinf(step * n));
    }
    i2s.write((const uint8_t *)buf, k * 2);
  }
}

static void playClip(const SndItem &it, uint32_t gen) {
  if (it.folder == SND_TONE_NO) return playTone(it, gen);
  char path[24];
  pathOf(it, path, sizeof(path));
  [[maybe_unused]] const bool isName = it.folder == SND_NAME_NO;
  File f = SD.open(path);
  if (!f) {
    missingCount++;
#if NAME_VOICE_ENABLE
    if (isName) {
      nameDrop(it.key);  // 목록이 틀렸다 — 다음 이름표 때 다시 받는다
      Serial.printf("[이름소리] %s 가 없다 — 목록에서 빼고 다시 받는다\n", path);
      return;
    }
#endif
    Serial.printf("[소리] %s 가 없다 — 카드에 voices 를 넣었는지, 폴더가 두 자리('01')인지 보세요\n",
                  path);
    return;
  }

  // RIFF 머리 → 'fmt ' 와 'data' 를 찾는다(그 사이의 LIST 같은 덩이는 건너뛴다)
  uint8_t h[12];
  uint16_t fmt = 0, ch = 0, bits = 0;
  uint32_t rate = 0, left = 0;
  bool ok = f.read(h, 12) == 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4);
  while (ok) {
    uint8_t c[8];
    if (f.read(c, 8) != 8) {
      ok = false;
      break;
    }
    const uint32_t size = rd32(c + 4);
    if (!memcmp(c, "fmt ", 4)) {
      uint8_t b[16];
      if (size < 16 || f.read(b, 16) != 16) {
        ok = false;
        break;
      }
      fmt = rd16(b);
      ch = rd16(b + 2);
      rate = rd32(b + 4);
      bits = rd16(b + 14);
      f.seek(f.position() + size - 16 + (size & 1));
    } else if (!memcmp(c, "data", 4)) {
      left = size;
      break;
    } else {
      f.seek(f.position() + size + (size & 1));
    }
  }
  if (!ok || fmt != 1 || bits != 16 || (ch != 1 && ch != 2) || !rate || !left) {
    badCount++;
    Serial.printf("[소리] %s 를 읽을 수 없다 (형식 %u · %u비트 · %u채널 · %luHz) — "
                  "PCM 16비트 WAV 여야 한다\n",
                  path, fmt, bits, ch, (unsigned long)rate);
    f.close();
#if NAME_VOICE_ENABLE
    // 이름 소리는 기기가 받아 쓴 것이라 깨졌으면 지우고 다시 받는다(받을 때 꼴을 보았으니
    // 여기 오는 것은 카드가 상한 때뿐이다). 고정 토막은 손으로 넣은 것이라 그대로 둔다.
    if (isName) {
      SD.remove(path);
      nameDrop(it.key);
    }
#endif
    return;
  }
  if (!i2sRateTo(rate)) {
    badCount++;
    Serial.printf("[소리] 앰프를 %luHz 로 열지 못했다\n", (unsigned long)rate);
    f.close();
    return;
  }

  static int16_t buf[SND_CHUNK / 2];
  cur = it;
  playing = true;
  playedCount++;
  while (left && gen == stopGen) {
    const size_t want = left < SND_CHUNK ? left : SND_CHUNK;
    const int n = f.read((uint8_t *)buf, want);
    if (n <= 0) break;
    left -= n;
    size_t samples = n / 2;
    if (ch == 2) {  // 좌우를 섞어 모노로
      samples /= 2;
      for (size_t i = 0; i < samples; i++) buf[i] = (int16_t)(((int32_t)buf[2 * i] + buf[2 * i + 1]) / 2);
    }
    const int32_t g = gainOf(volume);
    for (size_t i = 0; i < samples; i++) buf[i] = (int16_t)((buf[i] * g) >> 8);
    i2s.write((const uint8_t *)buf, samples * 2);
  }
  f.close();
  playing = false;
}

// 토막 사이 쉼 — 0 을 그만큼 써 넣는다(DMA 가 비면 저절로 0 을 내보내지만, 그러면 쉼의
// 길이가 다음 파일을 여는 시간에 딸려 들쭉날쭉하다).
static void playSilence(uint32_t ms) {
  static const int16_t zero[256] = {0};
  uint32_t samples = i2sRate * ms / 1000;
  while (samples) {
    const uint32_t k = samples < 256 ? samples : 256;
    i2s.write((const uint8_t *)zero, k * 2);
    samples -= k;
  }
}

static void soundTask(void *) {
#if NAME_VOICE_ENABLE
  nameScan();
#endif
  for (;;) {
    SndItem it;
    uint32_t gen;
    if (!deq(&it, &gen)) {
#if NAME_VOICE_ENABLE
      // 읽을 것이 없을 때만 쓴다 — 태깅한 아이의 소리가 쓰기 뒤로 밀리지 않게.
      // (한 개 30KB 쯤이라 쓰기는 수십 ms 다. 그 사이 태깅이 오면 그만큼만 늦다.)
      if (storePending) {
        nameStore();
        continue;
      }
#endif
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));  // 줄이 비면 세울 때까지 잔다
      continue;
    }
    playClip(it, gen);
    portENTER_CRITICAL(&qMux);
    const bool more = qCount > 0 && gen == stopGen;
    portEXIT_CRITICAL(&qMux);
    // 이름 뒤는 쉼을 따로 둔다 — '김하은 출석 완료' 가 한 낱말처럼 붙어 들리면 늘린다
    // 짧은 음 사이에는 넣지 않는다 — 삐-삐 의 틈은 음 줄이 스스로 정한다(쉼 음)
    if (more && it.folder != SND_TONE_NO)
      playSilence(it.folder == SND_NAME_NO ? SOUND_NAME_GAP_MS : SOUND_GAP_MS);
  }
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
  qClear();
  // SD — HSPI 에 핀을 직접 준다. 못 붙어도 기기는 선다(소리만 조용하다 — soundReport).
  // 여기서는 찍지 않는다 — setup 이 USB CDC 를 기다리기 **전에** 부르는 자리라 그 줄이
  // 버려진다(Serial.setTxTimeoutMs(0)). 결과는 setup 이 soundReport() 로 찍는다.
  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdOk = SD.begin(SD_CS_PIN, sdSpi, SD_SPI_HZ, "/sd", 2);
  i2s.setPins(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN);
  i2sRateTo(SND_DEFAULT_RATE);
  if (!sndTask)
    xTaskCreatePinnedToCore(soundTask, "sound", SND_TASK_STACK, nullptr, SND_TASK_PRIO, &sndTask,
                            SND_TASK_CORE);
}

void soundLoop() {}

void soundPump(uint32_t ms) { delay(ms); }

void soundStop() { qClear(); }

void soundSay(uint8_t clip) {
  if (!clip) return;
  soundStop();
  enq(SND_FOLDER, clip);
}

void soundSayMore(uint8_t clip) {
  if (!clip) return;
  enq(SND_FOLDER, clip);
}

void soundVolume(uint8_t vol) { volume = vol > 30 ? 30 : vol; }

// 짧은 음 — 끊지 않고 줄 뒤에 세운다. 높이 0 은 그만큼 쉰다. '삑' 을 바꿔 치우려면 부르는 쪽이
// 먼저 soundStop() 한다(리더 main.cpp 의 sndOk · sndFail).
void soundTone(uint16_t hz, uint16_t ms) {
  bool added = false;
  portENTER_CRITICAL(&qMux);
  if (qCount < SND_QUEUE_MAX) {
    const uint8_t i = (uint8_t)((qHead + qCount) % SND_QUEUE_MAX);
    queue[i].folder = SND_TONE_NO;
    queue[i].track = 0;
    queue[i].key = (uint32_t)hz << 16 | ms;
    qCount++;
    added = true;
  }
  portEXIT_CRITICAL(&qMux);
  if (added && sndTask) xTaskNotifyGive(sndTask);
}

// ── 이름 소리 — 본 고리 쪽 ───────────────────────────────────────
bool soundWavOk(const uint8_t *b, size_t len) {
  if (len < 44 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return false;
  bool fmtOk = false;
  for (size_t p = 12; p + 8 <= len;) {
    const uint32_t size = rd32(b + p + 4);
    if (size > len - p - 8) return false;  // 덩이가 받은 것 밖으로 나간다(덜 받았거나 깨졌다)
    if (!memcmp(b + p, "fmt ", 4)) {
      if (size < 16) return false;
      const uint8_t *f = b + p + 8;
      const uint16_t ch = rd16(f + 2);
      fmtOk = rd16(f) == 1 && rd16(f + 14) == 16 && (ch == 1 || ch == 2) && rd32(f + 4);
    } else if (!memcmp(b + p, "data", 4)) {
      return fmtOk && size > 0;  // 위에서 끝까지 받았는지 보았다
    }
    p += 8 + size + (size & 1);
  }
  return false;
}

#if NAME_VOICE_ENABLE
bool soundNamesReady() { return namesReady; }

bool soundHasName(const char *key) {
  uint32_t k;
  if (!key || strlen(key) != 8 || !keyOf(key, &k)) return false;
  portENTER_CRITICAL(&nameMux);
  const bool has = nameKeys && nameHasLocked(k);
  portEXIT_CRITICAL(&nameMux);
  return has;
}

bool soundStoreBusy() { return storePending; }

bool soundStoreClip(uint8_t folder, uint8_t track, uint8_t *buf, size_t len) {
  // 이름 폴더(09)에는 쓰지 않는다 — 판에는 01~08 만 온다
  if (!namesReady || storePending || !buf || !len || folder < 1 || folder > 8 || !track) return false;
  storeKind = STORE_CLIP;
  storeFolder = folder;
  storeTrack = track;
  storeBuf = buf;
  storeLen = len;
  storePending = true;
  if (sndTask) xTaskNotifyGive(sndTask);
  return true;
}

bool soundStorePackVer(const char *ver) {
  uint32_t k;
  if (!namesReady || storePending || !ver) return false;
  if (ver[0] && (strlen(ver) != 8 || !keyOf(ver, &k))) return false;
  storeKind = STORE_VER;
  strlcpy(storeKey, ver, sizeof(storeKey));
  storeBuf = nullptr;
  storeLen = 0;
  storePending = true;
  if (sndTask) xTaskNotifyGive(sndTask);
  return true;
}

void soundPackVer(char *out, size_t n) {
  portENTER_CRITICAL(&nameMux);
  strlcpy(out, packVer, n);
  portEXIT_CRITICAL(&nameMux);
}

bool soundSayName(const char *key) {
  uint32_t k;
  if (!soundHasName(key) || !keyOf(key, &k)) return false;
  soundStop();
  enqName(k);
  return true;
}

bool soundStoreName(const char *key, uint8_t *buf, size_t len) {
  uint32_t k;
  if (!namesReady || storePending || !buf || !len || !key || strlen(key) != 8 || !keyOf(key, &k))
    return false;
  storeKind = STORE_NAME;
  strlcpy(storeKey, key, sizeof(storeKey));
  storeBuf = buf;
  storeLen = len;
  storePending = true;  // 마지막에 세운다 — 일꾼은 이것을 보고 위의 셋을 읽는다
  if (sndTask) xTaskNotifyGive(sndTask);
  return true;
}
#else
bool soundNamesReady() { return false; }
bool soundHasName(const char *) { return false; }
bool soundStoreBusy() { return false; }
bool soundStoreName(const char *, uint8_t *, size_t) { return false; }
bool soundStoreClip(uint8_t, uint8_t, uint8_t *, size_t) { return false; }
bool soundStorePackVer(const char *) { return false; }
void soundPackVer(char *out, size_t n) { if (n) out[0] = '\0'; }
bool soundSayName(const char *) { return false; }
#endif

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
// ★ 여기 글자는 화면(../display)이 그리고 보내는 글자와 **한 자도** 틀리면 안 된다 — 틀리면 그 문구만
//   조용해진다(시리얼에 "읽을 토막이 없다"). 아래 다섯은 태깅 답이 아니라 화면이 {"say":"…"} 로
//   보내는 제 쪽 사정이다(../display/src/sound.cpp 의 clipText 와 짝) — nfcProject/slave 와 같다.
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

void soundSayNote(const char *note, long points, const char *nameKey, bool keep) {
  if (!keep) soundStop();
  if (!note) note = "";

  // 이름부터 — 그 아이의 소리가 SD 에 있을 때만. 아직 못 받은 아이·미등록 키링은 이름 없이
  // 지금처럼 안내문만 읽는다(화면에는 이름이 떠 있다).
#if NAME_VOICE_ENABLE && SOUND_SAY_NAME
  uint32_t k;
  if (nameKey && soundHasName(nameKey) && keyOf(nameKey, &k)) enqName(k);
#else
  (void)nameKey;
#endif

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
  // 고정 문구 13개를 돈 뒤 수 두 개 — 통째(폴더 03)와 조립(폴더 02) 두 길을 다 들어 본다.
  static uint8_t step = 0;
  const uint8_t steps = SND_PEOPLE + 2;
  if (step < SND_PEOPLE) {
    const uint8_t clip = (uint8_t)(step + 1);
    Serial.printf("[소리] 시험 %u/%u — /%02d/%03d.wav\n", step + 1, steps, SND_FOLDER, clip);
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
  if (!sdOk) {
    Serial.println("[소리] SD 를 붙이지 못했다 — 카드가 꽂혔는지 · FAT32 인지 · 배선(CS 42 · SCK 41 · "
                   "MOSI 40 · MISO 39)을 보세요. 점퍼선이 길면 config.h 의 SD_SPI_HZ 를 4000000 으로");
  } else {
    const bool has = SD.exists("/01/001.wav");
    Serial.printf("[소리] SD %s %lluMB · 토막 %s\n",
                  SD.cardType() == CARD_SDHC ? "SDHC" : (SD.cardType() == CARD_SD ? "SD" : "카드"),
                  SD.cardSize() / (1024ULL * 1024ULL),
                  has ? "있음" : "없음(/01/001.wav) — tools/make_voices.sh 로 굽고 카드에 복사");
  }
  Serial.printf("[소리] 앰프 %s %luHz · %s · 줄 %u토막 · 읽은 토막 %lu · 없는 파일 %lu · "
                "못 읽은 파일 %lu · 음량 %d/30\n",
                i2sOk ? "켜짐" : "못 켬", (unsigned long)i2sRate,
                playing ? "읽는중" : "조용", qCount, (unsigned long)playedCount,
                (unsigned long)missingCount, (unsigned long)badCount, volume);
  if (playing) {
    char path[24];
    pathOf(cur, path, sizeof(path));
    Serial.printf("[소리] 지금 %s\n", path);
  }
#if NAME_VOICE_ENABLE
  Serial.printf("[이름소리] SD 에 %d개%s · 이번에 받아 쓴 것 %lu · 못 쓴 것 %lu%s\n", nameCount,
                namesReady ? "" : " (아직 안 훑음 — SD 가 없거나 켜는 중)",
                (unsigned long)storedCount, (unsigned long)storeFailCount,
                storePending ? " · 쓰는 중" : "");
  Serial.printf("[고정토막] SD 의 판 %s · 이번에 갈아 끼운 토막 %lu\n",
                packVer[0] ? packVer : "없음(맥에서 구운 것이거나 받는 중)",
                (unsigned long)clipStoredCount);
#endif
}

#else  // SOUND_ENABLE 0 — 모듈 없이 구울 때. 부르는 자리를 그대로 두고 아무 일도 안 한다.

void soundBegin() {}
void soundLoop() {}
void soundPump(uint32_t ms) { delay(ms); }
void soundSay(uint8_t) {}
void soundSayMore(uint8_t) {}
void soundSayNote(const char *, long, const char *, bool) {}
void soundTone(uint16_t, uint16_t) {}
void soundStop() {}
void soundVolume(uint8_t) {}
bool soundNamesReady() { return false; }
bool soundHasName(const char *) { return false; }
bool soundWavOk(const uint8_t *, size_t) { return false; }
bool soundStoreName(const char *, uint8_t *, size_t) { return false; }
bool soundStoreBusy() { return false; }
bool soundStoreClip(uint8_t, uint8_t, uint8_t *, size_t) { return false; }
bool soundStorePackVer(const char *) { return false; }
void soundPackVer(char *out, size_t n) { if (n) out[0] = '\0'; }
bool soundSayName(const char *) { return false; }
void soundTestNext() { Serial.println("[소리] 꺼 두었습니다 (SOUND_ENABLE 0)"); }
void soundReport() { Serial.println("[소리] 꺼 두었습니다 (SOUND_ENABLE 0)"); }

#endif
