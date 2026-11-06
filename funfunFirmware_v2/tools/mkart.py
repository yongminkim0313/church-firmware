# 대기 화면 그림 — 앱(funfunApp)의 무늬 바탕 · 분류 뱃지 · 머리 띠 캐릭터를 RGB565 C 배열로 (src/art.h)
#
#   python3 tools/mkart.py <funfunApp> <funfunServer> src/art.h [미리보기.png]
#
# 대기 화면(home.h · pages.h)을 앱 화면에 맞추려고 **앱이 쓰는 그 그림**을 굽는다(2026-11-06):
#   · 무늬 바탕   funfunApp/ios/Sources/Assets.xcassets/BgLight          → ART_BG (240x240)
#   · 분류 뱃지   …/BtnAttend · BtnInvite · BtnSnack · BtnMood · BtnPoint  → ART_BADGE_* (BADGE_PX)
#                 앱의 내역 줄 왼쪽 동그라미와 같은 그림이다(FFArt.category · line 과 같은 고르기)
#   · 띠 캐릭터   funfunServer/frontend/public/funfun/characters 의 넷   → ART_BAND_* (BAND_PX)
#                 앱의 학생 카드 머리(고양이 신남 · 곰 포인트 · 토끼 박수 · 햄스터 포인트)
# 반투명 가장자리는 그 그림이 놓일 바탕색(뱃지는 흰 카드 · 캐릭터는 분홍 띠)에 섞고, 투명한 곳은
# 비침색(0xF81F)으로 둔다 — 깔 때 건너뛴다(characters.h · emoji.h 와 같은 꼴).
# ★ 바탕색(CARD · BAND)을 home.h 에서 바꾸면 여기도 바꾸고 다시 구울 것 — 가장자리가 뜬다.
import sys
from PIL import Image

app, server, out = sys.argv[1], sys.argv[2], sys.argv[3]
preview = sys.argv[4] if len(sys.argv) > 4 else None
XC = f'{app}/ios/Sources/Assets.xcassets'
KEY = 0xF81F
BADGE_PX = 28
BAND_PX = 30
CARD = (255, 255, 255)     # home.h HOME_CARD
BAND = (246, 231, 224)     # home.h HOME_BAND — 앱 학생 카드 머리의 분홍


def c565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pixels(img):
    return img.get_flattened_data() if hasattr(img, 'get_flattened_data') else img.getdata()


def imageset(name):
    import glob, os
    files = sorted(glob.glob(f'{XC}/{name}.imageset/*.png'), key=os.path.getsize)
    return Image.open(files[-1]).convert('RGBA')   # 가장 큰 배율


