// 꾸미포토(jdSnap) 부스 리모컨 — ESP32-C3 SuperMini
//
// 부스 앞에 둔 단추를 누르면 아이패드·태블릿의 꾸미포토 앱이 네 컷을 찍기 시작한다.
// 기기를 만지러 다가가지 않아도 되게 하려는 것이다.
//
// ## 두 길 중 하나로 붙는다 (보드에서 고른다)
//
// | | GATT (권함) | HID (블루투스 키보드) |
// |--|--|--|
// | 무엇 | 전용 BLE 서비스. 이 보드가 주변기기, 앱이 Central | 이 보드가 **키보드 흉내** |
// | 페어링 | **없다** — 앱이 서비스 UUID 로 찾아 바로 붙는다 | 기기 설정에서 **한 번 페어링해야 한다** |
// | 돌려받기 | 된다 — 앱이 지금 화면을 써 준다(LED·부저가 카운트다운을 같이 센다) | **안 된다** — 키보드에는 돌려줄 길이 없다 |
// | 보내는 것 | `[seq, button]` 두 바이트 | 스페이스(셔터) · ESC(처음으로) |
//
// 앱 쪽은 두 길을 같이 켜 둘 수 있으니(관리 화면 → 리모컨 → 연결 방식 '둘 다'),
// 리모컨에서 하나만 고르면 된다. 바꾸는 법은 config.h 의 '어느 길로 붙을지'.
//
// ## 왜 와이파이가 아닌가
//
// 아이패드는 AirPrint 프린터와 **같은 와이파이에 있어야** 인쇄가 되므로 이 보드의 AP 에
// 붙일 수 없고, 현장 공유기는 기기끼리의 통신을 막는 일이 흔하다(AP isolation).
// 게다가 꾸미포토는 네트워크가 끊겨도 도는 부스다 — 셔터를 와이파이에 매달면 그 원칙이
// 깨진다. 그래서 **이 보드는 어느 와이파이에도 붙지 않는다**(ChurchSecrets.h 도 보지 않는다).
//
// ## GATT 차림 — 앱의 `BoothRemote` 와 글자 하나까지 같아야 한다
//
//   Service  7B2E0001-5F3A-4C21-9B6E-1D7C4A2F8E10
//    ├ 7B2E0002-…  [notify]  기기 → 앱  [seq, button]
//    └ 7B2E0003-…  [write]   앱 → 기기  [state, value]
//
//   seq    0~255 로 도는 누름 번호(앱이 같은 번호를 두 번 받으면 흘려보낸다)
//   button 1 = 셔터, 2 = 처음으로
//   state  0 대기 · 1 카운트다운 · 2 찰칵 · 3 확인 화면 · 4 인쇄 중 · 5 관리 화면
//   value  state 가 1 이면 남은 초, 그 밖에는 찍힌 장수
//
// 앱 쪽 짝: `jdSnap/JdSnap/Services/BoothRemote.swift` ·
//          `jdSnap/android/…/service/BoothRemote.kt` (셋이 같이 고쳐져야 한다)
//
// ## 시리얼 (115200)
//   m  길 바꾸기(GATT ↔ HID) — NVS 에 남기고 다시 켠다
//   s  지금 형편
//   b  셔터를 누른 것처럼
//   n  '처음으로' 를 누른 것처럼   ← 단추를 달기 전에 앱을 시험해 볼 때

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>   // NimBLEDevice.h 가 끌어오지 않는다 — 따로 넣어야 한다
#include <Preferences.h>

#include "config.h"

// ── 앱과 맞춘 값 ──────────────────────────────────────────────────
static const char* SERVICE_UUID = "7B2E0001-5F3A-4C21-9B6E-1D7C4A2F8E10";
static const char* BUTTON_UUID  = "7B2E0002-5F3A-4C21-9B6E-1D7C4A2F8E10";
static const char* STATE_UUID   = "7B2E0003-5F3A-4C21-9B6E-1D7C4A2F8E10";

enum Button : uint8_t { BTN_SHUTTER = 1, BTN_BACK = 2 };
enum AppState : uint8_t {
  ST_IDLE = 0, ST_COUNTDOWN = 1, ST_SHOT = 2, ST_REVIEW = 3, ST_PRINTING = 4, ST_SETTINGS = 5
};

