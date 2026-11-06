#pragma once
// funfunFirmware — 와이파이 목록 (폰으로 적어 넣은 네 벌 [+ 구울 때 넣은 네 벌])
//
// nfcProject/master 의 creds.h 를 옮겨 온 것이다(2026-11-06). 설정 모드(portal.h)가
// 폰에서 받아 적은 것을 플래시(NVS)에 넣어 두고, 부팅할 때 그것을 먼저 본다.
//
// 섞는 순서: NVS 에 적힌 것(최근에 넣은 것이 맨 앞) → WIFI_USE_BUILTIN 1 이면 그다음
// ChurchSecrets.h 의 네 벌. 지금 기본값은 0 이다 — **폰으로 넣은 것만** 안다. 그래서
// 처음 켜면(아는 곳이 하나도 없으면) 곧바로 설정 모드가 열려 화면에 QR 이 뜬다.
//
// 빌드할 때만 넣는 와이파이(WIFI_SSID_OVERRIDE)가 있으면 **그 하나만** 쓴다 — 예전과
// 같은 뜻이다(책상에서 핫스팟 하나로 시험할 때). 그때는 NVS 도 보지 않는다.
//
// 붙는 차례는 wifiService(main.cpp)가 정한다 — 목록의 0 번부터 한 곳씩 붙어 본다.
// 그래서 폰으로 마지막에 넣은 것이 늘 먼저 쓰인다.

#include <Preferences.h>

#ifndef WIFI_NVS_NS
#define WIFI_NVS_NS "ffwifi"      // NVS 칸 이름(와이파이만 쓴다) — config.h 에서 정한다
#endif
#ifndef WIFI_USE_BUILTIN
#define WIFI_USE_BUILTIN 0
#endif
#define WIFI_SAVED_MAX 4          // 폰으로 적어 넣은 것을 이만큼 들고 있는다

// SSID 는 32바이트까지, WPA2 비밀번호는 63자까지다(+ 끝 글자 한 자리).
struct WifiAp {
  char ssid[33];
  char pass[65];
};

static WifiAp wifiAps[WIFI_SAVED_MAX + 4];
static int wifiApCount = 0;
static int wifiSavedCount = 0;  // 그중 앞쪽 몇 벌이 NVS 에서 온 것인가(시리얼 표시용)

// 목록 끝에 한 벌 더한다. 빈 이름과 이미 있는 이름은 넣지 않는다 — 같은 이름이 두 칸을
// 차지하면 폰으로 고친 비밀번호가 구울 때 넣은 옛 비밀번호에 가려질 수 있다(먼저 더한 쪽이 이긴다).
static void wifiApAdd(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return;
  if (wifiApCount >= (int)(sizeof(wifiAps) / sizeof(wifiAps[0]))) return;
  for (int i = 0; i < wifiApCount; i++)
    if (!strcmp(wifiAps[i].ssid, ssid)) return;
  WifiAp *a = &wifiAps[wifiApCount++];
  snprintf(a->ssid, sizeof(a->ssid), "%s", ssid);
  snprintf(a->pass, sizeof(a->pass), "%s", pass ? pass : "");
}

// 부팅할 때 한 번.
static void credsLoad() {
  wifiApCount = wifiSavedCount = 0;

#if defined(WIFI_SSID_OVERRIDE) && defined(WIFI_PASS_OVERRIDE)
  wifiApAdd(WIFI_SSID_OVERRIDE, WIFI_PASS_OVERRIDE);
  Serial.printf("[와이파이] 빌드할 때 넣은 '%s' 하나만 쓴다\n", wifiAps[0].ssid);
  return;
#endif

  Preferences nv;
  if (nv.begin(WIFI_NVS_NS, true)) {  // 읽기 전용 — 칸이 없으면 false(처음 켠 기기)
    const int n = nv.getInt("n", 0);
    for (int i = 0; i < n && i < WIFI_SAVED_MAX; i++) {
      char ks[4], kp[4];
      snprintf(ks, sizeof(ks), "s%d", i);
      snprintf(kp, sizeof(kp), "p%d", i);
      char ssid[33] = "", pass[65] = "";
      nv.getString(ks, ssid, sizeof(ssid));
      nv.getString(kp, pass, sizeof(pass));
      const int before = wifiApCount;
      wifiApAdd(ssid, pass);
      if (wifiApCount > before) wifiSavedCount++;
    }
    nv.end();
  }

#if WIFI_USE_BUILTIN
  wifiApAdd(WIFI_SSID_0, WIFI_PASS_0);
  wifiApAdd(WIFI_SSID_1, WIFI_PASS_1);
  wifiApAdd(WIFI_SSID_2, WIFI_PASS_2);
  wifiApAdd(WIFI_SSID_3, WIFI_PASS_3);
#endif

  Serial.printf("[와이파이] 아는 곳 %d곳 (폰으로 넣은 것 %d · 구울 때 넣은 것 %d)\n", wifiApCount,
                wifiSavedCount, wifiApCount - wifiSavedCount);
  for (int i = 0; i < wifiApCount; i++)
    Serial.printf("           %d순위 %s%s\n", i + 1, wifiAps[i].ssid,
                  i < wifiSavedCount ? " (폰)" : "");
}

// 폰에서 받아 적은 한 벌을 **맨 앞**에 넣는다(= 다음 부팅부터 1순위).
// 같은 이름이 이미 있으면 그 칸을 지우고 새것만 남긴다 — 비밀번호를 고친 경우다.
static bool credsSave(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return false;

  WifiAp old[WIFI_SAVED_MAX];
  int oldN = 0;
  Preferences nv;
  if (nv.begin(WIFI_NVS_NS, true)) {
    const int n = nv.getInt("n", 0);
    for (int i = 0; i < n && i < WIFI_SAVED_MAX; i++) {
      char ks[4], kp[4];
      snprintf(ks, sizeof(ks), "s%d", i);
      snprintf(kp, sizeof(kp), "p%d", i);
      old[oldN].ssid[0] = old[oldN].pass[0] = '\0';
      nv.getString(ks, old[oldN].ssid, sizeof(old[oldN].ssid));
      nv.getString(kp, old[oldN].pass, sizeof(old[oldN].pass));
      if (old[oldN].ssid[0]) oldN++;
    }
    nv.end();
  }

  if (!nv.begin(WIFI_NVS_NS, false)) {
    Serial.println("[설정] NVS 를 열지 못했습니다 — 저장하지 못했다");
    return false;
  }
  nv.putString("s0", ssid);
  nv.putString("p0", pass ? pass : "");
  int k = 1;
  for (int i = 0; i < oldN && k < WIFI_SAVED_MAX; i++) {
    if (!strcmp(old[i].ssid, ssid)) continue;  // 새것으로 갈음했다
    char ks[4], kp[4];
    snprintf(ks, sizeof(ks), "s%d", k);
    snprintf(kp, sizeof(kp), "p%d", k);
    nv.putString(ks, old[i].ssid);
    nv.putString(kp, old[i].pass);
    k++;
  }
  nv.putInt("n", k);
  nv.end();
  Serial.printf("[설정] '%s' 를 저장했다 (폰으로 넣은 것 %d벌)\n", ssid, k);
  return true;
}

// 폰으로 넣은 것을 모두 잊는다(시리얼 'W'). 다시 켜면 QR 이 뜬다.
static bool credsClear() {
  Preferences nv;
  if (!nv.begin(WIFI_NVS_NS, false)) return false;
  nv.clear();
  nv.end();
  Serial.println("[설정] 폰으로 넣은 와이파이를 모두 지웠다 — 다시 켜면 설정 모드가 열린다");
  return true;
}
