#pragma once
// funfunFirmware — 와이파이 설정 모드 (폰으로 와이파이를 적어 넣는다)
//
// nfcProject/master 의 portal.h 를 이 보드(240 원형 GC9A01 · BOOT 버튼 · 터치 없음)에
// 맞게 옮겨 온 것이다(2026-11-06). **앱은 만들지 않는다.** 보드가 스스로 와이파이(SoftAP)를
// 띄우고, 거기에 붙은 폰의 브라우저로 설정을 받는다 — 캡티브 포털이다:
//
//   1) 화면에 뜬 QR 을 폰 카메라로 찍는다 → "이 네트워크에 연결"
//      (QR 에 AP 이름·비밀번호가 들어 있어 손으로 칠 것이 없다)
//   2) 붙으면 설정 페이지가 **저절로** 뜬다. 안 뜨면 브라우저에 192.168.4.1
//   3) 목록에서 교회 와이파이를 고르고 비밀번호를 넣고 '저장'
//   4) 보드가 **그 자리에서 붙어 본다.** 붙으면 플래시에 적고 다시 시작한다.
//      못 붙으면 까닭(비밀번호 틀림 / 이름이 안 보임 / 신호 약함)을 그 화면에 보여 준다.
//
// ★ 비밀번호가 맞는 것을 **확인한 뒤에만** 저장한다. 틀린 것이 1순위로 올라앉아
//   그 뒤로 영영 못 붙는 일을 막으려는 것이다.
//
// ── 들어가는 길 ──────────────────────────────────────────────────
//   · 아는 와이파이가 하나도 없을 때(처음 켠 기기) — 켜자마자 들어간다.
//     아는 곳이 없는 동안에는 저절로 닫히지 않는다(닫아 봐야 붙을 곳이 없다).
//   · 아는 곳이 안 보일 때 저절로 — 켠 뒤 **한 번도 못 붙었고** 목록을
//     PORTAL_AUTO_ROUNDS 바퀴 돌아도 못 붙으면. 한 번이라도 붙었던 뒤에는 들어가지
//     않는다(공유기를 잠깐 껐을 때 기기가 설정 모드로 달아나면 안 된다).
//   · BOOT 버튼을 PORTAL_HOLD_MS(5초) 누르고 있기 — 아무 때나.
//     ★ 켤 때 누르고 있는 길은 **쓰지 않는다.** BOOT(GPIO0)를 쥔 채 전원을 넣으면
//       칩이 굽기 모드로 들어가 펌웨어가 아예 돌지 않는다.
//   · 시리얼 'w'
//
// ── 알아 둘 것 ────────────────────────────────────────────────────
//   · 설정 모드에서는 서버에 못 물어 조회·지급이 안 된다 — 그동안 읽힌 카드는 버린다
//     (몇 분 전 아이를 나중에 띄우면 그 자리 아이의 것으로 읽힌다).
//   · 폰이 붙어 있지 않은 채로 PORTAL_IDLE_MS 가 지나면 걷고 평소대로 돌아간다.
//   · 이 페이지에 기기 키(TALENT_DEVICE_KEY)는 띄우지 않는다. 와이파이만 다룬다.

#if PORTAL_ENABLE

#include <DNSServer.h>
#include <WebServer.h>
#include <esp_wifi.h>  // AP 설정·출력을 찍는다
#include <qrcode.h>

static WebServer portalWeb(80);
static DNSServer portalDns;

static bool portalOn = false;
static uint32_t portalQuietAt = 0;   // 폰이 하나도 안 붙어 있게 된 시각(0 = 붙어 있다)
static uint32_t portalRebootAt = 0;  // 0 이 아니면 이 시각에 보드를 다시 세운다
static char portalAp[33] = "";       // 띄운 AP 이름