// HID 키보드가 보내는 키(USB HID Usage Table, 키보드 페이지)
static const uint8_t KEY_SPACE  = 0x2C;   // 셔터
static const uint8_t KEY_ESCAPE = 0x29;   // 처음으로

// ── 들고 있는 것 ──────────────────────────────────────────────────
static Preferences prefs;
static bool modeHid = DEFAULT_MODE_HID;

static NimBLEServer* server = nullptr;
static NimBLECharacteristic* buttonChar = nullptr;   // GATT 모드
static NimBLECharacteristic* stateChar = nullptr;    // GATT 모드
static NimBLECharacteristic* hidInput = nullptr;     // HID 모드

static volatile bool connected = false;
static uint8_t seq = 0;
static uint8_t appState = ST_IDLE;
static uint8_t appValue = 0;
/** 카운트다운을 두 번 울리지 않게 — 같은 숫자가 다시 와도 한 번만 센다. */
static uint8_t lastCountdown = 0;

// ── LED ───────────────────────────────────────────────────────────
static uint32_t ledFlashUntil = 0;

static inline void ledWrite(bool on) {
#if PIN_LED >= 0
  digitalWrite(PIN_LED, LED_ACTIVE_LOW ? !on : on);
#else
  (void)on;
#endif
}

/** 한 번 번쩍(카운트다운 한 칸, 찰칵, 단추가 먹혔다는 표시). */
static void ledFlash(uint32_t ms) { ledFlashUntil = millis() + ms; }

/**
 * 지금 형편을 LED 하나로 알린다.
 *
 * | 형편 | 깜빡임 |
 * |--|--|
 * | 못 붙었다(찾는 중) | 1초에 한 번 짧게 |
 * | 붙었다 · 대기 | 3초에 한 번 아주 짧게(살아 있다) |
 * | 카운트다운 · 찰칵 | 그때마다 한 번 번쩍 |
 * | 확인 화면 | 계속 켜 둔다(이제 출력을 누를 차례) |
 * | 인쇄 중 | 빠르게 깜빡 |
 * | 관리 화면 | 두 번씩 깜빡 |
 *
 * HID 모드에서는 앱이 아무것도 돌려주지 않으므로 **'대기' 와 '못 붙었다' 만** 나온다.
 */
static void ledTick() {
  const uint32_t now = millis();
  if (now < ledFlashUntil) { ledWrite(true); return; }

  bool on = false;
  if (!connected) {
    on = (now % 1000) < 100;
  } else {
    switch (appState) {
      case ST_REVIEW:   on = true; break;
      case ST_PRINTING: on = (now % 240) < 120; break;
      case ST_SETTINGS: { const uint32_t t = now % 2000; on = (t < 60) || (t >= 180 && t < 240); } break;
      default:          on = (now % 3000) < 60; break;
    }
  }
  ledWrite(on);
}

// ── 부저 ──────────────────────────────────────────────────────────
#if PIN_BUZZER >= 0
static uint32_t buzzUntil = 0;
#endif

static void beep(uint16_t freq, uint32_t ms) {
#if PIN_BUZZER >= 0
#if BUZZER_PASSIVE
  tone(PIN_BUZZER, freq, ms);
#else
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_HIGH ? HIGH : LOW);
  buzzUntil = millis() + ms;
#endif
#else
  (void)freq; (void)ms;
#endif
}

static void buzzTick() {
#if PIN_BUZZER >= 0 && !BUZZER_PASSIVE
  if (buzzUntil && millis() >= buzzUntil) {
    digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
    buzzUntil = 0;
  }
#endif
}

// ── 단추 보내기 ───────────────────────────────────────────────────

/** HID 모드 — 키를 눌렀다 뗀다(8바이트 표준 키보드 리포트). */
static void sendKey(uint8_t keycode) {
  if (!hidInput) return;
  uint8_t report[8] = {0, 0, keycode, 0, 0, 0, 0, 0};
  hidInput->setValue(report, sizeof(report));
  hidInput->notify();
  delay(12);                      // 눌림과 뗌이 한 덩이로 뭉치지 않게
  memset(report, 0, sizeof(report));
  hidInput->setValue(report, sizeof(report));
  hidInput->notify();
}

