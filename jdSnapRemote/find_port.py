# 이 보드(리모컨)에만 굽는다.
#
# nfcProject/slave 와 같은 ESP32-C3 SuperMini 라 USB ID 가 똑같다(303A:1001 — ESP32 내장
# USB-JTAG). 둘을 같이 꽂아 두면 PlatformIO 가 알아서 고를 때 엉뚱한 쪽을 잡고, 같은 C3
# 끼리는 esptool 이 막아 주지도 않는다 — **엉뚱한 보드에 구워진다.**
# 구별되는 것은 시리얼(칩의 MAC)뿐이라, 그것으로 찾는다.
#
# ★ 받아 간 뒤 한 번: 보드를 꽂고 `pio device list` 의 SER= 값을 아래에 적는다.
#   비워 두면(기본) PlatformIO 의 평소 자동 고르기로 넘긴다 — 보드가 하나만 꽂혀 있으면
#   그것으로도 된다.
Import("env")

BOARD_SERIAL = ""       # 예: "9C:CC:01:7B:E3:78"  (리모컨 보드의 시리얼)


def find_board():
    from serial.tools import list_ports
    want = BOARD_SERIAL.replace(":", "").lower()
    if not want:
        return None
    for p in list_ports.comports():
        # hwid 는 'USB VID:PID=303A:1001 SER=9C:CC:01:7B:E3:78 LOCATION=2-1' 꼴이다.
        if want in (p.hwid or "").replace(":", "").lower():
            return p.device
    return None


port = find_board()
if port:
    env.Replace(UPLOAD_PORT=port)
    print("[포트] jdSnapRemote(%s) → %s" % (BOARD_SERIAL, port))
elif BOARD_SERIAL:
    print("[포트] 시리얼 %s 인 보드를 못 찾았습니다. PlatformIO 가 알아서 고릅니다 —"
          " 다른 C3 보드가 같이 꽂혀 있으면 그쪽에 구워질 수 있습니다." % BOARD_SERIAL)
else:
    print("[포트] find_port.py 의 BOARD_SERIAL 이 비어 있습니다 —"
          " PlatformIO 가 알아서 고릅니다(C3 보드를 하나만 꽂아 두세요).")
