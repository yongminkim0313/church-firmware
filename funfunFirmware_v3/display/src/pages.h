#pragma once
// funfunFirmware — 대기 화면 네 장 240×240 원형 (홈 뒤로 돌아가며 띄운다)
//
// 홈(home.h)과 같은 띠 · 바탕 · '카드를 대 주세요' 를 두르고, 가운데만 바꾼다.
// 2026-11-06 저녁: 앱의 내역 화면에 맞췄다 — 흰 카드 하나에 줄을 가름선으로 나누고, 줄 왼쪽에
// 앱과 같은 분류 뱃지(art.h)를 단다. 하는 일은 그대로다.
//
//   최근 내역    ┌ (↑) 오이안            +5P ┐   줄마다 카드 한 장 — 이름 · 분류·사유 · 몇 분 전
//                └     출석 · 3분 전          ┘   PAGE_RECENT_N 줄 — 넷이 넘으면 천천히 아래로 굴린다
//   오늘 출석    [(사람) 12명 출석]               이름 알약 세 칸씩, 넘치면 마지막 칸이 '외 N명'
//                [오이안][김하늘][박서준] …
//   출석 랭킹    (1) 오이안                 8회   이번 달 출석 많은 순 위에서 PAGE_RANK_N 줄
//   포인트 랭킹  (1) 김하늘              1,205P   잔액 많은 순 — 1등 줄만 주황으로 칠한다
//   ★ 최근 내역 · 두 랭킹은 원 안에 다 안 들면 천천히 아래로 굴린다(아래 '목록 굴리기').
//
// · 숫자는 서버에서 받는다(main.cpp 의 fetchBoard) —
//     최근 내역   GET /api/talent/feed?limit=PAGE_RECENT_N   (data[].name·delta·category·reason·agoSec)
//     나머지 셋   GET /api/talent/signage                     (today[] · month[] · points[])
//   signage 의 순위는 서버가 정한 등수 그대로다(공동 순위는 1·2·2·4). 0회·0P 줄은 서버가 빼서 준다.
// · 받은 것이 없는 장(오늘 아무도 안 왔다 · 아직 못 받았다)은 돌리지 않고 건너뛴다(pageHasData).
// · 위 띠의 점은 몇째 장인가다 — 홈이 첫째.
// · 원 아래로 갈수록 좁다 — 줄은 y 60..190 안에 두고 폭은 188 로 맞췄다(맨 아래 줄 모서리도 원 안).

// 최근 내역 줄 수. 원 안에는 두 줄짜리 카드 넷이 든다 — 그보다 많으면 위에서 잠깐 멈췄다가
// 천천히 아래로 굴리고, 끝에서 잠깐 멈춘 뒤 다음 장으로 넘어간다(아래 '최근 내역 굴리기').
#define PAGE_RECENT_N 15
#define PAGE_TODAY_N 12  // 오늘 출석 이름 칸 수(3 × 4). 넘치면 마지막 칸이 '외 N명'
#define PAGE_RANK_N 15   // 순위 줄 수 — 원 안에는 다섯이 든다, 넘치면 굴린다
#define PAGE_NAME_LEN 32

#define PAGE_FONT_XS u8g2_font_gulim11_t_korean2  // 카드 아랫줄(분류 · 몇 분 전)

#define PAGE_ROW_X 26  // 줄 카드 왼쪽 끝
#define PAGE_ROW_W 188
#define PAGE_TOP_Y 60  // 가운데 칸 위 끝(위 띠의 타원 · 점 아래)

// 메달 — 앱 순위(Art.swift 의 1 · 2 등 색)와 같은 쪽
#define PAGE_GOLD RGB565(242, 196, 56)
#define PAGE_SILVER RGB565(189, 194, 204)
#define PAGE_BRONZE RGB565(214, 146, 92)
#define PAGE_CARD_R 12  // 카드 모서리

enum IdlePage { PAGE_HOME, PAGE_RECENT, PAGE_TODAY, PAGE_ATTEND, PAGE_POINTS, PAGE_COUNT };

