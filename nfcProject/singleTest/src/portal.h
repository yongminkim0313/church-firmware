#pragma once
// singleTest — 와이파이 설정 모드 (폰으로 와이파이를 적어 넣는다)
// nfcProject/master 의 portal.h 를 옮겨 왔다. 웹 화면·저장 규칙은 그대로이고, 다른 것은 둘이다.
//   · 이 보드는 와이파이를 **일꾼(fetchTask, 0번 심)** 이 쥐고 있어서 설정 모드도 일꾼이 돌린다
//     (portalRun 이 그 자리에서 돌다가 저장하면 보드를 다시 세운다). 화면은 loop 가 그린다 —
//     일꾼은 portalDrawWant 를 세워 '다시 그려라' 만 알린다(화면은 loop 만 만진다는 규칙).
//   · 터치가 없는 화면이라 '켤 때 화면 누르기' 대신 **BOOT 단추**다(아래).
//
// 자원봉사자가 할 일:
//   1) 화면에 뜬 QR 을 폰 카메라로 찍는다 → "이 네트워크에 연결"
//   2) 붙으면 설정 페이지가 저절로 뜬다(안 뜨면 브라우저에 192.168.4.1 — 화면에도 적혀 있다)
//   3) 목록에서 와이파이를 고르고 비밀번호를 넣고 '저장'
//   4) 보드가 그 자리에서 붙어 본다. 붙으면 플래시에 적고 다시 시작한다.
//      못 붙으면 까닭(비밀번호 틀림 / 이름이 안 보임 / 신호 약함)을 그 화면에 보여 준다.
// ★ 비밀번호가 맞는 것을 **확인한 뒤에만** 저장한다(틀린 것이 1순위로 앉지 않게).
//
// ── 들어가는 길 세 가지 ───────────────────────────────────────────
//   · 켜고 PORTAL_BOOT_WINDOW_MS 안에 BOOT 단추를 PORTAL_BOOT_HOLD_MS 누르고 있기.
//     ★ 켜기 **전부터** 누르고 있으면 안 된다 — BOOT(GPIO0)를 누른 채 켜면 칩이 굽기 모드로
//       들어가 펌웨어가 아예 돌지 않는다. 화면이 켜진 뒤에 누른다.
//   · 아는 곳이 하나도 안 보일 때 저절로 — 켠 뒤 **한 번도 못 붙었고** PORTAL_AUTO_ROUNDS
//     바퀴를 둘러봐도 아는 이름이 없으면. 한 번이라도 붙었던 뒤에는 저절로 들어가지 않는다.
//   · 시리얼 'w' (설정 모드에서 'w' 를 다시 누르면 나간다 · 'W' 는 폰으로 넣은 것 지우기)
//
// ── 알아 둘 것 ────────────────────────────────────────────────────
//   · 설정 모드에서는 서버에 붙지 않는다 — 알림·순위가 멎는다.
//   · 폰이 붙어 있지 않은 채로 PORTAL_IDLE_MS 가 지나면 걷는다. 걷을 때는 **보드를 다시
//     세운다** — 와이파이·서버 연결을 처음부터 다시 맺는 것이 가장 확실하다.
//   · 이 페이지에 기기 키(TALENT_DEVICE_KEY)는 띄우지 않는다. 와이파이만 다룬다.

#include <WebServer.h>
#include <DNSServer.h>

#define PORTAL_AP_PREFIX      "nfc-setup"   // 띄우는 AP 이름 앞머리(뒤에 MAC 네 자리가 붙는다)
#define PORTAL_AP_PASS        "funfun1234"  // 8자 이상(WPA2). 빈 값이면 열린 AP
#define PORTAL_CHANNEL        1
#define PORTAL_IDLE_MS        300000UL      // 폰이 하나도 안 붙은 채로 이만큼 지나면 걷는다(5분)
#define PORTAL_AUTO_ROUNDS    3             // 아는 곳이 안 보이는 바퀴가 이만큼 이어지면 저절로
#define PORTAL_BOOT_HOLD_MS   1000
#define PORTAL_BOOT_WINDOW_MS 10000
#define PIN_BOOT              0             // BOOT 단추(누르면 LOW)

static WebServer portalWeb(80);
static DNSServer portalDns;