static void sendAction(uint8_t button) {
  if (!connected) {
    Serial.printf("[단추] %s — 아직 안 붙었습니다(버립니다)\n",
                  button == BTN_SHUTTER ? "셔터" : "처음으로");
    // 먹히지 않았다는 표시로 길게 번쩍 + 낮은 소리.
    ledFlash(400);
    beep(400, 120);
    return;
  }

  if (modeHid) {
    sendKey(button == BTN_SHUTTER ? KEY_SPACE : KEY_ESCAPE);
  } else {
    // seq 를 올려 보낸다 — 알림이 겹쳐 와도 앱이 한 번으로 센다.
    seq++;
    const uint8_t payload[2] = { seq, button };
    buttonChar->setValue(payload, sizeof(payload));
    buttonChar->notify();
  }

  Serial.printf("[단추] %s 보냄%s\n", button == BTN_SHUTTER ? "셔터" : "처음으로",
                modeHid ? "(HID)" : "(GATT)");
  ledFlash(80);
  beep(button == BTN_SHUTTER ? 2200 : 1200, 40);
}

// ── 단추 읽기 ─────────────────────────────────────────────────────
// 단추가 **하나뿐이면** 짧게 = 셔터, 길게 = 처음으로 다. 그래서 하나일 때는 손을
// **뗄 때** 셔터가 나간다(길게 누른 것인지 그때야 알 수 있다).
// 두 번째 단추를 달았으면 둘 다 **누르는 순간** 바로 나간다.

struct Btn {
  int pin = -1;
  bool down = false;
  uint32_t changedAt = 0;
  bool longFired = false;
};

static Btn btnMain;
static Btn btnBack;

static bool readPin(int pin) {
  const int v = digitalRead(pin);
  return BUTTON_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

static void setupButton(Btn& b, int pin) {
  if (pin < 0) return;
  b.pin = pin;
  pinMode(pin, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  b.down = readPin(pin);
  b.changedAt = millis();
}

static void buttonsTick() {
  const uint32_t now = millis();
  const bool twoButtons = (btnBack.pin >= 0);

  // 셔터(또는 하나뿐인 단추)
  if (btnMain.pin >= 0) {
    const bool down = readPin(btnMain.pin);
    if (down != btnMain.down && now - btnMain.changedAt > DEBOUNCE_MS) {
      btnMain.down = down;
      btnMain.changedAt = now;
      if (down) {
        btnMain.longFired = false;
        if (twoButtons) sendAction(BTN_SHUTTER);
      } else if (!twoButtons && !btnMain.longFired) {
        sendAction(BTN_SHUTTER);
      }
    }
    // 하나뿐일 때 — 쥐고 있으면 '처음으로'
    if (!twoButtons && btnMain.down && !btnMain.longFired &&
        now - btnMain.changedAt > LONG_PRESS_MS) {
      btnMain.longFired = true;
      sendAction(BTN_BACK);
    }
  }

  // 두 번째 단추 — 누르는 순간 '처음으로'
  if (btnBack.pin >= 0) {
    const bool down = readPin(btnBack.pin);
    if (down != btnBack.down && now - btnBack.changedAt > DEBOUNCE_MS) {
      btnBack.down = down;
      btnBack.changedAt = now;
      if (down) sendAction(BTN_BACK);
    }
  }
}

// ── BLE 콜백 ──────────────────────────────────────────────────────

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*s*/, NimBLEConnInfo& /*info*/) override {
    connected = true;
    appState = ST_IDLE;
    lastCountdown = 0;
    Serial.println("[BLE] 붙었습니다.");
    beep(1800, 60);
  }

  void onDisconnect(NimBLEServer* /*s*/, NimBLEConnInfo& /*info*/, int reason) override {
    connected = false;
    appState = ST_IDLE;
    Serial.printf("[BLE] 끊겼습니다(reason=%d) — 다시 알립니다.\n", reason);
    // 부스는 켜 둔 채 하루를 보낸다 — **다시 붙는 것이 제일 중요하다.**
    NimBLEDevice::startAdvertising();
  }
};