struct PageRecent {
  char name[PAGE_NAME_LEN];
  char why[48];     // '분류 · 사유' — 비면 '지급' · '사용'
  long delta;
  uint32_t agoSec;  // 서버가 센 경과 초(받은 때 기준)
};
struct PageRank {
  char name[PAGE_NAME_LEN];
  int rank;
  long value;  // 회 · P
};
// ★ 통째로 memset 한 뒤 채운다 — main.cpp 가 이 구조를 바이트째 섞어 '바뀌었나' 를 본다(drawIdle).
struct PagesData {
  bool recentOk;  // 최근 내역을 한 번이라도 받았다
  int recentN;
  PageRecent recent[PAGE_RECENT_N];
  bool boardOk;   // 알림판(signage)을 한 번이라도 받았다
  int todayN;     // 오늘 출석 **전체** 수(이름은 앞 PAGE_TODAY_N 명만 든다)
  char today[PAGE_TODAY_N][PAGE_NAME_LEN];
  int mon;        // 몇 월인가 — 출석 랭킹 제목
  int monthN;
  PageRank month[PAGE_RANK_N];
  int pointsN;
  PageRank points[PAGE_RANK_N];
};
static PagesData pagesData = {};
static uint32_t pagesRecentAt = 0;  // 최근 내역을 받은 때(millis) — '몇 분 전' 에 이만큼 더한다

static bool pageHasData(IdlePage p) {
  const PagesData &d = pagesData;
  switch (p) {
    case PAGE_HOME: return true;
    case PAGE_RECENT: return d.recentOk && d.recentN > 0;
    case PAGE_TODAY: return d.boardOk && d.todayN > 0;
    case PAGE_ATTEND: return d.boardOk && d.monthN > 0;
    case PAGE_POINTS: return d.boardOk && d.pointsN > 0;
    default: return false;
  }
}

// 폭 maxW 에 들게 뒤를 잘라 '..' 를 붙인다(한글은 글자 단위로 — UTF-8 이어지는 바이트를 함께 뗀다)
static void pageFit(char *s, size_t cap, int maxW, const uint8_t *font) {
  if (homeTextW(s, font) <= maxW) return;
  size_t n = strlen(s);
  while (n) {
    do { n--; } while (n && ((uint8_t)s[n] & 0xC0) == 0x80);
    s[n] = '\0';
    strlcat(s, "..", cap);
    if (homeTextW(s, font) <= maxW) return;
    s[n] = '\0';
  }
}

// 굴림(korean2)에는 가운뎃점 '·' 이 없다(폭 0 — 그냥 두면 '출석  방금' 처럼 빈칸만 남는다).
// 그 자리에 점을 손으로 찍는다. 그린 폭을 돌려준다. x 에서 시작, cy 가운데.
#define PAGE_MIDDOT "\xC2\xB7"
#define PAGE_DOT_W 11 // 점 한 칸의 폭(앞뒤 띄움 포함)
static int pageTextDots(const char *s, int x, int cy, uint16_t color, const uint8_t *font,
                        bool draw = true) {
  char seg[96];
  int w = 0;
  for (;;) {
    const char *dot = strstr(s, PAGE_MIDDOT);
    const size_t n = dot ? (size_t)(dot - s) : strlen(s);
    strlcpy(seg, s, n + 1 < sizeof(seg) ? n + 1 : sizeof(seg));
    // 점 앞뒤의 띄어쓰기는 점 칸이 맡는다
    size_t k = strlen(seg);
    while (k && seg[k - 1] == ' ') seg[--k] = '\0';
    const char *t = seg;
    while (*t == ' ') t++;
    if (*t) w += draw ? homeTextL(t, x + w, cy, color, font) : homeTextW(t, font);
    if (!dot) return w;
    if (draw) gfx->fillRect(x + w + PAGE_DOT_W / 2 - 1, cy - 1, 2, 2, color);
    w += PAGE_DOT_W;
    s = dot + 2;
  }
}

// '몇 분 전' — 서버가 센 경과 초에 받은 뒤 흐른 시간을 더한 값
static void pageAgo(uint32_t sec, char *out, size_t cap) {
  if (sec < 60)
    snprintf(out, cap, "방금");
  else if (sec < 3600)
    snprintf(out, cap, "%lu분 전", (unsigned long)(sec / 60));
  else if (sec < 86400)
    snprintf(out, cap, "%lu시간 전", (unsigned long)(sec / 3600));
  else
    snprintf(out, cap, "%lu일 전", (unsigned long)(sec / 86400));
}

