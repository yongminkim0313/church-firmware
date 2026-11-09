// 소리 (v3 화면) — 리더에게 글자로 말을 시킨다. 왜 이렇게 생겼는지는 sound.h 머리말에.

#include "sound.h"

// 번호 → 리더가 아는 글자. ★ 리더의 NOTE_CLIPS 와 **한 자도** 틀리면 안 된다 —
// 틀리면 그 문구만 조용해진다(리더 시리얼에 "읽을 토막이 없다" 가 찍힌다).
static const char *clipText(uint8_t clip) {
  switch (clip) {
    case SND_IDLE_PROMPT: return "카드를 올려 주세요";
    case SND_ALREADY: return "이미 출석했어요";
    case SND_UNKNOWN_CARD: return "등록되지 않은 키링";
    case SND_NO_SERVER: return "서버에 물어보지 못했어요";
    case SND_CONFIG: return "설정 다시 받는 중";
    case SND_READER_INIT: return "리더 준비 중";
    case SND_WIFI: return "와이파이 붙는 중";
    case SND_READER_FAIL: return "리더를 찾을 수 없어요";
    default: return nullptr;  // 홀로 읽을 일이 없는 토막(숫자 앞뒤에 붙는 것)
  }
}

static void say(uint8_t clip, bool more) {
#if SOUND_ENABLE
  const char *t = clipText(clip);
  if (!t) return;
  char msg[96];
  snprintf(msg, sizeof(msg), "{\"say\":\"%s\"%s}", t, more ? ",\"more\":true" : "");
  readerSend(msg);
#else
  (void)clip;
  (void)more;
#endif
}

void soundBegin() {}
void soundLoop() {}
void soundPump(uint32_t ms) { delay(ms); }
void soundSay(uint8_t clip) { say(clip, false); }
void soundSayMore(uint8_t clip) { say(clip, true); }
void soundStop() {}
void soundVolume(uint8_t) {}

bool soundNamesReady() { return false; }
bool soundHasName(const char *) { return false; }
bool soundWavOk(const uint8_t *, size_t) { return false; }
bool soundStoreName(const char *, uint8_t *, size_t) { return false; }
bool soundStoreBusy() { return true; }
bool soundStoreClip(uint8_t, uint8_t, uint8_t *, size_t) { return false; }
bool soundStorePackVer(const char *) { return false; }
void soundPackVer(char *out, size_t n) {
  if (n) out[0] = '\0';
}

// 리더 SD 에 그 키가 있는지는 이쪽이 모른다 — 보냈으면 true(없으면 리더 시리얼이 알린다).
bool soundSayName(const char *key) {
  if (!key || !key[0]) return false;
  char msg[48];
  snprintf(msg, sizeof(msg), "{\"name\":\"%.8s\"}", key);
  readerSend(msg);
  return true;
}

void soundTestNext() {
  readerSend("{\"test\":\"voice\"}");
  Serial.println("[소리] 리더에게 토막 하나를 읽게 했다 — 무엇을 읽었는지는 리더 시리얼에");
}

void soundReport() {
  Serial.printf("[소리] 리더가 낸다 — %s · 리더 쪽 상태는 리더 시리얼의 'status'\n",
                SOUND_ENABLE ? "읽을 말을 글자로 보낸다" : "말은 시키지 않는다(삑만)");
}
