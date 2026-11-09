# 펀펀 캐릭터 PNG → 32x32 RGB565 C 배열 (funfunFirmware/src/characters.h)
#   python3 tools/mkchars.py <funfunServer/frontend/public/funfun/characters> src/characters.h
# 원본은 앱·웹·서버(talentPhoto 의 ch-…)가 함께 쓰는 그 파일이다 — 그림을 고치면 여기서 다시 굽는다.
# 이모티콘(tools/mkemoji.py)과 같은 꼴: 반투명 가장자리는 흰 바탕에 섞고, 투명한 곳은 비침색.
import sys
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
PX = 32
KEY = 0xF81F
# 서버(talent/TalentManager.js)의 CHARACTER_ANIMAL_NAMES · CHARACTER_POSE_NAMES 와 같은 차례·이름
ANIMALS = [('bear', '곰'), ('cat', '고양이'), ('bunny', '토끼'), ('hamster', '햄스터')]
POSES = [('wave', '인사'), ('cheer', '신남'), ('love', '사랑'), ('coin', '포인트'), ('shy', '부끄'),
         ('sleep', '졸림'), ('clap', '박수'), ('gift', '선물'), ('surprise', '놀람'), ('sad', '슬픔')]
def c565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
def pixels(img):
    return img.get_flattened_data() if hasattr(img, 'get_flattened_data') else img.getdata()
arrays, rows = [], []
for a, an in ANIMALS:
    for p, pn in POSES:
        im = Image.open(f'{src}/{a}-{p}.png').convert('RGBA')
        bbox = im.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox()
        if bbox: im = im.crop(bbox)                   # 빈 여백을 떼고 그림만 크게
        im.thumbnail((PX, PX), Image.LANCZOS)
        canvas = Image.new('RGBA', (PX, PX), (0, 0, 0, 0))
        canvas.paste(im, ((PX - im.width) // 2, (PX - im.height) // 2))
        px = []
        for r, g, b, al in pixels(canvas):
            px.append(KEY if al < 40 else c565(*(int((v * al + 255 * (255 - al)) / 255) for v in (r, g, b))))
        name = f'CHAR_{a.upper()}_{p.upper()}'
        body = ',\n  '.join(', '.join(f'0x{v:04X}' for v in px[i:i + 16]) for i in range(0, len(px), 16))
        arrays.append(f'// :{a}-{p}: {an} {pn}\nstatic const uint16_t {name}[{PX * PX}] PROGMEM = {{\n  {body}\n}};\n')
        rows.append(f'    {{":{a}-{p}:", "{an} {pn}", {name}}},')
hdr = f'''#pragma once
// funfunFirmware — '지금 기분' 캐릭터 그림 (자동 생성 — 손으로 고치지 말 것)
//
// 아이는 이모티콘 대신 네 친구(곰·고양이·토끼·햄스터)의 열 가지 표정을 고를 수 있다. 서버는
// 그것을 status.emoji = ':bear-love:' 로, 이름을 status.label = '곰 사랑' 으로 보낸다.
// 리더 폰트로는 그릴 수 없어 이름만 떴던 것을, 이모티콘(emoji.h)처럼 그림으로 구워 둔다.
// (서버의 /photo/device/ch-… 로 받아 올 수도 있지만 태깅하는 순간 받으러 가지 않으려고 굽는다 —
//  받기와 그리기를 겹치지 않는다는 규칙, main.cpp 머리말.)
//
// 그림: funfunServer/frontend/public/funfun/characters/<동물>-<표정>.png (앱·웹과 같은 원본)
// 꼴: {PX}x{PX} RGB565 — emoji.h 와 같다(흰 바탕에 섞음 · 투명은 0x{KEY:04X}).
// 만든 길: python3 tools/mkchars.py <그 폴더> src/characters.h
// 표가 서버와 어긋나면(동물·표정을 늘렸다) 그 값은 못 찾고 예전처럼 이름으로 그린다.

{''.join(arrays)}
static const EmojiArt CHARACTER_ARTS[] = {{
{chr(10).join(rows)}
}};

static const uint16_t *characterArtOf(const char *key) {{
  if (!key || key[0] != ':') return nullptr;
  for (const EmojiArt &e : CHARACTER_ARTS)
    if (!strcmp(e.utf8, key)) return e.px;
  return nullptr;
}}
'''
open(out, 'w', encoding='utf-8').write(hdr)
print('ok', len(rows), 'characters →', out)
