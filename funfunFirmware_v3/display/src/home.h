#pragma once
// funfunFirmware — 홈(대기) 화면 240×240 원형
//
// 2026-11-06 저녁: **앱(funfunApp)의 학생 카드 · 내역 화면에 맞췄다** — 하는 일은 그대로, 얼굴만.
//
//        ┌─ 분홍 띠 ─ 9:24 | WiFi ─ 오늘도 펀펀하게! ─┐
//        (고양이)(곰)      ╭ 726 ╮       (토끼)(햄스터)     ← 앱 학생 카드 머리의 넷 · 가운데 동그라미
//                          ╰─P──╯
//                        총 포인트
//        ───────────────────────────────
//        등록 인원                     20명
//        오늘                    +36 / -15
//        └──── 분홍 띠 ─ [ ))) 카드를 대 주세요 ] ────┘
//
// · 바탕은 앱의 무늬 바탕(BgLight) 그대로다 — 밝은 회색에 파스텔 점 · 별 · 하트(art.h ART_BG).
// · 색은 앱 Theme.swift 의 FF 와 같다 — 주황(brand) · 지급 초록(earn) · 사용 주황(spend) · 회색 칩.
// · 숫자(등록 인원 · 총 포인트 · 오늘 +/−)는 서버의 GET /api/talent/summary 에서 받는다
//   (main.cpp 의 fetchSummary). 못 받으면 등록 인원만 이름표의 학번 수로 채우고 나머지는 '-'.
//
// ★ 이 화면은 **바뀔 때만** 다시 그린다(main.cpp 의 drawIdle — 분 · 연결 · 숫자의 서명을 견준다).
//   예전 대기 화면은 고리를 돌리느라 100ms 마다 화면 전체(115KB)를 밀어 넣었다.

#include "art.h"  // 앱의 무늬 바탕 · 분류 뱃지 · 띠 캐릭터 — tools/mkart.py 가 만든다

// 서버에서 받은 홈 숫자
struct HomeData {
  bool ok;       // 한 번이라도 받았다
  long members;  // 등록 인원
  long total;    // 총 포인트(잔액 합)
  long earn;     // 오늘 지급 합
  long spend;    // 오늘 사용 합(양수)
};
static HomeData homeData = {};
static int rosterMembers = 0;  // 이름표의 학번 수 — summary 를 못 받았을 때 등록 인원 대신

// 색 — 앱 Theme.swift(FF) · 시안에서 떴다
#define HOME_BG RGB565(242, 242, 246)          // 바탕(무늬를 못 깔 때)
#define HOME_BAND RGB565(246, 231, 224)        // 띠 — 앱 학생 카드 머리의 분홍 ★ mkart.py 의 BAND 와 같게
#define HOME_BAND_LT RGB565(251, 241, 237)     // 띠 안의 밝은 둥근 무늬
#define HOME_CARD RGB565(255, 255, 255)        // 카드 ★ mkart.py 의 CARD 와 같게
#define HOME_INK RGB565(28, 28, 30)            // 제목 글자
#define HOME_GREY RGB565(142, 142, 147)        // 둘째 글자(때 · 이름표)
#define HOME_LINE RGB565(229, 229, 234)        // 카드 안 가름선
#define HOME_CHIP RGB565(238, 238, 240)        // 회색 칩(앱 FF.chipBackground)
#define HOME_BRAND RGB565(249, 115, 74)        // 펀펀 주황(앱 FF.brand)
#define HOME_BRAND_SOFT RGB565(250, 230, 223)  // 옅은 주황(앱 학생 카드의 동그라미)
#define HOME_EARN RGB565(33, 153, 89)          // 지급 +(앱 FF.earn)
#define HOME_SPEND RGB565(219, 112, 28)        // 사용 −(앱 FF.spend) — 빨강은 오류 자리라 쓰지 않는다
#define HOME_PINK RGB565(239, 139, 151)        // 하트
#define HOME_DOT_OFF RGB565(232, 196, 184)     // 장 점(지금 아닌 장)

#define HOME_FONT_S u8g2_font_gulim12_t_korean2  // 작은 글(이름표·단추)
#define HOME_FONT_M u8g2_font_gulim14_t_korean2  // 제목·값
#define HOME_FONT_NUM u8g2_font_logisoso24_tn     // 큰 숫자
#define HOME_FONT_NUM_S u8g2_font_logisoso20_tn   // 큰 숫자가 원에 안 들 때