// 둘러본 결과 — 설정 모드에 들어갈 때 한 번, 웹에서 '다시 찾기' 를 누르면 또.
#define PORTAL_SCAN_MAX 24
static struct {
  char ssid[33];
  int8_t rssi;
  bool lock;
} portalFound[PORTAL_SCAN_MAX];
static int portalFoundN = 0;
static bool portalScanning = false;
static bool portalScanWant = false;
static uint32_t portalScanAt = 0;

// 받아 적은 것으로 붙어 보는 중인가
enum PortalTry : uint8_t { PT_IDLE, PT_RUN, PT_OK, PT_FAIL };
static PortalTry portalTry = PT_IDLE;
static uint32_t portalTryAt = 0;
static char portalSsid[33] = "";
static char portalPass[65] = "";
static char portalWhy[128] = "";

// 끊긴 까닭 — 이 펌웨어는 평소에 와이파이 이벤트를 듣지 않아서 설정 모드가 따로 듣는다.
static volatile bool portalStaDown = false;     // begin() 뒤 '끊겼다' 가 왔다
static volatile uint8_t portalLastReason = 0;   // 그때의 까닭(0 = 아직 없다)
static wifi_event_id_t portalEvtId = 0;

static const uint32_t PORTAL_TRY_MS = 20000;      // 한 번 붙어 보고 이만큼 기다린다
static const uint32_t PORTAL_TRY_MIN_MS = 3000;   // 끊김 신호는 이만큼 지난 뒤부터 믿는다
static const uint32_t PORTAL_SCAN_MAX_MS = 12000; // 둘러보기가 이만큼 넘으면 접는다

static bool portalActive() { return portalOn; }

// ── 폰이 AP 에 붙고 떨어지는 것 찍기 ────────────────────────────────
// "QR 을 찍었는데 접속이 안 된다" 를 가르려면 어디서 멈췄는지 알아야 한다:
//   붙음(연결) → 주소 받음(DHCP) → 웹 요청(설정 창) — 셋 중 어디까지 왔나.
// 와이파이 이벤트는 다른 태스크(이벤트 태스크)에서 불린다 — 거기서 Serial 을 쓰면 줄이 사라질
// 수 있어(main.cpp 의 pendingLog 머리말) 칸에 적어 두고 portalService(본 고리)가 찍는다.
struct PortalEvt {
  uint8_t kind;    // 1 붙음 · 2 떨어짐 · 3 주소 줌
  uint8_t mac[6];
  uint16_t reason;  // 떨어진 까닭(2 일 때)
  uint32_t ip;     // 준 주소(3 일 때)
};
#define PORTAL_EVT_N 16
static PortalEvt portalEvt[PORTAL_EVT_N];
static volatile uint8_t portalEvtHead = 0, portalEvtTail = 0;  // 이벤트 태스크가 head, 본 고리가 tail
static wifi_event_id_t portalApEvtId = 0;
static uint32_t portalReqN = 0;  // 웹 요청 수

static void portalEvtPush(const PortalEvt &e) {
  const uint8_t next = (portalEvtHead + 1) % PORTAL_EVT_N;
  if (next == portalEvtTail) return;  // 꽉 찼다 — 버린다
  portalEvt[portalEvtHead] = e;
  portalEvtHead = next;
}

static void portalEvtPrint() {
  while (portalEvtTail != portalEvtHead) {
    const PortalEvt &e = portalEvt[portalEvtTail];
    char mac[18];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", e.mac[0], e.mac[1], e.mac[2],
             e.mac[3], e.mac[4], e.mac[5]);
    if (e.kind == 1) {
      Serial.printf("[설정] 폰이 AP 에 붙었다 %s (지금 %d대)\n", mac, WiFi.softAPgetStationNum());
    } else if (e.kind == 2) {
      Serial.printf("[설정] 폰이 AP 에서 떨어졌다 %s — 까닭 %u(%s)\n", mac, e.reason,
                    WiFi.disconnectReasonName((wifi_err_reason_t)e.reason));
    } else if (e.kind == 3) {
      Serial.printf("[설정] 폰에 주소를 줬다 %s\n", IPAddress(e.ip).toString().c_str());
    }
    portalEvtTail = (portalEvtTail + 1) % PORTAL_EVT_N;
  }
}

