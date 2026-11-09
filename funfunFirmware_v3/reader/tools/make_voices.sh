#!/usr/bin/env bash
# 소리 토막 굽기 (v2) — ESP32 가 microSD 에서 읽어 I2S 앰프로 낼 WAV 를 만든다.
#
# 맥의 `say`(한국어 Yuna)로 읽히고 ffmpeg 으로 WAV(PCM 16비트 · 모노 · 22050Hz)로 옮긴다.
# v1(../funfunFirmware, DFPlayer 판)과 표·번호·대본이 같다 — 다른 것은 파일 꼴(.wav)뿐이다. 밖으로 나가는 것이
# 없어 인터넷도 API 키도 필요 없다 — 다른 목소리를 쓰고 싶으면 VOICE 를 바꾼다
# (`say -v '?' | grep ko_KR` 로 목록을 본다).
#
#   ./tools/make_voices.sh            # voices/ 에 만든다
#   VOICE=Sandy RATE=170 ./tools/make_voices.sh
#
# 다 만든 뒤 **FAT32 로 포맷한** microSD 의 뿌리에 폴더째 복사한다:
#   cp -R voices/01 voices/02 voices/03 /Volumes/<SD>/
# 그러면 카드 안이 이 꼴이 된다(src/sound.cpp 가 폴더·파일 번호로 찾는 짜임새다):
#   /01/001.wav … /01/013.wav    고정 문구
#   /02/001.wav … /02/014.wav    숫자 토막 (조립용 — 100 을 넘는 수)
#   /03/001.wav … /03/101.wav    0~100 통째 (번호 = 수 + 1)
#
# ★ 번호와 대본은 src/sound.h(SndClip)·src/sound.cpp(SNDN_*) 와 **같아야 한다.**
#   v1 과 nfcProject 의 표(mp3 판)도 같은 번호다 — 번호를 바꾸면 그쪽 스크립트와 함께 고칠 것.
#   한쪽만 고치면 엉뚱한 말이 나온다. 대본을 화면 글자와 같게 둔 까닭은 sound.h 머리말에.
#   ★ 서버의 funfunServer/talent/talentVoicePack.js 에도 **같은 표**가 있다(이름 소리와 같은
#   목소리로 고정 토막을 만들어 리더가 받아 갈아 끼운다 — NAME_VOICE.md 5단계). 함께 고칠 것.
#   이 스크립트는 이제 서버 없이 처음 SD 를 채우는 **비상용**이다. 이렇게 구운 카드에는
#   /voice.ver 가 없어서, 서버에서 이름 소리를 켜 두면 리더가 서버의 판으로 저절로 바꾼다.
#   다른 데는 두 곳뿐이다 — 'PN532' 는 '리더' 로, 'WiFi' 는 '와이파이' 로 읽힌다.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="${OUT:-voices}"
VOICE="${VOICE:-Yuna}"
RATE="${RATE:-180}"   # 분당 낱말 수. 아이들 앞이라 너무 빠르지 않게.

