# 이 보드(리더)에만 굽는다 — 화면 보드(master)와 USB ID 가 똑같아서다.
#
# 둘 다 303A:1001(ESP32 내장 USB-JTAG)로 보여, 같이 꽂아 두면 PlatformIO 가 알아서 고를 때
# 엉뚱한 쪽을 잡는다. 칩 종류가 달라(C3 / S3) esptool 이 막아 주기는 하지만, 그때마다
# 빌드가 실패로 끝나고 어느 보드가 어느 포트인지 다시 찾아야 한다.
# 구별되는 것은 **시리얼(칩의 MAC)** 뿐이라, 그것으로 찾는다.
#
# macOS 는 포트 이름을 USB **위치**에서 만든다(2-1 → usbmodem2101, 허브의 2-1.3 → usbmodem21301).
# 그래서 번호를 박아 두면 선을 옮길 때마다 어긋난다 — 이름 말고 시리얼로 찾는 까닭이다.
#
# 보드를 바꾸면 아래 한 줄만 고치면 된다 — `pio device list` 의 SER= 값이다.
# (화면 보드 쪽에도 같은 파일이 있다: master/find_port.py — 그쪽은 68:EE:8F:47:30:FC)
Import("env")

BOARD_SERIAL = "9C:CC:01:7B:E3:78"      # slave — NFC 리더 (ESP32-C3 SuperMini)


def find_board():
    from serial.tools import list_ports
    want = BOARD_SERIAL.replace(":", "").lower()
    if not want:
        return None
    for p in list_ports.comports():
        # hwid 는 'USB VID:PID=303A:1001 SER=9C:CC:01:7B:E3:78 LOCATION=2-1' 꼴이다.
        # 콜론을 떼고 견줘서, 시리얼을 콜론 있이 적든 없이 적든 걸리게 한다.
        if want in (p.hwid or "").replace(":", "").lower():
            return p.device
    return None


port = find_board()
if port:
    env.Replace(UPLOAD_PORT=port)
    print("[포트] slave(%s) → %s" % (BOARD_SERIAL, port))
else:
    # 못 찾아도 멈추지 않는다 — PlatformIO 의 평소 자동 고르기로 넘긴다.
    print("[포트] 시리얼 %s 인 보드를 못 찾았습니다. PlatformIO 가 알아서 고릅니다 —"
          " 화면 보드(master)가 같이 꽂혀 있으면 그쪽을 잡을 수 있습니다." % BOARD_SERIAL)