// ── 접속용 QR ──────────────────────────────────────────────────────
// 'WIFI:T:WPA;S:<이름>;P:<비밀번호>;;' 는 아이폰·안드로이드 카메라가 알아보는 규약이다.
// 규약에서 \ ; , : " 는 앞에 \ 를 붙여 막는다.
static void portalQrEsc(char *out, size_t cap, const char *s) {
  size_t o = 0;
  for (const char *p = s; *p && o + 2 < cap; p++) {
    if (*p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"') out[o++] = '\\';
    out[o++] = *p;
  }
  out[o] = '\0';
}

static void portalQrText(char *out, size_t cap) {
  char s[80], p[140];
  portalQrEsc(s, sizeof(s), portalAp);
  portalQrEsc(p, sizeof(p), PORTAL_AP_PASS);
  if (p[0])
    snprintf(out, cap, "WIFI:T:WPA;S:%s;P:%s;;", s, p);
  else
    snprintf(out, cap, "WIFI:T:nopass;S:%s;;", s);  // 비밀번호 없는 AP
}

// QR 을 그린다. 바깥에 흰 여백(quiet zone) 네 조각을 꼭 둔다 — 없으면 카메라가 못 읽는다.
// 글자 수에 맞는 가장 작은 판(version)을 고른다: 작을수록 조각이 커서 멀리서도 읽힌다.
static bool portalDrawQr(const char *text, int cy, int maxPx) {
#define PORTAL_QR_VMAX 6
#define PORTAL_QR_SIDE (PORTAL_QR_VMAX * 4 + 17)
  static uint8_t buf[(PORTAL_QR_SIDE * PORTAL_QR_SIDE + 7) / 8];

  // 판마다 담을 수 있는 글자 수(ECC_LOW · 바이트 모드, 1~6판).
  // ★ **라이브러리가 넘치는 것을 막아 주지 않는다**(qrcode.c 의 bb_appendBits 에 자리
  //   검사가 없다 — 긴 글자를 주면 제 버퍼 뒤를 넘어 쓴다). 그래서 우리가 먼저 재서 고른다.
  static const uint8_t QR_CAP[PORTAL_QR_VMAX + 1] = {0, 17, 32, 53, 78, 106, 134};

  const size_t len = strlen(text);
  uint8_t ver = 0;
  for (uint8_t v = 2; v <= PORTAL_QR_VMAX; v++)
    if (len <= QR_CAP[v]) {
      ver = v;
      break;
    }
  if (!ver) {
    Serial.printf("[설정] QR 에 넣을 글자가 너무 깁니다(%u자) — QR 은 그리지 않는다\n",
                  (unsigned)len);
    return false;
  }

  QRCode qr;
  if (qrcode_initText(&qr, buf, ver, ECC_LOW, text) != 0) {
    Serial.println("[설정] QR 을 만들지 못했습니다 — 그리지 않는다");
    return false;
  }

  const int n = qr.size + 8;  // 여백 네 조각씩
  const int s = maxPx / n;    // 조각 하나의 픽셀
  if (s < 2) return false;
  const int side = n * s;
  const int x0 = CX - side / 2, y0 = cy - side / 2;

  gfx->fillRect(x0, y0, side, side, RGB565_WHITE);  // 흰 바탕(여백까지)
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        gfx->fillRect(x0 + (x + 4) * s, y0 + (y + 4) * s, s, s, RGB565_BLACK);
  return true;
}

// 설정 모드 화면 — 240 원 안에: 제목 · QR · AP 이름 · 비밀번호 · 지금 사정.
// QR 은 3판(29조각 + 여백 8 = 37) × 3px = 111px 이다. 원의 모서리에 걸리지 않게
// 가운데보다 조금 위에 둔다(사각형 꼭짓점이 중심에서 ~95px — 반지름 120 안).
static void portalDraw(const char *status) {
  gfx->fillScreen(RGB565_BLACK);
  drawRing(COL_GOLD);

  drawUTF8("와이파이 설정", 36, PX_NOTE, COL_GOLD);

  char qr[180];
  portalQrText(qr, sizeof(qr));
  portalDrawQr(qr, 112, 120);

  drawUTF8(portalAp, 182, PX_NOTE, RGB565_WHITE);
  char pw[80];
  if (PORTAL_AP_PASS[0])
    snprintf(pw, sizeof(pw), "PW %s", PORTAL_AP_PASS);
  else
    snprintf(pw, sizeof(pw), "비밀번호 없음");
  drawUTF8(pw, 200, PX_NOTE, RGB565_WHITE);
  if (status && status[0])
    drawUTF8(status, 220, PX_NOTE, COL_GOLD);
  else
    drawAscii("192.168.4.1", 220, 1, COL_DIM);
  gfx->flush();
}

// ── 둘러보기 ───────────────────────────────────────────────────────
static void portalScanBegin() {
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    portalScanning = false;
    return;
  }
  portalScanning = true;
  portalScanAt = millis();
}