// 가운데 칸이 빈 장 — 돌릴 때는 건너뛰지만, 받기 전에 시험('g')으로 넘기면 이것이 뜬다
static void pageEmpty(const char *msg) {
  homeTextC(msg, CX, 124, HOME_GREY, HOME_FONT_M);
}

// 분류 뱃지 — 앱 FFArt.category · line 과 같은 고르기. why 는 '분류 · 사유'(사유가 없으면 분류만).
// 분류 이름으로 먼저 고르고, 모르는 분류(교회가 늘린 것)는 낱말로 짚고, 그래도 못 짚으면 곰 포인트.
static const uint16_t *pageBadgeOf(const char *why) {
  char cat[48];
  strlcpy(cat, why ? why : "", sizeof(cat));
  if (char *dot = strstr(cat, PAGE_MIDDOT)) {
    while (dot > cat && dot[-1] == ' ') dot--;
    *dot = '\0';
  }
  if (!strcmp(cat, "출석")) return ART_BADGE_ATTEND;
  if (!strcmp(cat, "전도/초청") || !strcmp(cat, "새친구")) return ART_BADGE_INVITE;
  if (!strcmp(cat, "간식 교환")) return ART_BADGE_SNACK;
  if (!strcmp(cat, "예배 태도")) return ART_BADGE_MOOD;
  const char *w = why ? why : "";
  if (strstr(w, "출석") || strstr(w, "예배")) return ART_BADGE_ATTEND;
  if (strstr(w, "초청") || strstr(w, "초대") || strstr(w, "전도") || strstr(w, "친구"))
    return ART_BADGE_INVITE;
  if (strstr(w, "간식") || strstr(w, "상품") || strstr(w, "교환")) return ART_BADGE_SNACK;
  if (strstr(w, "태도") || strstr(w, "마음")) return ART_BADGE_MOOD;
  return ART_BADGE_POINT;
}

// ── 최근 내역 ─────────────────────────────────────────────────────
#define PAGE_RECENT_H 34     // 줄 높이 — 카드 하나를 가름선으로 나눠 줄 사이 틈이 없다
#define PAGE_RECENT_STEP 34
#define PAGE_RECENT_TX (PAGE_ROW_X + 8 + ART_BADGE_PX + 7)  // 글자가 시작하는 곳(가름선도 여기서)

// 줄 하나를 y(위 끝)에 그린다 — 앱 내역 줄처럼 (뱃지) 이름 / 분류 · 사유 · 몇 분 전 · 오른쪽 +5P.
// passed = 받은 뒤 흐른 초('몇 분 전' 에 더한다). 흰 카드 위에 그린다(listCardDraw).
static void pageRecentRow(const PageRecent &r, int y, uint32_t passed) {
  const int cy = y + PAGE_RECENT_H / 2;
  const bool earn = r.delta >= 0;
  homeArt(pageBadgeOf(r.why), ART_BADGE_PX, PAGE_ROW_X + 8, cy - ART_BADGE_PX / 2);

  // 오른쪽 '+5P' · '-3P' — 앱처럼 지급 초록 · 사용 주황
  char val[20], num[16];
  commafy(earn ? r.delta : -r.delta, num, sizeof(num));
  snprintf(val, sizeof(val), "%s%sP", earn ? "+" : "-", num);
  const int valW = homeTextW(val, HOME_FONT_M) + 1;
  const int right = PAGE_ROW_X + PAGE_ROW_W - 10;
  homeTextL(val, right - valW, cy, earn ? HOME_EARN : HOME_SPEND, HOME_FONT_M, true);

  // 가운데 두 줄 — 이름 / '분류 · 사유 · 3분 전'(길면 사유를 자르고 시각은 남긴다)
  const int tx = PAGE_RECENT_TX, room = right - valW - 6 - tx;
  char nm[PAGE_NAME_LEN + 4];
  strlcpy(nm, r.name[0] ? r.name : "(이름 없음)", sizeof(nm));
  pageFit(nm, sizeof(nm), room, HOME_FONT_S);
  homeTextL(nm, tx, cy - 7, HOME_INK, HOME_FONT_S, true);

  char ago[16], why[sizeof(r.why) + 4], sub[sizeof(why) + 24];
  pageAgo(r.agoSec + passed, ago, sizeof(ago));
  strlcpy(why, r.why[0] ? r.why : (earn ? "지급" : "사용"), sizeof(why));
  // 넘치면 사유를 떼고 분류만('특별 미션') — 그래도 넘치면 뒤를 자른다
  const int whyRoom = room - homeTextW(ago, PAGE_FONT_XS) - PAGE_DOT_W;
  if (pageTextDots(why, 0, 0, 0, PAGE_FONT_XS, false) > whyRoom) {
    char *dot = strstr(why, PAGE_MIDDOT);
    if (dot) {
      while (dot > why && dot[-1] == ' ') dot--;
      *dot = '\0';
    }
    pageFit(why, sizeof(why), whyRoom, PAGE_FONT_XS);
  }
  snprintf(sub, sizeof(sub), "%s " PAGE_MIDDOT " %s", why, ago);
  pageTextDots(sub, tx, cy + 8, HOME_GREY, PAGE_FONT_XS);
}

