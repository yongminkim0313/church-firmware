#pragma once
// funfunFirmware — 홈(대기) 화면 240×240 원형
//
// 2026-11-06 받은 원형 워치 시안('오늘도 펀펀하게!')을 이 화면에 옮긴 것이다:
//
//        ┌─ 주황 띠 ─ 9:24 | WiFi ─ 오늘도 펀펀하게! ─┐
//        (천사) 펀펀 포인트        [(곰) 내 포인트]
//         ( 사람 )                  (  Σ   )
//         (  72  )                  ( 496  )
//         (등록 인원)               (총 포인트)
//              ↑ 오늘: +12 / -5 ↓
//        └──────── 주황 띠 ─ 카드를 대 주세요 ────────┘
//
// · 시안 맨 아래의 탭(홈·활동·상점·설정)은 넣지 않았다 — 이 기기에는 터치가 없어 누를 수
//   없는 단추가 되고, 원 아래쪽은 폭이 120px 남짓이라 넷이 들어가지도 않는다. 그 자리에
//   이 기기에서 할 수 있는 일('카드를 대 주세요')을 둔다.
// · 시안의 'LTE | 88%' 는 이 기기에 없다 — 와이파이 상태를 대신 적는다.
// · 숫자(등록 인원 · 총 포인트 · 오늘 +/−)는 서버의 GET /api/talent/summary 에서 받는다
//   (main.cpp 의 fetchSummary). 못 받으면 등록 인원만 이름표의 학번 수로 채우고 나머지는 '-'.
//
// ★ 이 화면은 **바뀔 때만** 다시 그린다(main.cpp 의 drawIdle — 분 · 연결 · 숫자의 서명을 견준다).
//   예전 대기 화면은 고리를 돌리느라 100ms 마다 화면 전체(115KB)를 밀어 넣었다.

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

#define HOME_ORANGE RGB565(243, 156, 52)
#define HOME_ORANGE_SOFT RGB565(255, 226, 186)
#define HOME_CREAM RGB565(255, 245, 230)
#define HOME_DOT RGB565(245, 220, 188)
#define HOME_BROWN RGB565(100, 66, 36)
#define HOME_GREEN RGB565(92, 122, 92)
#define HOME_RED RGB565(214, 72, 60)
#define HOME_GREY RGB565(120, 110, 98)
#define HOME_CARD RGB565(254, 252, 247)
#define HOME_LINE RGB565(224, 218, 208)

#define HOME_FONT_S u8g2_font_gulim12_t_korean2  // 작은 글(이름표·단추)
#define HOME_FONT_M u8g2_font_gulim14_t_korean2  // 제목·오늘 줄
#define HOME_FONT_NUM u8g2_font_logisoso24_tn     // 큰 숫자
#define HOME_FONT_NUM_S u8g2_font_logisoso20_tn   // 큰 숫자가 원에 안 들 때

// 자리 — 위에서 아래로
#define HOME_BAND_Y 44     // 위 띠 아래 끝(가운데는 타원으로 HOME_BAND_Y + 10 까지 내려온다)
#define HOME_DOTS_CY 49    // 몇째 장인가 — 그 타원 안
#define HOME_ROW_CY 68     // 펀펀 포인트 · 내 포인트
#define HOME_CIRC_CY 124   // 두 동그라미의 가운데
#define HOME_CIRC_R 44
#define HOME_CIRC_LX 70
#define HOME_CIRC_RX 170
#define HOME_TODAY_CY 180  // 오늘 +/-
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
// 천사 — 얼굴 · 고리 · 날개 (가운데 cx, cy · 폭 18)
static void homeAngel(int cx, int cy) {
  gfx->fillEllipse(cx - 7, cy + 1, 4, 3, RGB565_WHITE);  // 날개
  gfx->drawEllipse(cx - 7, cy + 1, 4, 3, HOME_LINE);
  gfx->fillEllipse(cx + 7, cy + 1, 4, 3, RGB565_WHITE);
  gfx->drawEllipse(cx + 7, cy + 1, 4, 3, HOME_LINE);
  gfx->fillCircle(cx, cy + 1, 5, RGB565(255, 233, 160));  // 얼굴
  gfx->drawCircle(cx, cy + 1, 5, HOME_ORANGE);
  gfx->drawEllipse(cx, cy - 6, 4, 1, RGB565(240, 190, 40));  // 고리
  gfx->drawPixel(cx - 2, cy, HOME_BROWN);                    // 눈
  gfx->drawPixel(cx + 2, cy, HOME_BROWN);
}

// 곰 — 귀 · 얼굴 · 주둥이 · 눈 (가운데 cx, cy · 폭 16)
static void homeBear(int cx, int cy) {
  const uint16_t fur = RGB565(150, 96, 52), muzzle = RGB565(240, 210, 170);
  gfx->fillCircle(cx - 5, cy - 5, 3, fur);
  gfx->fillCircle(cx + 5, cy - 5, 3, fur);
  gfx->fillCircle(cx, cy, 7, fur);
  gfx->fillEllipse(cx, cy + 3, 3, 2, muzzle);
  gfx->fillCircle(cx - 3, cy - 1, 1, RGB565_BLACK);
  gfx->fillCircle(cx + 3, cy - 1, 1, RGB565_BLACK);
  gfx->drawPixel(cx, cy + 2, RGB565_BLACK);
}

