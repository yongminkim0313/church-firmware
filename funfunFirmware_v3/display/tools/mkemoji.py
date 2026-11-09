# Noto Color Emoji PNG(128) → 32x32 RGB565 C 배열 (funfunFirmware/src/emoji.h)
import sys
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
PX = 32
KEY = 0xF81F
# (코드포인트, 서버가 보내는 이모티콘, 서버의 label) — talent/TalentManager.js 의 STATUS_EMOJI_NAMES 와 같은 차례
E = [('1f600', '😀', '기쁨'), ('1f606', '😆', '신남'), ('1f60a', '😊', '웃음'), ('1f634', '😴', '졸림'),
     ('1f622', '😢', '슬픔'), ('1f621', '😡', '화남'), ('1f912', '🤒', '아픔'), ('1f914', '🤔', '생각'),
     ('1f4aa', '💪', '힘냄'), ('1f3b5', '🎵', '노래')]
def c565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
lines = []
for cp, ch, label in E:
    im = Image.open(f'{src}/emoji_u{cp}.png').convert('RGBA')
    im.thumbnail((PX, PX), Image.LANCZOS)            # 비율을 지켜 줄인다(🎵 은 납작하다)
    canvas = Image.new('RGBA', (PX, PX), (0, 0, 0, 0))
    canvas.paste(im, ((PX - im.width) // 2, (PX - im.height) // 2))
    px = []
    for r, g, b, a in canvas.get_flattened_data() if hasattr(canvas, "get_flattened_data") else canvas.getdata():
        if a < 40:
            px.append(KEY)                               # 비침 — 바탕(흰 동그라미)이 그대로
        else:                                            # 가장자리는 흰 바탕에 섞는다
            px.append(c565(*(int((v * a + 255 * (255 - a)) / 255) for v in (r, g, b))))
    body = ',\n  '.join(', '.join(f'0x{p:04X}' for p in px[i:i + 16]) for i in range(0, len(px), 16))
    lines.append(f'// {ch} {label} (U+{cp.upper()})\nstatic const uint16_t EMOJI_{cp.upper()}[{PX * PX}] PROGMEM = {{\n  {body}\n}};\n')
tbl = '\n'.join(f'    {{"{ch}", "{label}", EMOJI_{cp.upper()}}},' for cp, ch, label in E)
hdr = f'''#pragma once
// funfunFirmware — '지금 기분' 이모티콘 그림 (자동 생성 — 손으로 고치지 말 것)
//
// 리더의 한글 폰트(u8g2)에는 이모티콘 글리프가 없어, 서버가 보내는 이모티콘(status.emoji)을
// 지금까지는 우리말 이름(status.label · '생각')으로만 그렸다. 아이가 고를 수 있는 이모티콘은
// 서버의 STATUS_EMOJIS 열 개로 정해져 있어(talent/TalentManager.js) 그림으로 구워 둔다.
// 목록에 없는 이모티콘(서버가 늘린 것)은 이 표에서 못 찾고, 그때는 예전처럼 이름으로 그린다.
//
// 그림: Noto Color Emoji (Google, github.com/googlefonts/noto-emoji v2.047 · png/128)
//       — SIL Open Font License 1.1 / Apache 2.0. 128px 를 {PX}px 로 줄였다.
// 꼴: {PX}x{PX} RGB565(리틀엔디안). 반투명 가장자리는 **흰 바탕**에 섞었다 — 화면이 흰 동그라미
//     위에 얹는다(main.cpp 의 drawMoodBadge). 투명한 곳은 0x{KEY:04X}(비침색 — 건너뛴다).
// 만든 길: python3 mkemoji.py <noto png 폴더> src/emoji.h  (스크립트는 커밋하지 않았다 — 위 표가 전부다)

#define EMOJI_PX {PX}

{''.join(lines)}
struct EmojiArt {{
  const char *utf8;     // 서버가 보내는 그대로(status.emoji)
  const char *label;    // 서버의 우리말 이름(status.label) — 참고용
  const uint16_t *px;
}};
static const EmojiArt EMOJI_ARTS[] = {{
{tbl}
}};

// 서버가 보낸 이모티콘에 맞는 그림. 없으면 nullptr(이름으로 그린다).
static const uint16_t *emojiArtOf(const char *utf8) {{
  if (!utf8 || !utf8[0]) return nullptr;
  for (const EmojiArt &e : EMOJI_ARTS)
    if (!strcmp(e.utf8, utf8)) return e.px;
  return nullptr;
}}
'''
open(out, 'w', encoding='utf-8').write(hdr)
print('ok', len(E), 'emoji →', out)