// 결과를 거둔다. 같은 이름이 여러 번 보이면(공유기 두 대) 센 것 하나만 남긴다.
static void portalScanReap() {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - portalScanAt >= PORTAL_SCAN_MAX_MS) {
      WiFi.scanDelete();
      portalScanning = false;
    }
    return;
  }
  portalScanning = false;
  if (n < 0) {
    WiFi.scanDelete();
    return;
  }

  portalFoundN = 0;
  for (int16_t j = 0; j < n; j++) {
    const String ss = WiFi.SSID(j);
    if (!ss.length()) continue;  // 이름을 숨긴 곳 — 직접 입력으로
    int at = -1;
    for (int i = 0; i < portalFoundN; i++)
      if (ss == portalFound[i].ssid) {
        at = i;
        break;
      }
    if (at < 0) {
      if (portalFoundN >= PORTAL_SCAN_MAX) continue;
      at = portalFoundN++;
      snprintf(portalFound[at].ssid, sizeof(portalFound[at].ssid), "%s", ss.c_str());
      portalFound[at].rssi = -127;
      portalFound[at].lock = false;
    }
    if (WiFi.RSSI(j) > portalFound[at].rssi) {
      portalFound[at].rssi = (int8_t)WiFi.RSSI(j);
      portalFound[at].lock = (WiFi.encryptionType(j) != WIFI_AUTH_OPEN);
    }
  }
  WiFi.scanDelete();

  // 센 것부터 — 자기 교회 와이파이는 거의 늘 맨 위에 온다
  for (int i = 1; i < portalFoundN; i++)
    for (int j = i; j > 0 && portalFound[j].rssi > portalFound[j - 1].rssi; j--) {
      auto t = portalFound[j];
      portalFound[j] = portalFound[j - 1];
      portalFound[j - 1] = t;
    }
  Serial.printf("[설정] %d곳이 보인다\n", portalFoundN);
}

// ── 웹 ────────────────────────────────────────────────────────────
static String portalEsc(const char *s) {  // HTML 에 그대로 끼우기 전에 막는다
  String o;
  for (const char *p = s; *p; p++) {
    switch (*p) {
      case '&': o += F("&amp;"); break;
      case '<': o += F("&lt;"); break;
      case '>': o += F("&gt;"); break;
      case '"': o += F("&quot;"); break;
      case '\'': o += F("&#39;"); break;
      default: o += *p;
    }
  }
  return o;
}