// 사람 — 머리와 어깨(두 겹 테)
static void homePerson(int cx, int cy, uint16_t c) {
  gfx->drawCircle(cx, cy - 5, 4, c);
  gfx->drawCircle(cx, cy - 5, 3, c);
  gfx->drawRoundRect(cx - 7, cy + 1, 14, 8, 3, c);
  gfx->drawRoundRect(cx - 6, cy + 2, 12, 6, 2, c);
}

// Σ — 굴림에 기댈 수 없어(그 글자가 있는지 판마다 다르다) 선으로 긋는다
static void homeSigma(int cx, int cy, uint16_t c) {
  for (int d = 0; d < 2; d++) {
    gfx->drawLine(cx - 6, cy - 8 + d, cx + 6, cy - 8 + d, c);
    gfx->drawLine(cx - 6 + d, cy - 8, cx + 1 + d, cy, c);
    gfx->drawLine(cx + 1 + d, cy, cx - 6 + d, cy + 8, c);
    gfx->drawLine(cx - 6, cy + 8 - d, cx + 6, cy + 8 - d, c);
  }
}

// 위·아래 화살표(채운 삼각형 + 짧은 대)
static void homeArrow(int cx, int cy, bool up, uint16_t c) {
  if (up) {
    gfx->fillTriangle(cx, cy - 6, cx - 5, cy, cx + 5, cy, c);
    gfx->fillRect(cx - 1, cy, 3, 6, c);
  } else {
    gfx->fillTriangle(cx, cy + 6, cx - 5, cy, cx + 5, cy, c);
    gfx->fillRect(cx - 1, cy - 6, 3, 6, c);
  }
}

// 정수를 천 단위 콤마로 — main.cpp 의 commafy 를 쓴다
static void commafy(long v, char *out, size_t n);

// 동그라미 하나 — 아이콘 · 큰 숫자 · 이름표
static void homeStat(int cx, uint16_t fill, uint16_t edge, uint16_t numColor, const char *num,
                     const char *label, bool sigma) {
  gfx->fillCircle(cx, HOME_CIRC_CY, HOME_CIRC_R, fill);
  gfx->drawCircle(cx, HOME_CIRC_CY, HOME_CIRC_R, edge);
  gfx->drawCircle(cx, HOME_CIRC_CY, HOME_CIRC_R - 1, edge);
  if (sigma)
    homeSigma(cx, HOME_CIRC_CY - 25, numColor);
  else
    homePerson(cx, HOME_CIRC_CY - 25, HOME_ORANGE);

  // 큰 숫자 — 원 안에 안 들면 한 단계 작은 꼴로
  const uint8_t *nf = HOME_FONT_NUM;
  if (homeTextW(num, nf) > 2 * HOME_CIRC_R - 14) nf = HOME_FONT_NUM_S;
  homeTextC(num, cx, HOME_CIRC_CY - 1, numColor, nf);

  homeTextC(label, cx, HOME_CIRC_CY + 23, HOME_GREY, HOME_FONT_S);
}

// ── 띠 · 바탕 — 홈과 대기 화면 네 장(pages.h)이 함께 쓴다 ──────────
// 바탕 점 — 고정된 자리(늘 같은 무늬). 그 위에 나머지가 덮는다.
static void homeBackdrop() {
  gfx->fillScreen(HOME_CREAM);
  for (int i = 0; i < 70; i++) {
    const int x = (i * 73 + 17) % 240;
    const int y = HOME_BAND_Y + 14 + (i * 131 + 29) % (HOME_FOOT_Y - HOME_BAND_Y - 22);
    gfx->fillCircle(x, y, (i % 3) ? 1 : 2, HOME_DOT);
  }
}

// 위 띠 — 아래 끝을 타원으로 둥글게. 첫 줄은 시각 | 연결, 둘째 줄은 제목.
static void homeTopBand(const char *timeText, bool isOnline, const char *title) {
  gfx->fillRect(0, 0, 240, HOME_BAND_Y, HOME_ORANGE);
  gfx->fillEllipse(CX, HOME_BAND_Y, 150, 10, HOME_ORANGE);
  char top[40];
  snprintf(top, sizeof(top), "%s | %s", timeText, isOnline ? "WiFi" : "오프라인");
  homeTextC(top, CX, 15, RGB565_WHITE, HOME_FONT_S);
  homeTextC(title, CX, 34, RGB565_WHITE, HOME_FONT_M, true);
}

