#pragma once
// nfcProject — 와이파이 목록 (구울 때 넣은 네 벌 + 폰으로 적어 넣은 네 벌)
//
// 여태 와이파이는 ChurchSecrets.h 에 박아 둔 네 벌뿐이었다 — 새 교회로 기기를 가져가면
// 노트북을 들고 가 다시 구워야 했다. 그래서 **설정 모드**(portal.h)가 폰에서 받아 적은
// 것을 플래시(NVS)에 넣어 두고, 부팅할 때 그것을 먼저 보게 한다.
//
// 섞는 순서: NVS 에 적힌 것(최근에 넣은 것이 맨 앞) → 그다음 ChurchSecrets.h 의 네 벌.
// 고르는 규칙은 그대로다 — wifiService 가 **눈에 보이는 것 중 목록에서 가장 앞선 것**을
// 하나 골라 붙는다(main.cpp 의 WP_SCAN). 그래서 폰으로 넣은 것이 늘 먼저 쓰인다.
// 구울 때 넣은 네 벌은 지워지지 않는다 — 폰으로 넣은 것이 다 틀려도 집(교회) 망에는
// 돌아올 수 있어야 하니까.
//
// ★ NVS 에 **쓰는** 순간에는 플래시 캐시가 잠깐 꺼져 화면이 한 줄 밀릴 수 있다
//   (main.cpp 머리말의 WiFi.persistent(false) 와 똑같은 까닭). 쓰는 것은 설정 모드에서
//   '저장' 을 누를 때 한 번뿐이고 그 뒤에 어차피 보드를 다시 세우므로 눈에 남지 않는다.
//   읽기(부팅 때)는 캐시를 끄지 않아 괜찮다.

#include <Preferences.h>

#define WIFI_NVS_NS      "nfcwifi"   // NVS 칸 이름(와이파이만 쓴다)
#define WIFI_SAVED_MAX   4           // 폰으로 적어 넣은 것을 이만큼 들고 있는다

// SSID 는 32바이트까지, WPA2 비밀번호는 63자까지다(+ 끝 글자 한 자리).
struct WifiAp { char ssid[33]; char pass[65]; };

static WifiAp wifiAps[WIFI_SAVED_MAX + 4];
static int    wifiApCount    = 0;
static int    wifiSavedCount = 0;    // 그중 앞쪽 몇 벌이 NVS 에서 온 것인가(시리얼 표시용)

// 목록 끝에 한 벌 더한다. 빈 이름과 이미 있는 이름은 넣지 않는다 —
// 같은 이름이 두 칸을 차지하면 뒤 칸은 영영 쓰이지 않고, 폰으로 고친 비밀번호가
// 구울 때 넣은 옛 비밀번호에 가려지는 일만 생긴다(먼저 더한 쪽이 이긴다).
static void wifiApAdd(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return;
  if (wifiApCount >= (int)(sizeof(wifiAps) / sizeof(wifiAps[0]))) return;
  for (int i = 0; i < wifiApCount; i++)
    if (!strcmp(wifiAps[i].ssid, ssid)) return;
  WifiAp *a = &wifiAps[wifiApCount++];
  snprintf(a->ssid, sizeof(a->ssid), "%s", ssid);
  snprintf(a->pass, sizeof(a->pass), "%s", pass ? pass : "");
}

// 부팅할 때 한 번. NVS → 컴파일값 순서로 목록을 짠다.
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

  wifiApAdd(WIFI_SSID_0, WIFI_PASS_0);
  wifiApAdd(WIFI_SSID_1, WIFI_PASS_1);
  wifiApAdd(WIFI_SSID_2, WIFI_PASS_2);
  wifiApAdd(WIFI_SSID_3, WIFI_PASS_3);

  Serial.printf("[와이파이] 아는 곳 %d곳 (폰으로 넣은 것 %d · 구울 때 넣은 것 %d)\n",
                wifiApCount, wifiSavedCount, wifiApCount - wifiSavedCount);
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

// 폰으로 넣은 것을 모두 잊는다(시리얼 'W'). 구울 때 넣은 네 벌은 그대로 남는다.
static bool credsClear() {
  Preferences nv;
  if (!nv.begin(WIFI_NVS_NS, false)) return false;
  nv.clear();
  nv.end();
  Serial.println("[설정] 폰으로 넣은 와이파이를 모두 지웠다");
  return true;
}