// 자리 — 위에서 아래로
#define HOME_BAND_Y 44     // 위 띠 아래 끝(가운데는 타원으로 HOME_BAND_Y + 10 까지 내려온다)
#define HOME_DOTS_CY 49    // 몇째 장인가 — 그 타원 안
#define HOME_HEAD_Y 84     // 홈만 — 띠가 캐릭터 넷이 앉는 데까지 내려온다(앱 학생 카드 머리)
#define HOME_AV_CY 98      // 홈 가운데 동그라미(총 포인트)
#define HOME_AV_R 32
#define HOME_ROW1_CY 160   // 등록 인원
#define HOME_ROW2_CY 178   // 오늘 +/-
#define HOME_FOOT_Y 198    // 아래 띠 위 끝(가운데는 타원으로 8 만큼 올라온다)
#define HOME_FOOT_CY 216

// 글자 폭(픽셀)
static int homeTextW(const char *s, const uint8_t *font) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setFont(font);
  gfx->setUTF8Print(true);
  gfx->setTextSize(1);
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// 글자를 x 에서 시작해(가로) cy 가운데(세로)에 그리고, 그린 폭을 돌려준다.
// bold 면 한 픽셀 옆에 한 번 더 그린다 — 굴림에는 굵은 꼴이 없다.
static int homeTextL(const char *s, int x, int cy, uint16_t color, const uint8_t *font,
                     bool bold = false) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setFont(font);
  gfx->setUTF8Print(true);
  gfx->setTextSize(1);
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  gfx->setTextColor(color);
  gfx->setCursor(x - x1, cy - h / 2 - y1);
  gfx->print(s);
  if (bold) {
    gfx->setCursor(x - x1 + 1, cy - h / 2 - y1);
    gfx->print(s);
  }
  return w + (bold ? 1 : 0);
}

static void homeTextC(const char *s, int cx, int cy, uint16_t color, const uint8_t *font,
                      bool bold = false) {
  homeTextL(s, cx - homeTextW(s, font) / 2, cy, color, font, bold);
}

// ── 그림 ─────────────────────────────────────────────────────────
// 그림(art.h · characters.h) 한 장을 (x0, y0) 에 깐다 — 비침색은 건너뛴다. 지금 gfx 가 화면이든
// 긴 그림판(pages.h)이든 그 폭 · 높이로 자른다.
static void homeArt(const uint16_t *art, int px, int x0, int y0) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb || !art) return;
  const int W = gfx->width(), H = gfx->height();
  for (int y = 0; y < px; y++) {
    const int fy = y0 + y;
    if (fy < 0 || fy >= H) continue;
    for (int x = 0; x < px; x++) {
      const int fx = x0 + x;
      if (fx < 0 || fx >= W) continue;
      const uint16_t c = art[y * px + x];  // ESP32 는 플래시가 주소에 비쳐 그대로 읽힌다
      if (c != TRANSPARENT_KEY) fb[fy * W + fx] = c;
    }
  }
}

// 하트(가운데 cx, cy · 반지름 r)
static void homeHeart(int cx, int cy, int r, uint16_t c) {
  gfx->fillCircle(cx - r / 2 - 1, cy - 1, r / 2 + 1, c);
  gfx->fillCircle(cx + r / 2 + 1, cy - 1, r / 2 + 1, c);
  gfx->fillTriangle(cx - r - 1, cy, cx + r + 1, cy, cx, cy + r + 1, c);
}

// 반짝이 — 네 갈래 별
static void homeSparkle(int cx, int cy, int r, uint16_t c) {
  gfx->drawFastVLine(cx, cy - r, 2 * r + 1, c);
  gfx->drawFastHLine(cx - r, cy, 2 * r + 1, c);
  gfx->drawPixel(cx - 1, cy - 1, c);
  gfx->drawPixel(cx + 1, cy - 1, c);
  gfx->drawPixel(cx - 1, cy + 1, c);
  gfx->drawPixel(cx + 1, cy + 1, c);
}

// 키링을 대는 표시 — 앱 위 단추의 ')))'. 왼쪽 끝 x, 가운데 cy.
static void homeNfcWaves(int x, int cy, uint16_t c) {
  for (int k = 0; k < 3; k++) {
    const int r = 3 + k * 4;
    gfx->fillArc(x - 3, cy, r + 1, r, -50, 50, c);
  }
}

// 정수를 천 단위 콤마로 — main.cpp 의 commafy 를 쓴다
static void commafy(long v, char *out, size_t n);