// 몇째 장인가 — 위 띠가 타원으로 내려온 자리(제목 아래)에 점 n 개. dot < 0 이면 안 그린다.
static void homePageDots(int dot, int n) {
  if (dot < 0 || n <= 1) return;
  const int gap = 10, x0 = CX - (n - 1) * gap / 2;
  for (int i = 0; i < n; i++) {
    if (i == dot)
      gfx->fillCircle(x0 + i * gap, HOME_DOTS_CY, 3, RGB565_WHITE);
    else
      gfx->fillCircle(x0 + i * gap, HOME_DOTS_CY, 2, HOME_ORANGE_SOFT);
  }
}

// 아래 띠 — 위 끝을 타원으로 둥글게, 그 안에 흰 알약 하나
static void homeFootBand() {
  gfx->fillRect(0, HOME_FOOT_Y, 240, 240 - HOME_FOOT_Y, HOME_ORANGE);
  gfx->fillEllipse(CX, HOME_FOOT_Y, 150, 8, HOME_ORANGE);
  const char *s = "카드를 대 주세요";
  const int w = homeTextW(s, HOME_FONT_S) + 20;
  gfx->fillRoundRect(CX - w / 2, HOME_FOOT_CY - 10, w, 20, 10, RGB565_WHITE);
  homeTextC(s, CX, HOME_FOOT_CY, HOME_BROWN, HOME_FONT_S, true);
}

// 홈 한 장. timeText 는 '9:24'(모르면 '--:--'). dot/dots — 대기 화면이 돌아가는 장 가운데
// 몇째인가(pages.h). 돌리지 않으면 -1.
static void homeDraw(const char *timeText, bool isOnline, int dot = -1, int dots = 0) {
  homeBackdrop();
  homeTopBand(timeText, isOnline, "오늘도 펀펀하게!");
  homePageDots(dot, dots);

  // 둘째 줄 — (천사) 펀펀 포인트 · [(곰) 내 포인트]
  {
    const char *lab = "펀펀 포인트", *pill = "내 포인트";
    const int labW = 18 + 4 + homeTextW(lab, HOME_FONT_S) + 1;
    const int pillW = 8 + 16 + 4 + homeTextW(pill, HOME_FONT_S) + 10;
    const int gap = 10;
    int x = CX - (labW + gap + pillW) / 2;
    homeAngel(x + 9, HOME_ROW_CY);
    homeTextL(lab, x + 22, HOME_ROW_CY, HOME_BROWN, HOME_FONT_S, true);
    x += labW + gap;
    gfx->fillRoundRect(x, HOME_ROW_CY - 11, pillW, 22, 11, HOME_ORANGE);
    homeBear(x + 8 + 8, HOME_ROW_CY);
    homeTextL(pill, x + 8 + 16 + 4, HOME_ROW_CY, RGB565_WHITE, HOME_FONT_S, true);
  }

  // 두 동그라미
  char members[16], total[16];
  const long m = homeData.ok ? homeData.members : rosterMembers;
  if (m > 0 || homeData.ok)
    commafy(m, members, sizeof(members));
  else
    strlcpy(members, "-", sizeof(members));
  if (homeData.ok)
    commafy(homeData.total, total, sizeof(total));
  else
    strlcpy(total, "-", sizeof(total));
  homeStat(HOME_CIRC_LX, HOME_ORANGE_SOFT, HOME_ORANGE, HOME_ORANGE, members, "등록 인원", false);
  homeStat(HOME_CIRC_RX, HOME_CARD, HOME_LINE, HOME_GREEN, total, "총 포인트", true);

  // 오늘 +지급 / -사용 — 화살표 · '오늘:' · +A · ' / ' · -B · 화살표 를 가운데로 모아 놓는다
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
    // ★ 사이 띄움은 픽셀로 준다 — getTextBounds 가 끝 공백을 폭에 넣지 않아 '오늘: ' 의
    //   뒤 칸이 사라졌다('오늘:+0 /-0').
    const char *lab = "오늘:", *sep = "/";
    const int g = 5;  // 조각 사이
    const int w = 12 + g + homeTextW(lab, HOME_FONT_M) + g + homeTextW(a, HOME_FONT_M) + 1 + g +
                  homeTextW(sep, HOME_FONT_M) + g + homeTextW(b, HOME_FONT_M) + 1 + g + 12;
    int x = CX - w / 2;
    homeArrow(x + 5, HOME_TODAY_CY, true, HOME_ORANGE);
    x += 12 + g;
    x += homeTextL(lab, x, HOME_TODAY_CY, HOME_GREY, HOME_FONT_M) + g;
    x += homeTextL(a, x, HOME_TODAY_CY, HOME_GREEN, HOME_FONT_M, true) + g;
    x += homeTextL(sep, x, HOME_TODAY_CY, HOME_GREY, HOME_FONT_M) + g;
    x += homeTextL(b, x, HOME_TODAY_CY, HOME_RED, HOME_FONT_M, true) + g;
    homeArrow(x + 5, HOME_TODAY_CY, false, HOME_RED);
  }

  homeFootBand();
}
