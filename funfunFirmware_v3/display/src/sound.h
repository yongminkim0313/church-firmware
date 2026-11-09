#pragma once
// ─────────────────────────────────────────────────────────────────
// 소리 (v3 화면) — **말은 리더가 한다.** 이 파일은 리더에게 말을 시키는 얇은 겉봉이다.
//
// 스피커(MAX98357A)와 소리 토막·이름 소리가 든 microSD 는 리더 보드(../reader)에 달렸다.
// 이 보드는 읽을 말을 **글자**로 보낸다 — 리더가 제 표(../reader/src/sound.cpp 의 NOTE_CLIPS)에서
// 토막을 찾고, 모르는 글자는 조용히 지나간다(v2 sound.h '읽는 말 = 화면 글자' 그대로).
//   태깅 결과   main.cpp 의 readerResult — {"uid","name","points","known","note","snd"}
//   제 쪽 사정  soundSay(번호) → {"say":"설정 다시 받는 중"} · soundSayMore → {"say":…,"more":true}
//   이름 하나   soundSayName(키) → {"name":"<키>"} (시리얼 'a')
//   시험        soundTestNext() → {"test":"voice"} (리더가 토막을 하나씩 읽는다 — 시리얼 'v')
//
// 함수 모양은 v2 의 sound.h 와 같다 — main.cpp 를 v2 와 같은 코드로 두려는 것이다.
// SD 에 쓰는 쪽(soundStore* · soundNamesReady …)은 **아직 없다**: 서버에서 받는 것은 이 보드, 두는
// 곳은 리더라 둘 사이에 파일을 건네는 길이 필요하다(README '다음 걸음'). 지금은 '없음' 을 돌려줘
// main.cpp 의 이름 소리 받기 · 고정 토막 판 받기가 시작하지 않는다.
// ─────────────────────────────────────────────────────────────────

#include <Arduino.h>

#include "config.h"

// 폴더 01 — 고정 문구. 번호는 리더 SD 의 /01/NNN.wav 와 같다(../reader/src/sound.h 의 SndClip).
enum SndClip : uint8_t {
  SND_NONE = 0,
  SND_IDLE_PROMPT = 1,   // 카드를 올려 주세요
  SND_BALANCE = 2,       // 잔여 포인트
  SND_ALREADY = 3,       // 이미 출석했어요
  SND_UNKNOWN_CARD = 4,  // 등록되지 않은 키링
  SND_NO_SERVER = 5,     // 서버에 물어보지 못했어요
  SND_CONFIG = 6,        // 설정 다시 받는 중
  SND_READER_INIT = 7,   // 리더 준비 중
  SND_WIFI = 8,          // 와이파이 붙는 중
  SND_READER_FAIL = 9,   // 리더를 찾을 수 없어요
  SND_ATTEND_OK = 10,    // 출석 완료
  SND_POINT = 11,        // 포인트
  SND_ETC = 12,          // 외
  SND_PEOPLE = 13,       // 명
};

// main.cpp 에 있다 — 선이 살아 있으면 선으로, 아니면 라디오로 한 덩이를 리더에게 보낸다.
void readerSend(const char *msg);

void soundBegin();
void soundLoop();
void soundPump(uint32_t ms);
void soundSay(uint8_t clip);
void soundSayMore(uint8_t clip);
void soundStop();
void soundVolume(uint8_t vol);

// 이름 소리 · 고정 토막 판 — 지금은 받아 두지 않는다(위 머리말). 키를 실어 보내 읽히는 것은 된다.
bool soundNamesReady();
bool soundHasName(const char *key);
bool soundWavOk(const uint8_t *buf, size_t len);
bool soundStoreName(const char *key, uint8_t *buf, size_t len);
bool soundStoreBusy();
bool soundStoreClip(uint8_t folder, uint8_t track, uint8_t *buf, size_t len);
bool soundStorePackVer(const char *ver);
void soundPackVer(char *out, size_t n);
bool soundSayName(const char *key);

void soundTestNext();
void soundReport();