static const char PORTAL_CSS[] PROGMEM =
    "body{font-family:-apple-system,'Apple SD Gothic Neo',sans-serif;margin:0;padding:20px;"
    "background:#111;color:#eee;line-height:1.4}h1{font-size:19px;margin:0 0 6px}"
    "p{color:#9a9a9a;font-size:14px;margin:6px 0}label{display:block;margin:16px 0 5px;"
    "font-size:14px;color:#ccc}"
    "select,input{width:100%;box-sizing:border-box;font-size:17px;padding:11px;border-radius:9px;"
    "border:1px solid #444;background:#1c1c1c;color:#fff}"
    "button{width:100%;font-size:17px;font-weight:600;padding:14px;margin-top:20px;border:0;"
    "border-radius:9px;background:#f0b000;color:#111}a{color:#f0b000}"
    ".ok{color:#7ddc7d}.bad{color:#ff8a7a}";

static void portalSend(const String &body, const char *refresh = nullptr) {
  String h = F("<!doctype html><html lang=\"ko\"><head><meta charset=\"utf-8\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  if (refresh) {
    h += F("<meta http-equiv=\"refresh\" content=\"");
    h += refresh;
    h += F("\">");
  }
  h += F("<title>와이파이 설정</title><style>");
  h += FPSTR(PORTAL_CSS);
  h += F("</style></head><body>");
  h += body;
  h += F("</body></html>");
  portalWeb.send(200, "text/html; charset=utf-8", h);
}

static void portalPageRoot() {
  // 어느 주소로 왔나 — 폰이 캡티브 확인(captive.apple.com 등)을 하러 왔는지 보인다
  portalReqN++;
  Serial.printf("[설정] 웹 요청 #%lu %s %s%s\n", (unsigned long)portalReqN,
                portalWeb.client().remoteIP().toString().c_str(), portalWeb.hostHeader().c_str(),
                portalWeb.uri().c_str());
  String b = F("<h1>펀펀포인트 리더 — 와이파이</h1>");

  if (portalScanning) {
    b += F("<p>주변 와이파이를 둘러보는 중…</p>");
    portalSend(b, "3");
    return;
  }

  b += F("<p>이 리더가 붙을 와이파이를 골라 주세요.</p>"
         "<form method=\"post\" action=\"/save\"><label>와이파이</label><select name=\"ssid\">");
  for (int i = 0; i < portalFoundN; i++) {
    char tail[48];
    snprintf(tail, sizeof(tail), " (%s%s)",
             portalFound[i].rssi > -60 ? "센 신호" : portalFound[i].rssi > -75 ? "보통" : "약함",
             portalFound[i].lock ? ", 비밀번호 필요" : ", 열림");
    b += F("<option value=\"");
    b += portalEsc(portalFound[i].ssid);
    b += F("\">");
    b += portalEsc(portalFound[i].ssid);
    b += tail;
    b += F("</option>");
  }
  if (!portalFoundN) b += F("<option value=\"\">보이는 곳이 없습니다</option>");
  b += F("</select>"
         "<label>목록에 없으면 이름을 직접 (이쪽을 적으면 위 선택은 무시합니다)</label>"
         "<input name=\"manual\" autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\">"
         "<label>비밀번호</label>"
         "<input name=\"pass\" autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\">"
         "<button type=\"submit\">저장하고 붙어 보기</button></form>"
         "<p><a href=\"/rescan\">다시 찾기</a></p>"
         "<p>휴대폰 핫스팟도 됩니다. 5GHz 전용 와이파이는 이 기기가 보지 못해 목록에 "
         "나오지 않습니다(2.4GHz 만 됩니다).</p>");
  portalSend(b);
}

// 끊긴 까닭을 사람 말로. 손볼 자리가 저마다 달라 "못 붙었다" 로는 알 수 없다.
static void portalWhyFrom(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE:
      snprintf(portalWhy, sizeof(portalWhy), "비밀번호가 맞지 않는 것 같습니다.");
      break;
    case WIFI_REASON_NO_AP_FOUND:
      snprintf(portalWhy, sizeof(portalWhy),
               "그 이름을 찾지 못했습니다. 이름을 다시 보시고, 5GHz 전용이 아닌지도 보세요.");
      break;
    case WIFI_REASON_ASSOC_TOOMANY:
      snprintf(portalWhy, sizeof(portalWhy),
               "공유기가 더 받지 않습니다(붙은 기기가 너무 많습니다).");
      break;
    case 0:
      snprintf(portalWhy, sizeof(portalWhy),
               "기다렸는데 아무 답이 없었습니다. 신호가 약한 자리일 수 있습니다.");
      break;
    default:
      snprintf(portalWhy, sizeof(portalWhy), "못 붙었습니다 (%s).",
               WiFi.disconnectReasonName((wifi_err_reason_t)reason));
      break;
  }
}

