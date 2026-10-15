# 이 보드에만 굽는다 — USB 구멍을 바꿔도.
#
# ── 왜 포트 이름을 박아 두면 안 되나 ──────────────────────────────
# macOS 는 포트 이름을 USB **위치**에서 만든다(/dev/cu.usbmodem<위치><인터페이스>).
# 2-1 구멍이면 usbmodem2101, 허브를 거쳐 2-1.3 이 되면 usbmodem21301 이다. 같은 보드인데
# 선을 옮겼을 뿐인데 이름이 달라져, platformio.ini 에 번호를 박아 두면 그때마다 어긋난다.
#
# ── 보드가 하나가 됐어도 그대로 둔다 ──────────────────────────────
# 리더(ESP32-C3)가 없어져 '엉뚱한 보드에 굽는' 걱정은 줄었다. 그래도 LILYGO 보드들은
# USB ID 가 303A:1001 로 다 똑같아(내장 USB-JTAG), 다른 ESP32-S3 를 같이 꽂아 두면
# PlatformIO 가 알아서 고를 때 엉뚱한 쪽을 잡는다. 구별되는 것은 **시리얼(칩의 MAC)**
# 뿐이라, 그것으로 찾는 이 방식을 그대로 쓴다.
#
# 보드를 바꾸면 아래 한 줄만 고치면 된다 — `pio device list` 의 SER= 값이다.
Import("env")

BOARD_SERIAL = "68:EE:8F:47:30:FC"      # LILYGO T-RGB (ESP32-S3) — 이 프로젝트의 단 하나뿐인 보드


def find_board():
    from serial.tools import list_ports
    want = BOARD_SERIAL.replace(":", "").lower()
    if not want:
        return None
    for p in list_ports.comports():
        # hwid 는 'USB VID:PID=303A:1001 SER=68:EE:8F:47:30:FC LOCATION=2-1' 꼴이다.
        # 콜론을 떼고 견줘서, 시리얼을 콜론 있이 적든 없이 적든 걸리게 한다.
        if want in (p.hwid or "").replace(":", "").lower():
            return p.device
    return None


port = find_board()
if port:
    env.Replace(UPLOAD_PORT=port)
    print("[포트] T-RGB(%s) → %s" % (BOARD_SERIAL, port))
else:
    # 못 찾아도 멈추지 않는다 — PlatformIO 의 평소 자동 고르기로 넘긴다.
    # 다만 다른 보드가 같이 꽂혀 있으면 그쪽에 구워질 수 있으니 눈에 띄게 알린다.
    print("[포트] 시리얼 %s 인 보드를 못 찾았습니다. PlatformIO 가 알아서 고릅니다 —"
          " 다른 ESP32-S3 가 같이 꽂혀 있으면 그쪽에 구워질 수 있습니다." % BOARD_SERIAL)