/** 앱이 지금 화면을 써 준다(GATT 모드만) — `[state, value]` 두 바이트. */
class StateCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& /*info*/) override {
    const NimBLEAttValue v = ch->getValue();
    if (v.length() < 1) return;

    appState = v[0];
    appValue = (v.length() >= 2) ? v[1] : 0;

    switch (appState) {
      case ST_COUNTDOWN:
        // 같은 숫자가 다시 와도 한 번만 센다(앱이 붙는 순간에도 지금 상태를 한 번 보낸다).
        if (appValue > 0 && appValue != lastCountdown) {
          lastCountdown = appValue;
          ledFlash(120);
          beep(appValue <= 1 ? 2600 : 1600, 70);
        }
        break;
      case ST_SHOT:
        lastCountdown = 0;
        ledFlash(250);
        beep(2900, 120);
        break;
      default:
        lastCountdown = 0;
        break;
    }
    Serial.printf("[화면] state=%u value=%u\n", appState, appValue);
  }
};

// ── 켜기 ──────────────────────────────────────────────────────────

static void startGatt() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(TX_POWER_DBM);
  // **본딩·암호화를 걸지 않는다** — 걸면 아이패드 설정 화면에서 페어링을 받아야 하고
  // 현장에서 그게 꼬인다. 부스 기기끼리 쓰는 전용 UUID 면 족하다.

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = server->createService(SERVICE_UUID);
  buttonChar = service->createCharacteristic(BUTTON_UUID, NIMBLE_PROPERTY::NOTIFY);
  stateChar = service->createCharacteristic(
      STATE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  stateChar->setCallbacks(new StateCallbacks());
  // 서비스를 따로 start() 하지 않는다 — NimBLE 2.x 는 **알리기를 시작할 때** 서버가
  // 가진 서비스를 통째로 올린다(`NimBLEService::start()` 는 아무 일도 하지 않는 옛 함수다).

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // ★★ **이 세 줄의 순서가 중요하다.** 알림 한 덩이는 31바이트뿐인데, 128비트 서비스
  //    UUID 하나가 18바이트를 먹는다(+ 플래그 3 = 21). 이름까지 같은 덩이에 넣으면
  //    넘쳐서 **UUID 가 조용히 뒤쪽(스캔 응답)으로 밀려나고**, 그러면 앱의 걸러 찾기
  //    (`scanForPeripherals(withServices:)` · `ScanFilter`)가 리모컨을 못 본다.
  //    그래서 **스캔 응답을 먼저 켠다** — NimBLE 의 `setName` 은 스캔 응답이 켜져
  //    있으면 이름을 그쪽에 넣으므로, 앞 덩이가 UUID 몫으로 비워진다.
  adv->enableScanResponse(true);
  adv->setName(DEVICE_NAME);            // → 스캔 응답으로 간다(사람이 보는 이름)
  adv->addServiceUUID(SERVICE_UUID);    // → 알림 덩이에 남는다(앱이 이것으로 찾는다)
  adv->start();

  Serial.println("[BLE] GATT 로 알립니다 — 앱 관리 화면의 '연결 방식' 을 GATT 나 '둘 다' 로.");
}

// 표준 8바이트 키보드. 꾸미포토는 스페이스·ESC 두 키만 쓰지만, 리포트 틀은 흔한 키보드
// 그대로 둔다 — 아이패드·안드로이드가 군말 없이 받아들이는 모양이다.
static const uint8_t HID_REPORT_MAP[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x05, 0x07,        //   Usage Page (Keyboard)
  0x19, 0xE0,        //   Usage Minimum (Left Control)
  0x29, 0xE7,        //   Usage Maximum (Right GUI)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)  ← 조합키 여덟 개
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x03,        //   Input (Constant)                  ← 예약된 한 바이트
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Keyboard)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0x65,        //   Usage Maximum (101)
  0x81, 0x00,        //   Input (Data, Array)               ← 동시에 누른 키 여섯 개
  0xC0               // End Collection
};