def bake(im, px, bg, crop=True):
    if crop:
        bbox = im.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox()
        if bbox: im = im.crop(bbox)
    im = im.copy()
    im.thumbnail((px, px), Image.LANCZOS)
    canvas = Image.new('RGBA', (px, px), (0, 0, 0, 0))
    canvas.paste(im, ((px - im.width) // 2, (px - im.height) // 2))
    vals, shown = [], Image.new('RGB', (px, px))
    for i, (r, g, b, al) in enumerate(pixels(canvas)):
        if al < 40:
            vals.append(KEY)
            shown.putpixel((i % px, i // px), (255, 0, 255))
        else:
            rgb = tuple(int((v * al + c * (255 - al)) / 255) for v, c in zip((r, g, b), bg))
            vals.append(c565(*rgb))
            shown.putpixel((i % px, i // px), rgb)
    return vals, shown


def carray(name, vals, comment):
    body = ',\n  '.join(', '.join(f'0x{v:04X}' for v in vals[i:i + 16]) for i in range(0, len(vals), 16))
    return f'// {comment}\nstatic const uint16_t {name}[{len(vals)}] PROGMEM = {{\n  {body}\n}};\n'


parts, shots = [], []

# 무늬 바탕 — 앱 바탕(1290 폭)의 가운데 위쪽 한 칸을 떠 240 으로. 무늬가 너무 잘아지지 않게 폭 600 을 뜬다.
bg = Image.open(f'{XC}/BgLight.imageset/' + __import__('os').listdir(f'{XC}/BgLight.imageset')[0]).convert('RGB') \
    if False else imageset('BgLight').convert('RGB')
side = 600
x0 = (bg.width - side) // 2
y0 = int(bg.height * 0.22)
bgc = bg.crop((x0, y0, x0 + side, y0 + side)).resize((240, 240), Image.LANCZOS)
parts.append(carray('ART_BG', [c565(*p) for p in pixels(bgc)], '무늬 바탕 240x240 — 앱 BgLight 의 한 칸'))
shots.append(bgc)

BADGES = [('ATTEND', 'BtnAttend', '출석 — 토끼'), ('INVITE', 'BtnInvite', '전도/초청 · 새친구 — 햄스터'),
          ('SNACK', 'BtnSnack', '간식 교환 — 곰 선물'), ('MOOD', 'BtnMood', '예배 태도 — 고양이'),
          ('POINT', 'BtnPoint', '그 밖의 분류 — 곰 포인트')]
for key, name, what in BADGES:
    vals, shown = bake(imageset(name), BADGE_PX, CARD)
    parts.append(carray(f'ART_BADGE_{key}', vals, f'분류 뱃지 {BADGE_PX}x{BADGE_PX} — {what} (앱 {name})'))
    shots.append(shown)

BAND_CHARS = [('CAT', 'cat-cheer'), ('BEAR', 'bear-coin'), ('BUNNY', 'bunny-clap'), ('HAMSTER', 'hamster-coin')]
for key, f in BAND_CHARS:
    vals, shown = bake(Image.open(f'{server}/frontend/public/funfun/characters/{f}.png').convert('RGBA'),
                       BAND_PX, BAND)
    parts.append(carray(f'ART_BAND_{key}', vals, f'머리 띠 캐릭터 {BAND_PX}x{BAND_PX} — {f}'))
    shots.append(shown)

hdr = f'''#pragma once
// funfunFirmware — 대기 화면 그림 (자동 생성 — 손으로 고치지 말 것)
//
// 앱(funfunApp)의 무늬 바탕 · 분류 뱃지 · 학생 카드 머리의 캐릭터를 그대로 굽는다 — 대기 화면이
// 앱과 같은 얼굴이 되게(2026-11-06). 만든 길과 원본은 tools/mkart.py 머리말에.
//   python3 tools/mkart.py <funfunApp> <funfunServer> src/art.h
// 꼴: RGB565. 투명한 곳은 비침색 0x{KEY:04X}(깔 때 건너뛴다). 가장자리는 놓일 바탕색에 섞었다 —
//   뱃지는 흰 카드(HOME_CARD), 띠 캐릭터는 분홍 띠(HOME_BAND).

#define ART_BADGE_PX {BADGE_PX}
#define ART_BAND_PX {BAND_PX}

{''.join(parts)}'''
open(out, 'w', encoding='utf-8').write(hdr)
print('ok →', out)

if preview:
    W = 240 + 10 + max(BADGE_PX, BAND_PX) * 2 + 20
    sheet = Image.new('RGB', (W, 240), (255, 0, 255))
    sheet.paste(shots[0], (0, 0))
    y = 0
    for i, s in enumerate(shots[1:]):
        big = s.resize((s.width * 2, s.height * 2), Image.NEAREST)
        sheet.paste(big, (250, y)); y += big.height + 2
        if y > 240 - 60: break
    sheet.save(preview)
    # 띠 캐릭터는 따로
    strip = Image.new('RGB', (BAND_PX * 4 * 2, BAND_PX * 2), BAND)
    for i, s in enumerate(shots[1 + len(BADGES):]):
        strip.paste(s.resize((BAND_PX * 2, BAND_PX * 2), Image.NEAREST), (i * BAND_PX * 2, 0))
    strip.save(preview.replace('.png', '_band.png'))