// ── 띠 · 바탕 — 홈과 대기 화면 네 장(pages.h)이 함께 쓴다 ──────────
// 바탕 — 앱의 무늬 바탕 그대로(밝은 회색에 파스텔 점 · 별 · 하트). 늘 같은 무늬다.
static void homeBackdrop() {
  uint16_t *fb = gfx->getFramebuffer();
  if (fb && gfx->width() == 240 && gfx->height() == 240)
    memcpy(fb, ART_BG, sizeof(ART_BG));
  else
    gfx->fillScreen(HOME_BG);
}

// 띠의 분홍 바탕 — 아래 끝 bottom 까지. 앱 학생 카드 머리처럼 두 모서리에 밝은 둥근 무늬와
// 하트 · 반짝이를 얹는다.
static void homeBandFill(int bottom) {
  gfx->fillRect(0, 0, 240, bottom, HOME_BAND);
  gfx->fillCircle(28, -6, 46, HOME_BAND_LT);
  gfx->fillCircle(212, -6, 46, HOME_BAND_LT);
}

// 위 띠 — 아래 끝을 타원으로 둥글게. 첫 줄은 시각 | 연결, 둘째 줄은 제목.
static void homeTopBand(const char *timeText, bool isOnline, const char *title) {
  homeBandFill(HOME_BAND_Y);
  gfx->fillEllipse(CX, HOME_BAND_Y, 150, 10, HOME_BAND);
  homeHeart(58, 30, 3, HOME_PINK);
  homeSparkle(184, 28, 3, HOME_BRAND);
  char top[40];
  snprintf(top, sizeof(top), "%s | %s", timeText, isOnline ? "WiFi" : "오프라인");
  homeTextC(top, CX, 15, HOME_GREY, HOME_FONT_S);
  homeTextC(title, CX, 34, HOME_INK, HOME_FONT_M, true);
}

// 몇째 장인가 — 위 띠가 타원으로 내려온 자리(제목 아래)에 점 n 개. dot < 0 이면 안 그린다.
static void homePageDots(int dot, int n) {
  if (dot < 0 || n <= 1) return;
  const int gap = 10, x0 = CX - (n - 1) * gap / 2;
  for (int i = 0; i < n; i++) {
    if (i == dot)
      gfx->fillCircle(x0 + i * gap, HOME_DOTS_CY, 3, HOME_BRAND);
    else
      gfx->fillCircle(x0 + i * gap, HOME_DOTS_CY, 2, HOME_DOT_OFF);
  }
}

// 아래 띠 — 위 끝을 타원으로 둥글게, 그 안에 흰 알약 하나(')))' 카드를 대 주세요)
static void homeFootBand() {
  gfx->fillRect(0, HOME_FOOT_Y, 240, 240 - HOME_FOOT_Y, HOME_BAND);
  gfx->fillEllipse(CX, HOME_FOOT_Y, 150, 8, HOME_BAND);
  const char *s = "카드를 대 주세요";
  const int iconW = 14, w = 12 + iconW + 4 + homeTextW(s, HOME_FONT_S) + 1 + 12;
  const int x = CX - w / 2;
  gfx->fillRoundRect(x, HOME_FOOT_CY - 11, w, 22, 11, HOME_CARD);
  homeNfcWaves(x + 12, HOME_FOOT_CY, HOME_BRAND);
  homeTextL(s, x + 12 + iconW + 4, HOME_FOOT_CY, HOME_BRAND, HOME_FONT_S, true);
}