command -v say >/dev/null || { echo "say 가 없다 — 이 스크립트는 macOS 용이다"; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg 이 없다 — brew install ffmpeg"; exit 1; }

# 폴더 01 — 고정 문구 (src/sound.h 의 SndClip)
CLIPS_01=(
  "카드를 올려 주세요"        # 001 SND_IDLE_PROMPT
  "잔여 포인트"               # 002 SND_BALANCE
  "이미 출석했어요"           # 003 SND_ALREADY
  "등록되지 않은 키링"        # 004 SND_UNKNOWN_CARD
  "서버에 물어보지 못했어요"  # 005 SND_NO_SERVER
  "설정 다시 받는 중"         # 006 SND_CONFIG
  "리더 준비 중"              # 007 SND_READER_INIT  (화면은 'PN532 준비 중')
  "와이파이 붙는 중"          # 008 SND_WIFI         (화면은 'WiFi 붙는 중')
  "리더를 찾을 수 없어요"     # 009 SND_READER_FAIL
  "출석 완료"                 # 010 SND_ATTEND_OK
  "포인트"                    # 011 SND_POINT
  "외"                        # 012 SND_ETC
  "명"                        # 013 SND_PEOPLE
)

# 폴더 02 — 숫자 토막 (src/sound.cpp 의 SNDN_*). 이어 붙여 아무 수나 읽는다.
# 100 을 넘는 수(잔액)에만 쓴다 — 100 까지는 폴더 03 에 통째로 있다.
CLIPS_02=(
  "영" "일" "이" "삼" "사" "오" "육" "칠" "팔" "구"  # 001~010
  "십" "백" "천" "만"                                 # 011~014
)

# 폴더 03 — 0~100 을 한 토막씩. 번호가 '수 + 1' 이다(0→001 … 100→101).
# ★ 토막을 이어 붙이면 '오십칠' 이 '오 · 십 · 칠' 로 띄엄띄엄 들린다. 소리로 나가는 수는
#   거의 다 100 아래(출석 포인트·대부분의 잔액)라, 그 구간만 통째로 구워 한 번에 읽는다.
#   대본은 손으로 적지 않고 아래에서 만든다 — 101개를 손으로 적으면 한 자리가 틀린다.
sino() {  # sino <0~100> → 한자말 읽기(십일 · 이십 · 백 …)
  local n="$1"
  local -a ones=(영 일 이 삼 사 오 육 칠 팔 구)
  local out=""
  if (( n == 0 )); then out="영"
  elif (( n < 10 )); then out="${ones[n]}"
  elif (( n == 100 )); then out="백"
  else
    local t=$(( n / 10 )) o=$(( n % 10 ))
    if (( t > 1 )); then out="${ones[t]}"; fi
    out="${out}십"
    if (( o > 0 )); then out="${out}${ones[o]}"; fi
  fi
  printf '%s' "$out"
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

bake() {  # bake <폴더> <번호> <대본> [quiet]
  local folder="$1" n="$2" text="$3" quiet="${4:-}"
  local aiff="$tmp/x.aiff"
  local wav
  wav="$(printf '%s/%s/%03d.wav' "$OUT" "$folder" "$n")"
  say -v "$VOICE" -r "$RATE" -o "$aiff" "$text"
  # adelay — 앞머리 20ms 만 비운다. v1 은 DFPlayer 가 첫 몇십 ms 를 먹어 120ms 를 비웠는데,
  #   I2S 는 첫 표본부터 그대로 나가 그만큼은 필요 없다(토막을 이을 때 쉼만 늘어난다).
  # apad   — 뒤에 80ms 를 붙인다. 토막을 이어 읽을 때 끝 음절이 잘려 뭉개지지 않게.
  # -map_metadata -1 · -bitexact — LIST 같은 덧붙임 덩이 없이 'fmt ' 와 'data' 만 남긴다.
  # 22050Hz 모노 16비트 — src/sound.cpp 가 켤 때 앰프를 이 율로 연다(다르면 바꿔 틀기는 한다).
  ffmpeg -nostdin -loglevel error -y -i "$aiff" \
    -af "adelay=20,apad=pad_dur=0.08" -map_metadata -1 -bitexact \
    -ac 1 -ar 22050 -c:a pcm_s16le "$wav"
  if [ -z "$quiet" ]; then
    printf '  /%s/%03d.wav  %-26s %5s KB  %ss\n' "$folder" "$n" "$text" \
      "$(( ($(stat -f%z "$wav") + 512) / 1024 ))" \
      "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$wav" | cut -c1-4)"
  fi
}

rm -rf "$OUT"
mkdir -p "$OUT/01" "$OUT/02" "$OUT/03"

echo "목소리 $VOICE · 속도 $RATE"
echo "폴더 01 — 고정 문구"
for i in "${!CLIPS_01[@]}"; do bake 01 "$((i + 1))" "${CLIPS_01[$i]}"; done
echo "폴더 02 — 숫자 토막(조립용)"
for i in "${!CLIPS_02[@]}"; do bake 02 "$((i + 1))" "${CLIPS_02[$i]}"; done

echo "폴더 03 — 0~100 통째 (101개)"
for n in $(seq 0 100); do
  bake 03 "$((n + 1))" "$(sino "$n")" quiet
  if (( n % 20 == 0 )); then printf '  %d … %s\n' "$n" "$(sino "$n")"; fi
done

echo
echo "다 됐다 → $OUT  ($(find "$OUT" -name '*.wav' | wc -l | tr -d ' ')개 · $(du -sh "$OUT" | cut -f1))"
echo "FAT32 microSD 뿌리에 복사:  cp -R $OUT/01 $OUT/02 $OUT/03 /Volumes/<SD>/"