// ── 오늘 출석 ─────────────────────────────────────────────────────
static void pageToday() {
  const PagesData &d = pagesData;
  if (!d.todayN) return pageEmpty("아직 아무도 없어요");

  // 머리 — (토끼 뱃지) 12명 출석
  {
    char s[24];
    snprintf(s, sizeof(s), "%d명 출석", d.todayN);
    const int cy = 72, w = ART_BADGE_PX + 6 + homeTextW(s, HOME_FONT_M) + 1;
    const int x = CX - w / 2;
    homeArt(ART_BADGE_ATTEND, ART_BADGE_PX, x, cy - ART_BADGE_PX / 2);
    homeTextL(s, x + ART_BADGE_PX + 6, cy, HOME_INK, HOME_FONT_M, true);
  }

  // 이름 칩 — 흰 카드 안에 세 칸씩 네 줄(앱 학생 카드의 '초4 · 소망반' 회색 칩).
  // 넘치면 마지막 칸을 '외 N명' 으로 바꾼다(옅은 주황).
  const int cols = 3, cw = 56, ch = 19, gx = 5, top = 94, step = 23;
  const int shown = d.todayN < PAGE_TODAY_N ? d.todayN : PAGE_TODAY_N;
  const bool more = d.todayN > PAGE_TODAY_N;
  const int names = more ? PAGE_TODAY_N - 1 : shown;
  const int rows = (shown + cols - 1) / cols;
  gfx->fillRoundRect(PAGE_ROW_X, top - 7, PAGE_ROW_W, rows * step + 10, PAGE_CARD_R, HOME_CARD);
  for (int i = 0; i < shown; i++) {
    const int row = i / cols, col = i % cols;
    const int inRow = (row == (shown - 1) / cols) ? shown - row * cols : cols;  // 끝 줄은 가운데로
    const int x0 = CX - (inRow * cw + (inRow - 1) * gx) / 2;
    const int x = x0 + col * (cw + gx), y = top + row * step;
    char s[PAGE_NAME_LEN + 4];
    if (i < names) {
      gfx->fillRoundRect(x, y, cw, ch, ch / 2, HOME_CHIP);
      strlcpy(s, d.today[i], sizeof(s));
      pageFit(s, sizeof(s), cw - 8, HOME_FONT_S);
      homeTextC(s, x + cw / 2, y + ch / 2, HOME_INK, HOME_FONT_S);
    } else {
      gfx->fillRoundRect(x, y, cw, ch, ch / 2, HOME_BRAND_SOFT);
      snprintf(s, sizeof(s), "외 %d명", d.todayN - names);
      homeTextC(s, x + cw / 2, y + ch / 2, HOME_BRAND, HOME_FONT_S, true);
    }
  }
}

// ── 랭킹 (출석 · 포인트) ─────────────────────────────────────────
#define PAGE_RANK_H 26  // 다섯 줄(130)이 보이는 칸에 꼭 든다
#define PAGE_RANK_STEP 26

#define PAGE_RANK_TX (PAGE_ROW_X + 34)  // 이름이 시작하는 곳(가름선도 여기서)