static void portalPageStatus() {
  String b;
  switch (portalTry) {
    case PT_RUN:
      b = F("<h1>붙어 보는 중…</h1><p>");
      b += portalEsc(portalSsid);
      b += F(" 에 붙어 보고 있습니다. 20초쯤 걸릴 수 있습니다.</p>");
      portalSend(b, "2");
      return;
    case PT_OK:
      b = F("<h1 class=\"ok\">붙었습니다</h1><p>");
      b += portalEsc(portalSsid);
      b += F(" 를 저장했습니다. 이 기기가 곧 다시 시작합니다 — 폰은 원래 와이파이로 돌려 주세요.</p>");
      portalSend(b);
      return;
    case PT_FAIL:
      b = F("<h1 class=\"bad\">못 붙었습니다</h1><p>");
      b += portalEsc(portalWhy);
      b += F("</p><p>저장하지 않았습니다 — 맞는 것만 저장합니다.</p>"
             "<p><a href=\"/\">다시 해 보기</a></p>");
      portalSend(b);
      return;
    default:
      portalWeb.sendHeader("Location", "/");
      portalWeb.send(302, "text/plain", "");
      return;
  }
}

static void portalPageSave() {
  String ssid = portalWeb.arg("manual");
  ssid.trim();
  if (!ssid.length()) ssid = portalWeb.arg("ssid");
  const String pass = portalWeb.arg("pass");

  if (!ssid.length()) {
    portalSend(F("<h1 class=\"bad\">이름이 비어 있습니다</h1><p><a href=\"/\">돌아가기</a></p>"));
    return;
  }

  snprintf(portalSsid, sizeof(portalSsid), "%s", ssid.c_str());
  snprintf(portalPass, sizeof(portalPass), "%s", pass.c_str());
  portalWhy[0] = '\0';
  portalTry = PT_RUN;
  portalTryAt = 0;  // portalService 가 실제로 걸고 시각을 적는다
  portalWeb.sendHeader("Location", "/status");
  portalWeb.send(302, "text/plain", "");
}

static bool portalWebBegun = false;
static void portalBegin() {
  if (!portalWebBegun) {  // 길은 한 번만 깐다 — 다시 들어올 때 같은 길이 두 벌 쌓이지 않게
    portalWeb.on("/", HTTP_GET, portalPageRoot);
    portalWeb.on("/save", HTTP_POST, portalPageSave);
    portalWeb.on("/status", HTTP_GET, portalPageStatus);
    portalWeb.on("/rescan", HTTP_GET, []() {
      portalScanWant = true;
      portalWeb.sendHeader("Location", "/");
      portalWeb.send(302, "text/plain", "");
    });
    // 어느 주소로 들어와도 설정 페이지를 준다. 아이폰은 captive.apple.com 을, 안드로이드는
    // connectivitycheck 를 찔러 보고 **제가 바라던 답이 아니면** 로그인 창을 띄운다 —
    // 그래서 200 으로 이 페이지를 돌려주는 것이 곧 '설정 창 자동으로 띄우기' 다.
    portalWeb.onNotFound(portalPageRoot);
    portalWebBegun = true;
  }
  portalWeb.begin();
}