static volatile bool portalOn = false;      // loop 는 이것을 보고 평소 화면을 멈춘다
static volatile bool portalDrawWant = false;
static char     portalAp[33]     = "";      // 띄운 AP 이름
static char     portalStatus[64] = "";      // 화면 맨 아래 한 줄(빈 값이면 192.168.4.1)
static uint32_t portalQuietAt  = 0;         // 폰이 하나도 안 붙어 있게 된 시각(0 = 붙어 있다)
static uint32_t portalRebootAt = 0;

#define PORTAL_SCAN_MAX 24
static struct { char ssid[33]; int8_t rssi; bool lock; } portalFound[PORTAL_SCAN_MAX];
static int      portalFoundN   = 0;
static bool     portalScanning = false;
static bool     portalScanWant = false;
static uint32_t portalScanAt   = 0;

enum PortalTry : uint8_t { PT_IDLE, PT_RUN, PT_OK, PT_FAIL };
static PortalTry portalTry   = PT_IDLE;
static uint32_t  portalTryAt = 0;
static char      portalSsid[33] = "";
static char      portalPass[65] = "";
static char      portalWhy[128] = "";

static const uint32_t PORTAL_TRY_MS      = 20000;
static const uint32_t PORTAL_TRY_MIN_MS  = 3000;   // 결판 신호는 이만큼 지난 뒤부터 믿는다
static const uint32_t PORTAL_SCAN_MAX_MS = 12000;

// 와이파이가 왜 끊겼나 — WiFi.onEvent 가 적는다(main.cpp). 설정 화면이 사람 말로 바꾼다.
static volatile uint8_t wifiLastReason = 0;
static volatile bool    wifiStaBusy    = false;   // 붙어 보는 중(끊김 신호가 오면 내린다)

static void portalSetStatus(const char *s) {
  snprintf(portalStatus, sizeof(portalStatus), "%s", s ? s : "");
  portalDrawWant = true;
}

// ── 접속용 QR 글자 ─────────────────────────────────────────────────
// 'WIFI:T:WPA;S:<이름>;P:<비밀번호>;;' 는 아이폰·안드로이드 카메라가 알아보는 규약이다.
// \ ; , : " 는 앞에 \ 를 붙여 막는다.
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
  if (p[0]) snprintf(out, cap, "WIFI:T:WPA;S:%s;P:%s;;", s, p);
  else      snprintf(out, cap, "WIFI:T:nopass;S:%s;;", s);
}

// ── 둘러보기 ───────────────────────────────────────────────────────
static void portalScanBegin() {
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) { portalScanning = false; return; }
  portalScanning = true;
  portalScanAt   = millis();
}

// 결과를 거둔다. 같은 이름이 여러 번 보이면(공유기 두 대) 센 것 하나만 남긴다.
static void portalScanReap() {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - portalScanAt >= PORTAL_SCAN_MAX_MS) { WiFi.scanDelete(); portalScanning = false; }
    return;
  }
  portalScanning = false;
  if (n < 0) { WiFi.scanDelete(); return; }

  portalFoundN = 0;
  for (int16_t j = 0; j < n; j++) {
    const String ss = WiFi.SSID(j);
    if (!ss.length()) continue;                              // 이름을 숨긴 곳 — 직접 입력으로
    int at = -1;
    for (int i = 0; i < portalFoundN; i++)
      if (ss == portalFound[i].ssid) { at = i; break; }
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

  for (int i = 1; i < portalFoundN; i++)                     // 센 것부터
    for (int j = i; j > 0 && portalFound[j].rssi > portalFound[j - 1].rssi; j--) {
      auto t = portalFound[j]; portalFound[j] = portalFound[j - 1]; portalFound[j - 1] = t;
    }
  Serial.printf("[설정] %d곳이 보인다\n", portalFoundN);
}

// ── 웹 ────────────────────────────────────────────────────────────
static String portalEsc(const char *s) {          // HTML 에 그대로 끼우기 전에 막는다
  String o;
  for (const char *p = s; *p; p++) {
    switch (*p) {
      case '&': o += F("&amp;");  break;
      case '<': o += F("&lt;");   break;
      case '>': o += F("&gt;");   break;
      case '"': o += F("&quot;"); break;
      case '\'': o += F("&#39;"); break;
      default:  o += *p;
    }
  }
  return o;
}

