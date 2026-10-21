#pragma once
// 교회 펌웨어 공용 인증정보 — **템플릿**. 실제 값은 ChurchSecrets.h 에 둔다(깃 제외).
//
// 받아 간 뒤 한 번: cp secrets/ChurchSecrets.example.h secrets/ChurchSecrets.h
// 그다음 아래 값을 제 망·제 교회의 것으로 채운다.
//
// 세 프로젝트(funfunFirmware · nfcProject/master · nfcProject/single)가 이 한 파일을 함께
// 본다 — 같은 망, 같은 서버, 같은 기기 키다. 프로젝트마다 한 벌씩 두면 와이파이
// 비밀번호를 바꿀 때 한 곳을 빠뜨리고, 그 기기만 조용히 못 붙는다.
// 들어가는 길은 platformio.ini 의 build_flags = -I <이 폴더> 다.

// ── 와이파이 (우선순위 4개) ───────────────────────────────────────
// 0→1→2→3 순서대로 붙어 본다. 넷 다 안 되면 처음으로 돌아가 반복한다.
// 쓸 곳이 하나뿐이면 넷에 같은 값을 적어 두면 된다.
// ※ ESP32 는 2.4GHz 만 본다 — 5GHz 전용 AP 는 못 붙고 다음 순위로 넘어간다.
#define WIFI_SSID_0 "first-ssid"
#define WIFI_PASS_0 "first-password"
#define WIFI_SSID_1 "second-ssid"
#define WIFI_PASS_1 "second-password"
#define WIFI_SSID_2 "third-ssid"
#define WIFI_PASS_2 "third-password"
#define WIFI_SSID_3 "fourth-ssid"
#define WIFI_PASS_3 "fourth-password"

// ── 펀펀포인트 기기 키 ────────────────────────────────────────────
// 운영 서버(.env.docker)의 TALENT_DEVICE_KEY 와 같아야 한다. 이 키가 곧 교회다 —
// 서버는 이 값으로 어느 교회의 기기인지 가른다(tenant/resolve.js 의 cidByKey).
// ★ 새면 그 교회의 포인트를 올릴 수 있다. 공개 저장소·채팅·스크린샷에 올리지 말 것.
#define TALENT_DEVICE_KEY "여기에-기기-키"