// ── 들어가기 · 나오기 ──────────────────────────────────────────────
static void portalStart(const char *why) {
  if (portalOn) return;
  Serial.printf("[설정] 설정 모드로 들어간다 — %s\n", why);
  ledBase(LED_BOOT);  // 노랑 — '하는 중'

  // 먼저 둘러본다(아직 STA 뿐일 때). AP 를 띄운 뒤에 둘러보면 채널을 옮겨 다니느라
  // 붙어 있는 폰이 끊길 수 있어 **첫 목록만은 조용할 때** 받는다.
  drawStatus("주변 와이파이를 둘러봅니다");
  WiFi.scanDelete();
  WiFi.disconnect();
  delay(300);
  WiFi.scanNetworks(true);
  portalScanning = true;
  portalScanAt = millis();
  while (portalScanning) {
    portalScanReap();
    ledLoop();
    delay(50);
  }

  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(portalAp, sizeof(portalAp), "%s-%02x%02x", PORTAL_AP_PREFIX, mac[4], mac[5]);

  WiFi.mode(WIFI_AP_STA);  // AP 를 띄우면서 STA 도 살려 둔다(받아 적은 것으로 붙어 봐야 한다)
  const char *pass = PORTAL_AP_PASS[0] ? PORTAL_AP_PASS : nullptr;
  if (!WiFi.softAP(portalAp, pass, PORTAL_CHANNEL)) {
    Serial.println("[설정] AP 를 띄우지 못했습니다 — 설정 모드를 접는다");
    WiFi.mode(WIFI_STA);
    goIdle(false);
    return;
  }
#if PORTAL_TX_POWER
  // 송신 출력을 낮춘다(config.h 의 PORTAL_TX_POWER 머리말). 폰이 이름은 보는데 "네트워크에
  // 연결할 수 없음" 으로 떨어지던 것 — 붙는 동안 오가는 프레임이 최대 출력에서 깨진다.
  WiFi.setTxPower((wifi_power_t)PORTAL_TX_POWER);
#endif
  {
    // 실제로 걸린 AP 설정을 찍는다 — 폰이 못 붙을 때 보안 방식·채널·출력부터 본다
    wifi_config_t ac = {};
    esp_wifi_get_config(WIFI_IF_AP, &ac);
    int8_t q = 0;
    esp_wifi_get_max_tx_power(&q);  // 0.25dBm 단위
    Serial.printf("[설정] AP 보안 %d(3=WPA2) · PMF 필수 %d · 채널 %d · 최대 %d명 · 출력 %.1fdBm\n",
                  (int)ac.ap.authmode, (int)ac.ap.pmf_cfg.required, (int)ac.ap.channel,
                  (int)ac.ap.max_connection, q / 4.0f);
  }
  portalDns.setErrorReplyCode(DNSReplyCode::NoError);
  portalDns.start(53, "*", WiFi.softAPIP());  // 어떤 이름을 물어도 우리를 가리킨다
  portalBegin();

  if (!portalApEvtId)
    portalApEvtId = WiFi.onEvent([](WiFiEvent_t ev, WiFiEventInfo_t info) {
      PortalEvt e = {};
      if (ev == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        e.kind = 1;
        memcpy(e.mac, info.wifi_ap_staconnected.mac, 6);
      } else if (ev == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        e.kind = 2;
        memcpy(e.mac, info.wifi_ap_stadisconnected.mac, 6);
        e.reason = info.wifi_ap_stadisconnected.reason;
      } else if (ev == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
        e.kind = 3;
        memcpy(e.mac, info.wifi_ap_staipassigned.mac, 6);
        e.ip = info.wifi_ap_staipassigned.ip.addr;
      } else {
        return;
      }
      portalEvtPush(e);
    });

  if (!portalEvtId)
    portalEvtId = WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
          portalLastReason = info.wifi_sta_disconnected.reason;
          portalStaDown = true;
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  portalOn = true;
  portalTry = PT_IDLE;
  portalRebootAt = 0;
  portalQuietAt = millis();
  Serial.printf("[설정] AP '%s' (채널 %d) · http://%s/\n", portalAp, PORTAL_CHANNEL,
                WiFi.softAPIP().toString().c_str());
  portalDraw(nullptr);
}