static void startHid() {
  NimBLEDevice::init(DEVICE_NAME_HID);
  NimBLEDevice::setPower(TX_POWER_DBM);
  // HID 는 **페어링(본딩)을 해야** 아이패드가 키보드로 받아들인다 — GATT 쪽과 반대다.
  // 화면도 자판도 없는 기기라 IO 능력은 '없음' 으로 알린다(숫자 맞추기 없이 붙는다).
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEHIDDevice* hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Jesus Dream Church");
  hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
  hid->setBatteryLevel(100);   // startServices() 는 부르지 않는다(위 GATT 쪽과 같은 사정)

  hidInput = hid->getInputReport(1);   // 리포트 맵의 Report ID (1)

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // GATT 쪽과 같은 순서로 둔다(여기 HID 서비스 UUID 는 16비트라 자리가 넉넉하지만,
  // 두 자리의 모양을 맞춰 두는 편이 나중에 덜 헷갈린다).
  adv->enableScanResponse(true);
  adv->setName(DEVICE_NAME_HID);
  adv->setAppearance(HID_KEYBOARD);    // 기기 목록에 키보드로 보인다(0x03C1)
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->start();

  Serial.printf("[BLE] HID 키보드(%s)로 알립니다 — 아이패드 설정 → 블루투스에서 "
                "한 번 페어링하세요.\n", DEVICE_NAME_HID);
}

/** 켜는 동안 단추를 쥐고 있으면 길을 바꾼다(GATT ↔ HID). */
static void maybeToggleModeAtBoot() {
  if (btnMain.pin < 0 || !readPin(btnMain.pin)) return;

  Serial.printf("[길] 단추를 쥐고 켰습니다 — %lums 더 쥐고 있으면 길을 바꿉니다.\n",
                (unsigned long)MODE_TOGGLE_HOLD_MS);
  const uint32_t start = millis();
  while (readPin(btnMain.pin)) {
    if (millis() - start >= MODE_TOGGLE_HOLD_MS) {
      modeHid = !modeHid;
      prefs.putBool("hid", modeHid);
      Serial.printf("[길] %s 로 바꿨습니다.\n", modeHid ? "HID" : "GATT");
      // 세 번 번쩍 — 바뀐 것을 눈으로 알려 준다.
      for (int i = 0; i < 3; i++) {
        ledWrite(true); beep(2400, 80); delay(120);
        ledWrite(false); delay(120);
      }
      // 단추를 뗄 때까지 기다린다 — 안 그러면 그 손이 첫 셔터로 들어간다.
      while (readPin(btnMain.pin)) delay(20);
      btnMain.down = false;
      btnMain.changedAt = millis();
      return;
    }
    delay(20);
  }
  Serial.println("[길] 손을 떼서 그대로 둡니다.");
}

static void printStatus() {
  Serial.printf("[형편] 길=%s · %s · state=%u value=%u · 단추=%d/%d · seq=%u\n",
                modeHid ? "HID" : "GATT",
                connected ? "붙었다" : "안 붙었다",
                appState, appValue, PIN_BUTTON, PIN_BUTTON_BACK, seq);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n꾸미포토 부스 리모컨 (ESP32-C3)");

#if PIN_LED >= 0
  pinMode(PIN_LED, OUTPUT);
  ledWrite(false);
#endif
#if PIN_BUZZER >= 0 && !BUZZER_PASSIVE
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
#endif

  setupButton(btnMain, PIN_BUTTON);
  setupButton(btnBack, PIN_BUTTON_BACK);

  prefs.begin("jdsnap", false);
  modeHid = prefs.getBool("hid", DEFAULT_MODE_HID);
  maybeToggleModeAtBoot();

  if (modeHid) startHid(); else startGatt();
  printStatus();
}

void loop() {
  buttonsTick();
  ledTick();
  buzzTick();

  // 시리얼 명령 — 단추를 달기 전에 앱을 시험해 볼 때 쓴다.
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'm':
        modeHid = !modeHid;
        prefs.putBool("hid", modeHid);
        Serial.printf("[길] %s 로 바꿨습니다 — 다시 켭니다.\n", modeHid ? "HID" : "GATT");
        delay(200);
        ESP.restart();            // BLE 차림이 아예 달라 올린 채로는 못 바꾼다
        break;
      case 's': printStatus(); break;
      case 'b': sendAction(BTN_SHUTTER); break;
      case 'n': sendAction(BTN_BACK); break;
      default: break;
    }
  }

  delay(5);
}