// 순위 줄 하나를 y(위 끝)에 그린다 — (메달) 이름 · 오른쪽 값. 흰 카드 위에 그린다(listCardDraw).
static void pageRankRow(const PageRank &r, int y, const char *unit, uint16_t valColor) {
  const int cy = y + PAGE_RANK_H / 2;

  // 등수 메달 — 1·2·3 은 금·은·동, 그 밑은 회색 칩
  const int mx = PAGE_ROW_X + 17;
  char rk[8];
  snprintf(rk, sizeof(rk), "%d", r.rank);
  if (r.rank >= 1 && r.rank <= 3) {
    const uint16_t medal = r.rank == 1 ? PAGE_GOLD : (r.rank == 2 ? PAGE_SILVER : PAGE_BRONZE);
    gfx->fillCircle(mx, cy, 9, medal);
    homeTextC(rk, mx + 1, cy, RGB565_WHITE, HOME_FONT_S, true);
  } else {
    gfx->fillCircle(mx, cy, 9, HOME_CHIP);
    homeTextC(rk, mx + 1, cy, HOME_GREY, HOME_FONT_S, true);
  }

  // 오른쪽 값 — '8회' · '1,205P'
  char val[24], num[16];
  commafy(r.value, num, sizeof(num));
  snprintf(val, sizeof(val), "%s%s", num, unit);
  const int valW = homeTextW(val, HOME_FONT_M) + 1;
  const int right = PAGE_ROW_X + PAGE_ROW_W - 10;
  homeTextL(val, right - valW, cy, valColor, HOME_FONT_M, true);

  const int tx = PAGE_RANK_TX;
  char nm[PAGE_NAME_LEN + 4];
  strlcpy(nm, r.name[0] ? r.name : "(이름 없음)", sizeof(nm));
  pageFit(nm, sizeof(nm), right - valW - 6 - tx, HOME_FONT_S);
  homeTextL(nm, tx, cy, HOME_INK, HOME_FONT_S, true);
}

// ── 목록 굴리기 (최근 내역 · 출석 랭킹 · 포인트 랭킹) ─────────────
// 줄이 원 안에 다 들지 않으면(최근 내역은 넷 · 랭킹은 다섯 넘게) 천천히 아래로 굴린다. 프레임마다
// 글자를 다시 그리면 한글(U8g2) 열다섯 줄에 40ms 가 든다 — 그래서 **목록 전체를 긴 그림판(PSRAM)에
// 한 번만** 그려 두고, 프레임마다 보이는 창만 화면 버퍼로 옮긴 뒤 그 칸(y 60..190)만 패널로 보낸다.
//   · 그림판은 하나를 돌려 쓴다 — 한 번에 한 장만 보인다. 장이 바뀌면 그 장의 목록으로 다시 그린다.
//   · 그림판은 비침색(TRANSPARENT_KEY)으로 채운 뒤 줄을 그린다. 옮길 때 비침색은 건너뛰어 줄
//     사이·카드 모서리 밖에는 바탕(크림 · 점 무늬)이 그대로 보인다. 바탕은 굴러가지 않는다.
//   · 바탕은 장을 통째로 그릴 때(pageDraw) 줄을 깔기 **전에** 그 칸을 떠 둔다(listBack).
//   · 그림판은 장 · 내용 · (최근 내역은) '몇 분 전' 의 분이 바뀔 때만 다시 그린다(listBoardSig).
// 언제 멈추고 언제 굴리나 · 그동안 서버 받기를 쉬는 것은 main.cpp(listScroll*)가 정한다.
// 줄이 보이는 칸 — 위 띠의 타원(y 54 까지) 아래부터 아래 띠의 타원(y 190 부터) 위까지.
#define PAGE_VIEW_TOP PAGE_TOP_Y
#define PAGE_VIEW_BOT (HOME_FOOT_Y - 8)
#define PAGE_VIEW_H (PAGE_VIEW_BOT - PAGE_VIEW_TOP)
#define PAGE_BOARD_H                                                                         \
  (PAGE_RECENT_N * PAGE_RECENT_STEP > PAGE_RANK_N * PAGE_RANK_STEP ? PAGE_RECENT_N * PAGE_RECENT_STEP \
                                                                   : PAGE_RANK_N * PAGE_RANK_STEP)