static const char PORTAL_CSS[] PROGMEM =
  "body{font-family:-apple-system,'Apple SD Gothic Neo',sans-serif;margin:0;padding:20px;"
  "background:#111;color:#eee;line-height:1.4}h1{font-size:19px;margin:0 0 6px}"
  "p{color:#9a9a9a;font-size:14px;margin:6px 0}label{display:block;margin:16px 0 5px;font-size:14px;color:#ccc}"
  "select,input{width:100%;box-sizing:border-box;font-size:17px;padding:11px;border-radius:9px;"
  "border:1px solid #444;background:#1c1c1c;color:#fff}"
  "button{width:100%;font-size:17px;font-weight:600;padding:14px;margin-top:20px;border:0;"
  "border-radius:9px;background:#f0b000;color:#111}a{color:#f0b000}"
  ".ok{color:#7ddc7d}.bad{color:#ff8a7a}";

static void portalSend(const String &body, const char *refresh = nullptr) {
  String h = F("<!doctype html><html lang=\"ko\"><head><meta charset=\"utf-8\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  if (refresh) { h += F("<meta http-equiv=\"refresh\" content=\""); h += refresh; h += F("\">"); }
  h += F("<title>와이파이 설정</title><style>");
  h += FPSTR(PORTAL_CSS);
  h += F("</style></head><body>");
  h += body;
  h += F("</body></html>");
  portalWeb.send(200, "text/html; charset=utf-8", h);
}

static void portalPageRoot() {
  String b = F("<h1>펀펀포인트 화면 — 와이파이</h1>");
  if (portalScanning) {
    b += F("<p>주변 와이파이를 둘러보는 중…</p>");
    portalSend(b, "3");
    return;
  }
  b += F("<p>이 화면이 붙을 와이파이를 골라 주세요.</p>"
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
         "<p>휴대폰 핫스팟도 됩니다(아이폰은 '호환성 최대화' 를 켜 주세요). 5GHz 전용 와이파이는 "
         "이 기기가 보지 못해 목록에 나오지 않습니다(2.4GHz 만 됩니다).</p>");
  portalSend(b);
}

// 끊긴 까닭을 사람 말로. 손볼 자리가 저마다 달라 "못 붙었다" 로는 알 수 없다.
static void portalWhyFrom(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE:
      snprintf(portalWhy, sizeof(portalWhy), "비밀번호가 맞지 않는 것 같습니다."); break;
    case WIFI_REASON_NO_AP_FOUND:
      snprintf(portalWhy, sizeof(portalWhy),
               "그 이름을 찾지 못했습니다. 이름을 다시 보시고, 5GHz 전용이 아닌지도 보세요."); break;
    case WIFI_REASON_ASSOC_TOOMANY:
      snprintf(portalWhy, sizeof(portalWhy), "공유기가 더 받지 않습니다(붙은 기기가 너무 많습니다)."); break;
    case 0:
      snprintf(portalWhy, sizeof(portalWhy), "기다렸는데 아무 답이 없었습니다. 신호가 약한 자리일 수 있습니다."); break;
    default:
      snprintf(portalWhy, sizeof(portalWhy), "못 붙었습니다 (%s).",
               WiFi.disconnectReasonName((wifi_err_reason_t)reason)); break;
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
  portalTry    = PT_RUN;
  portalTryAt  = 0;               // portalService 가 실제로 걸고 시각을 적는다
  portalWeb.sendHeader("Location", "/status");
  portalWeb.send(302, "text/plain", "");
}

static void portalBegin() {
  portalWeb.on("/",       HTTP_GET,  portalPageRoot);
  portalWeb.on("/save",   HTTP_POST, portalPageSave);
  portalWeb.on("/status", HTTP_GET,  portalPageStatus);
  portalWeb.on("/rescan", HTTP_GET,  []() {
    portalScanWant = true;
    portalWeb.sendHeader("Location", "/");
    portalWeb.send(302, "text/plain", "");
  });
  // 어느 주소로 들어와도 설정 페이지를 준다 — 아이폰·안드로이드가 찔러 보는 주소에 이 페이지를
  // 돌려주는 것이 곧 '설정 창 저절로 띄우기' 다.
  portalWeb.onNotFound(portalPageRoot);
  portalWeb.begin();
}