// 홈 한 장. timeText 는 '9:24'(모르면 '--:--'). dot/dots — 대기 화면이 돌아가는 장 가운데
// 몇째인가(pages.h). 돌리지 않으면 -1.
static void homeDraw(const char *timeText, bool isOnline, int dot = -1, int dots = 0) {
  homeBackdrop();
  // 앱 학생 카드 — 분홍 머리 밑은 흰 카드다(원 가장자리가 카드의 테가 된다)
  gfx->fillRect(0, HOME_HEAD_Y, 240, HOME_FOOT_Y - HOME_HEAD_Y, HOME_CARD);
  // 머리 — 앱 학생 카드처럼 띠가 캐릭터 넷이 앉는 데까지 내려온다
  homeBandFill(HOME_HEAD_Y);
  homeHeart(104, 64, 3, HOME_PINK);
  homeHeart(150, 58, 2, HOME_PINK);
  homeSparkle(30, 50, 3, HOME_BRAND);
  homeSparkle(212, 52, 3, HOME_BRAND);
  {
    char top[40];
    snprintf(top, sizeof(top), "%s | %s", timeText, isOnline ? "WiFi" : "오프라인");
    homeTextC(top, CX, 15, HOME_GREY, HOME_FONT_S);
    homeTextC("오늘도 펀펀하게!", CX, 34, HOME_INK, HOME_FONT_M, true);
  }
  homePageDots(dot, dots);
  const int cy = HOME_HEAD_Y - ART_BAND_PX;  // 띠 아래 끝에 발을 딛는다
  homeArt(ART_BAND_CAT, ART_BAND_PX, 30, cy);
  homeArt(ART_BAND_BEAR, ART_BAND_PX, 62, cy - 2);
  homeArt(ART_BAND_BUNNY, ART_BAND_PX, 148, cy - 2);
  homeArt(ART_BAND_HAMSTER, ART_BAND_PX, 180, cy);

  // 가운데 동그라미 — 총 포인트(앱 학생 카드의 이름 동그라미 자리)
  char total[16];
  if (homeData.ok)
    commafy(homeData.total, total, sizeof(total));
  else
    strlcpy(total, "-", sizeof(total));
  gfx->fillCircle(CX, HOME_AV_CY, HOME_AV_R + 4, HOME_CARD);
  gfx->fillCircle(CX, HOME_AV_CY, HOME_AV_R, HOME_BRAND_SOFT);
  gfx->drawCircle(CX, HOME_AV_CY, HOME_AV_R, RGB565(240, 206, 194));
  const uint8_t *nf = HOME_FONT_NUM;
  if (homeTextW(total, nf) > 2 * HOME_AV_R - 10) nf = HOME_FONT_NUM_S;
  homeTextC(total, CX, HOME_AV_CY - 3, HOME_BRAND, nf);
  homeTextC("P", CX, HOME_AV_CY + 19, HOME_BRAND, HOME_FONT_S, true);
  homeTextC("총 포인트", CX, HOME_AV_CY + HOME_AV_R + 11, HOME_GREY, HOME_FONT_S);

  // 가름선 — 앱 학생 카드의 잔액 줄 위 선
  const int lx = 34, rx = 206;
  gfx->drawFastHLine(lx, HOME_ROW1_CY - 12, rx - lx, HOME_LINE);

  // 등록 인원 · 오늘 +/- — 앱의 '잔액 … 20P' 줄처럼 왼쪽 이름표 · 오른쪽 값
  {
    char members[16], v[24];
    const long m = homeData.ok ? homeData.members : rosterMembers;
    if (m > 0 || homeData.ok)
      commafy(m, members, sizeof(members));
    else
      strlcpy(members, "-", sizeof(members));
    snprintf(v, sizeof(v), "%s명", members);
    homeTextL("등록 인원", lx + 6, HOME_ROW1_CY, HOME_GREY, HOME_FONT_S);
    homeTextL(v, rx - 6 - homeTextW(v, HOME_FONT_M) - 1, HOME_ROW1_CY, HOME_INK, HOME_FONT_M, true);
  }
  {
    char a[20], b[20], na[16], nb[16];
    if (homeData.ok) {
      commafy(homeData.earn, na, sizeof(na));
      commafy(homeData.spend, nb, sizeof(nb));
    } else {
      strlcpy(na, "-", sizeof(na));
      strlcpy(nb, "-", sizeof(nb));
    }
    // 숫자를 못 받았으면 부호 없이 '-' 하나만('+-' · '--' 는 값처럼 읽힌다)
    snprintf(a, sizeof(a), homeData.ok ? "+%s" : "%s", na);
    snprintf(b, sizeof(b), homeData.ok ? "-%s" : "%s", nb);
    // ★ 사이 띄움은 픽셀로 준다 — getTextBounds 가 끝 공백을 폭에 넣지 않는다
    const char *sep = "/";
    const int g = 5;
    const int w = homeTextW(a, HOME_FONT_M) + 1 + g + homeTextW(sep, HOME_FONT_M) + g +
                  homeTextW(b, HOME_FONT_M) + 1;
    homeTextL("오늘", lx + 6, HOME_ROW2_CY, HOME_GREY, HOME_FONT_S);
    int x = rx - 6 - w;
    x += homeTextL(a, x, HOME_ROW2_CY, HOME_EARN, HOME_FONT_M, true) + g;
    x += homeTextL(sep, x, HOME_ROW2_CY, HOME_GREY, HOME_FONT_M) + g;
    homeTextL(b, x, HOME_ROW2_CY, HOME_SPEND, HOME_FONT_M, true);
  }

  homeFootBand();
}
