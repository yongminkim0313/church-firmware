#pragma once
// singleTest — 와이파이 목록 (폰으로 적어 넣은 네 벌 + 구울 때 넣은 한 벌)
// nfcProject/master 의 creds.h 를 옮겨 왔다. 다른 점은 구울 때 넣은 것이 ChurchSecrets.h 의
// 네 벌이 아니라 main.cpp 의 WIFI_SSID 한 벌이라는 것뿐이다.
//
// 섞는 순서: NVS 에 적힌 것(최근에 넣은 것이 맨 앞) → 그다음 WIFI_SSID.
// 붙을 때는 **눈에 보이는 것 중 목록에서 가장 앞선 것**을 고른다(main.cpp 의 wifiJoin).
// 그래서 폰으로 넣은 것이 늘 먼저 쓰이고, 구울 때 넣은 한 벌은 지워지지 않는다.
//
// ★ NVS 에 **쓰는** 순간에는 플래시 캐시가 잠깐 꺼져 화면이 한 줄 밀릴 수 있다.
//   쓰는 것은 설정 모드에서 '저장' 을 누를 때 한 번뿐이고 그 뒤에 보드를 다시 세운다.

#include <Preferences.h>

#define WIFI_NVS_NS      "nfcwifi"   // NVS 칸 이름(와이파이만 쓴다)
#define WIFI_SAVED_MAX   4           // 폰으로 적어 넣은 것을 이만큼 들고 있는다

// SSID 는 32바이트까지, WPA2 비밀번호는 63자까지다(+ 끝 글자 한 자리).
struct WifiAp { char ssid[33]; char pass[65]; };

static WifiAp wifiAps[WIFI_SAVED_MAX + 1];
static int    wifiApCount    = 0;
static int    wifiSavedCount = 0;    // 그중 앞쪽 몇 벌이 NVS 에서 온 것인가(시리얼 표시용)

// 목록 끝에 한 벌 더한다. 빈 이름과 이미 있는 이름은 넣지 않는다(먼저 더한 쪽이 이긴다).
static void wifiApAdd(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return;
  if (wifiApCount >= (int)(sizeof(wifiAps) / sizeof(wifiAps[0]))) return;
  for (int i = 0; i < wifiApCount; i++)
    if (!strcmp(wifiAps[i].ssid, ssid)) return;
  WifiAp *a = &wifiAps[wifiApCount++];
  snprintf(a->ssid, sizeof(a->ssid), "%s", ssid);
  snprintf(a->pass, sizeof(a->pass), "%s", pass ? pass : "");
}

// 켤 때 한 번. NVS → 구울 때 넣은 것 순서로 목록을 짠다.
static void credsLoad() {
  wifiApCount = wifiSavedCount = 0;

  Preferences nv;
  if (nv.begin(WIFI_NVS_NS, true)) {            // true = 읽기만(아직 아무것도 안 적혔으면 실패한다)
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

  wifiApAdd(WIFI_SSID, WIFI_PASS);

  Serial.printf("[와이파이] 아는 곳 %d곳 (폰으로 넣은 것 %d · 구울 때 넣은 것 %d)\n",
                wifiApCount, wifiSavedCount, wifiApCount - wifiSavedCount);
  for (int i = 0; i < wifiApCount; i++)
    Serial.printf("           %d순위 %s%s\n", i + 1, wifiAps[i].ssid, i < wifiSavedCount ? " (폰)" : "");
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
    if (!strcmp(old[i].ssid, ssid)) continue;      // 새것으로 갈음했다
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

// 폰으로 넣은 것을 모두 잊는다(시리얼 'W'). 구울 때 넣은 한 벌은 그대로 남는다.
static bool credsClear() {
  Preferences nv;
  if (!nv.begin(WIFI_NVS_NS, false)) return false;
  nv.clear();
  nv.end();
  Serial.println("[설정] 폰으로 넣은 와이파이를 모두 지웠다");
  return true;
}