// ── 들어가기 ───────────────────────────────────────────────────────
// 일꾼 위에서 부른다. 돌아오지 않는다 — 저장하면 다시 세우고, 걷어도(아무도 안 붙었다 ·
// 시리얼 'w') 다시 세운다.
static void portalRun(const char *why) {
  Serial.printf("[설정] 설정 모드로 들어간다 — %s\n", why);
  portalOn = true;
  portalSetStatus("주변 와이파이를 둘러봅니다");

  // 먼저 둘러본다(아직 STA 뿐일 때) — AP 를 띄운 뒤에는 채널을 옮겨 다니느라 붙은 폰이 끊길 수 있다
  WiFi.scanDelete();
  WiFi.disconnect();
  vTaskDelay(pdMS_TO_TICKS(300));
  WiFi.scanNetworks(true);
  portalScanning = true;
  portalScanAt   = millis();
  while (portalScanning) { portalScanReap(); vTaskDelay(pdMS_TO_TICKS(50)); }

  uint8_t mac[6] = { 0 };
  WiFi.macAddress(mac);
  snprintf(portalAp, sizeof(portalAp), "%s-%02x%02x", PORTAL_AP_PREFIX, mac[4], mac[5]);

  WiFi.mode(WIFI_AP_STA);         // AP 를 띄우면서 STA 도 살려 둔다(받아 적은 것으로 붙어 봐야 한다)
  if (!WiFi.softAP(portalAp, PORTAL_AP_PASS[0] ? PORTAL_AP_PASS : nullptr, PORTAL_CHANNEL)) {
    Serial.println("[설정] AP 를 띄우지 못했습니다 — 다시 시작한다");
    ESP.restart();
  }
  portalDns.setErrorReplyCode(DNSReplyCode::NoError);
  portalDns.start(53, "*", WiFi.softAPIP());    // 어떤 이름을 물어도 우리를 가리킨다
  portalBegin();
  portalTry      = PT_IDLE;
  portalQuietAt  = millis();
  Serial.printf("[설정] AP '%s' (채널 %d) · 비밀번호 %s · http://%s/\n",
                portalAp, PORTAL_CHANNEL, PORTAL_AP_PASS, WiFi.softAPIP().toString().c_str());
  portalSetStatus("");

  for (;;) {
    portalDns.processNextRequest();
    portalWeb.handleClient();

    if (portalScanning)      portalScanReap();
    else if (portalScanWant) { portalScanWant = false; portalScanBegin(); }

    if (portalTry == PT_RUN) {
      if (!portalTryAt) {                               // 아직 걸지 않았다 — 지금 건다
        portalSetStatus("붙어 보는 중");
        WiFi.disconnect();
        wifiLastReason = 0;
        wifiStaBusy    = true;
        WiFi.begin(portalSsid, portalPass);
        portalTryAt = millis();
      } else if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[설정] '%s' 에 붙었다 (%s) — 저장한다\n", portalSsid, WiFi.localIP().toString().c_str());
        credsSave(portalSsid, portalPass);              // 맞는 것만 적는다
        portalTry      = PT_OK;
        portalRebootAt = millis() + 3000;               // 성공 화면을 폰이 읽을 틈을 준다
        portalSetStatus("저장했습니다 — 다시 시작합니다");
      } else {
        const uint32_t el = millis() - portalTryAt;
        // 결판 신호(끊김)는 조금 지난 뒤부터 믿는다 — 걸기 직전에 남아 있던 신호가 곧바로 와서
        // 멀쩡한 시도를 접게 만든 적이 있다(master).
        if ((!wifiStaBusy && el >= PORTAL_TRY_MIN_MS) || el >= PORTAL_TRY_MS) {
          portalWhyFrom(wifiLastReason);
          Serial.printf("[설정] '%s' 에 못 붙었다 — %s\n", portalSsid, portalWhy);
          WiFi.disconnect();
          portalTry = PT_FAIL;
          portalSetStatus("못 붙었습니다");
        }
      }
    }

    if (portalRebootAt && (int32_t)(millis() - portalRebootAt) >= 0) {
      Serial.println("[설정] 다시 시작합니다...");
      Serial.flush();
      ESP.restart();
    }

    // 시리얼 — 'w' 나가기 · 'W' 폰으로 넣은 것 지우기
    if (Serial.available()) {
      const int c = Serial.read();
      if (c == 'w') { Serial.println("[설정] 시리얼로 나간다 — 다시 시작한다"); Serial.flush(); ESP.restart(); }
      if (c == 'W') credsClear();
    }

    // 저절로 걷기 — 폰이 붙어 있는 동안에는 세지 않는다
    if (WiFi.softAPgetStationNum() > 0) portalQuietAt = 0;
    else if (!portalQuietAt)            portalQuietAt = millis();
    if (portalTry != PT_OK && portalQuietAt && millis() - portalQuietAt >= PORTAL_IDLE_MS) {
      Serial.printf("[설정] %lu분 동안 아무도 붙지 않았다 — 다시 시작한다\n", (unsigned long)(PORTAL_IDLE_MS / 60000));
      Serial.flush();
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