static Arduino_Canvas *listBoard = nullptr;  // 240 × PAGE_BOARD_H — PSRAM 약 240KB
static uint16_t *listBack = nullptr;         // 보이는 칸의 바탕(240 × 130) — PSRAM 약 62KB
static uint32_t listBoardSig = 0;
static bool listBoardFailed = false;  // 못 잡았다 — 예전처럼 원 안에 드는 줄만 그린다
static int listOff = 0;               // 지금 굴린 만큼(px) — main.cpp 가 정한다

static bool pageScrolls(IdlePage p) { return p == PAGE_RECENT || p == PAGE_ATTEND || p == PAGE_POINTS; }
static int listRows(IdlePage p) {
  const PagesData &d = pagesData;
  const int n = p == PAGE_RECENT ? d.recentN : (p == PAGE_ATTEND ? d.monthN : (p == PAGE_POINTS ? d.pointsN : 0));
  const int cap = p == PAGE_RECENT ? PAGE_RECENT_N : PAGE_RANK_N;
  return n < cap ? n : cap;
}
static int listStep(IdlePage p) { return p == PAGE_RECENT ? PAGE_RECENT_STEP : PAGE_RANK_STEP; }
static int listRowH(IdlePage p) { return p == PAGE_RECENT ? PAGE_RECENT_H : PAGE_RANK_H; }
static int listContentH(IdlePage p) {
  const int n = listRows(p);
  return n ? n * listStep(p) - (listStep(p) - listRowH(p)) : 0;
}
// 굴릴 수 있는 길이 — 0 이면 다 들어간다(굴리지 않는다)
static int listScrollMax(IdlePage p) {
  if (listBoardFailed || !pageScrolls(p)) return 0;
  const int m = listContentH(p) - PAGE_VIEW_H;
  return m > 0 ? m : 0;
}

static void listRowDraw(IdlePage p, int i, int y, uint32_t passed) {
  const PagesData &d = pagesData;
  if (p == PAGE_RECENT) pageRecentRow(d.recent[i], y, passed);
  else if (p == PAGE_ATTEND) pageRankRow(d.month[i], y, "회", HOME_INK);
  else pageRankRow(d.points[i], y, "P", HOME_BRAND);  // 앱의 잔액과 같은 주황
}

// 흰 카드 하나에 줄 n 개 — 위 끝 y0. 줄 사이는 글자 시작점부터 가름선(앱 내역 목록과 같은 꼴).
static void listCardDraw(IdlePage p, int y0, int n, uint32_t passed) {
  const int step = listStep(p);
  gfx->fillRoundRect(PAGE_ROW_X, y0, PAGE_ROW_W, n * step, PAGE_CARD_R, HOME_CARD);
  const int lx = p == PAGE_RECENT ? PAGE_RECENT_TX : PAGE_RANK_TX;
  for (int i = 0; i < n; i++) {
    if (i) gfx->drawFastHLine(lx, y0 + i * step, PAGE_ROW_X + PAGE_ROW_W - 10 - lx, HOME_LINE);
    listRowDraw(p, i, y0 + i * step, passed);
  }
}

// 그림판을 그 장의 지금 내용으로. 바뀐 것이 없으면 그냥 돌아간다. 못 잡으면 false.
static bool listBoardEnsure(IdlePage p, uint32_t passed) {
  if (listBoardFailed) return false;
  if (!listBoard) {
    listBoard = new Arduino_Canvas(240, PAGE_BOARD_H, nullptr);
    if (!listBoard || !listBoard->begin(GFX_SKIP_OUTPUT_BEGIN)) {
      delete listBoard;
      listBoard = nullptr;
      listBoardFailed = true;
      Serial.println("[목록] 그림판을 못 잡았다 — 굴리지 않고 원 안에 드는 줄만 띄운다");
      return false;
    }
  }
  const PagesData &d = pagesData;
  uint32_t sig = 2166136261u;
  auto mix = [&](uint32_t v) { sig = (sig ^ v) * 16777619u; };
  auto mixBytes = [&](const void *v, size_t n) {
    for (size_t i = 0; i < n; i++) mix(((const uint8_t *)v)[i]);
  };
  mix(p);
  mix((uint32_t)listRows(p));
  if (p == PAGE_RECENT) {
    mixBytes(d.recent, sizeof(d.recent));
    mix(passed / 60);
  } else {
    mixBytes(p == PAGE_ATTEND ? d.month : d.points, sizeof(d.month));
  }
  if (sig == listBoardSig) return true;
  listBoardSig = sig;

  const uint32_t t0 = micros();
  Arduino_Canvas *screen = gfx;  // 줄 그리는 함수들은 gfx 에 그린다 — 잠깐 그림판으로 돌린다
  gfx = listBoard;
  gfx->fillRect(0, 0, 240, listContentH(p) + 1, TRANSPARENT_KEY);
  listCardDraw(p, 0, listRows(p), passed);
  gfx = screen;
  Serial.printf("[목록] 그림판 %d줄 다시 그림 (%lums)\n", listRows(p),
                (unsigned long)((micros() - t0 + 500) / 1000));
  return true;
}