static void portalStop() {
  if (!portalOn) return;
  Serial.println("[설정] 설정 모드를 걷는다 — 평소대로 돌아간다");
  portalWeb.stop();
  portalDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalOn = false;
  portalTry = PT_IDLE;

  // 설정하는 동안 리더가 읽은 묵은 태그는 버린다
  char uid[32];
  takePendingUid(uid, sizeof(uid));

  // 셈을 지우지 않으면 나오는 길에 또 들어온다. 첫 순위부터 곧바로 붙어 본다.
  wifiNoApRounds = 0;
  wifiIdx = 0;
  wifiTried = false;
  ledBase(LED_BOOT);
  goIdle(false);
}

// loop 에서 설정 모드일 때만 불린다 — 이때 wifiService 는 돌지 않는다(둘이 라디오를 다툰다).
static void portalService() {
  portalEvtPrint();
  portalDns.processNextRequest();
  portalWeb.handleClient();

  if (portalScanning)
    portalScanReap();
  else if (portalScanWant) {
    portalScanWant = false;
    portalScanBegin();
  }

  // 받아 적은 것으로 붙어 보기
  if (portalTry == PT_RUN) {
    if (!portalTryAt) {  // 아직 걸지 않았다 — 지금 건다
      portalDraw("붙어 보는 중");
      WiFi.disconnect();
      portalLastReason = 0;
      portalStaDown = false;
      WiFi.begin(portalSsid, portalPass);
      portalTryAt = millis();
    } else if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[설정] '%s' 에 붙었다 (%s) — 저장한다\n", portalSsid,
                    WiFi.localIP().toString().c_str());
      credsSave(portalSsid, portalPass);  // 맞는 것만 적는다
      portalTry = PT_OK;
      portalRebootAt = millis() + 3000;  // 성공 화면을 폰이 읽을 틈을 준다
      portalDraw("저장했습니다");
      ledBase(LED_OK);
    } else {
      const uint32_t el = millis() - portalTryAt;
      // '끊겼다' 는 조금 지난 뒤부터 믿는다 — 걸기 직전 disconnect() 의 신호가 곧바로
      // 날아와 멀쩡한 시도를 접게 만들 수 있다(master WP_CONNECT 머리말과 같은 까닭).
      if ((portalStaDown && el >= PORTAL_TRY_MIN_MS) || el >= PORTAL_TRY_MS) {
        portalWhyFrom(portalStaDown ? portalLastReason : 0);
        Serial.printf("[설정] '%s' 에 못 붙었다 — %s\n", portalSsid, portalWhy);
        WiFi.disconnect();
        portalTry = PT_FAIL;
        portalDraw("못 붙었습니다");
      }
    }
  }

  if (portalRebootAt && (int32_t)(millis() - portalRebootAt) >= 0) {
    Serial.println("[설정] 다시 시작합니다...");
    Serial.flush();
    ESP.restart();
  }

  // 저절로 닫기 — 폰이 붙어 있는 동안, 그리고 아는 와이파이가 하나도 없을 때는 세지 않는다
  // (없으면 닫아 봐야 붙을 곳이 없어 헛돌다가 다시 들어올 뿐이다).
  if (wifiApCount == 0 || WiFi.softAPgetStationNum() > 0)
    portalQuietAt = 0;
  else if (!portalQuietAt)
    portalQuietAt = millis();
  if (portalTry != PT_OK && portalQuietAt && millis() - portalQuietAt >= PORTAL_IDLE_MS) {
    Serial.printf("[설정] %lu분 동안 아무도 붙지 않았다\n",
                  (unsigned long)(PORTAL_IDLE_MS / 60000));
    portalStop();
  }
}

#else  // PORTAL_ENABLE 0 — 설정 모드를 아예 넣지 않는다
static bool portalActive() { return false; }
static void portalStart(const char *) {
  Serial.println("[설정] 설정 모드가 꺼져 있습니다(PORTAL_ENABLE 0)");
}
static void portalStop() {}
static void portalService() {}
#endif