// 보이는 칸을 화면 버퍼에 다시 깐다 — 바탕을 되돌리고, 그림판의 off 줄부터 비침색만 빼고 얹는다.
// 줄 카드가 있는 가로 폭(PAGE_ROW_X ~)만 만진다 — 그 밖은 바탕 그대로다.
static void listCompose(IdlePage p, int off) {
  uint16_t *fb = gfx->getFramebuffer();
  const uint16_t *board = listBoard ? listBoard->getFramebuffer() : nullptr;
  if (!fb || !board || !listBack) return;
  const int x0 = PAGE_ROW_X, x1 = PAGE_ROW_X + PAGE_ROW_W, ch = listContentH(p);
  for (int y = 0; y < PAGE_VIEW_H; y++) {
    uint16_t *dst = fb + (PAGE_VIEW_TOP + y) * 240;
    memcpy(dst + x0, listBack + y * 240 + x0, (x1 - x0) * 2);
    const int by = y + off;
    if (by >= ch) continue;
    const uint16_t *src = board + by * 240;
    for (int x = x0; x < x1; x++)
      if (src[x] != TRANSPARENT_KEY) dst[x] = src[x];
  }
}

// 목록 장 하나 — 장을 통째로 그릴 때(pageDraw) 띠 사이에서 부른다.
static void pageList(IdlePage p) {
  if (!listRows(p)) return pageEmpty("아직 기록이 없어요");
  const uint32_t passed = (millis() - pagesRecentAt) / 1000;
  // 줄을 깔기 전의 바탕을 떠 둔다 — 굴릴 때마다 이것으로 칸을 되돌린다
  if (!listBack && !listBoardFailed) listBack = (uint16_t *)ps_malloc(240 * PAGE_VIEW_H * 2);
  if (listBack && listBoardEnsure(p, passed)) {
    memcpy(listBack, gfx->getFramebuffer() + PAGE_VIEW_TOP * 240, 240 * PAGE_VIEW_H * 2);
    const int m = listScrollMax(p);
    if (listOff > m) listOff = m;
    listCompose(p, listOff);
    return;
  }
  // 그림판을 못 잡았다 — 예전처럼 원 안에 드는 줄만 바로 그린다
  listBoardFailed = true;
  int fit = PAGE_VIEW_H / listStep(p);
  if (fit > listRows(p)) fit = listRows(p);
  listCardDraw(p, PAGE_TOP_Y, fit, passed);
}

// 대기 화면 한 장. 홈은 home.h 의 homeDraw 로 — 여기는 나머지 넷.
static void pageDraw(IdlePage p, const char *timeText, bool isOnline) {
  if (p == PAGE_HOME) return homeDraw(timeText, isOnline, PAGE_HOME, PAGE_COUNT);
  char title[32];
  switch (p) {
    case PAGE_RECENT: strlcpy(title, "최근 내역", sizeof(title)); break;
    case PAGE_TODAY: strlcpy(title, "오늘 출석", sizeof(title)); break;
    case PAGE_ATTEND:
      if (pagesData.mon)
        snprintf(title, sizeof(title), "%d월 출석 랭킹", pagesData.mon);
      else
        strlcpy(title, "출석 랭킹", sizeof(title));
      break;
    default: strlcpy(title, "포인트 랭킹", sizeof(title)); break;
  }
  homeBackdrop();
  homeTopBand(timeText, isOnline, title);
  homePageDots(p, PAGE_COUNT);
  if (p == PAGE_TODAY)
    pageToday();
  else
    pageList(p);  // 최근 내역 · 출석 랭킹 · 포인트 랭킹 — 넘치면 굴린다
  homeFootBand();
}
