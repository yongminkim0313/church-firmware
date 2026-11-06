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
    # ★ 못 찾으면 **업로드를 멈춘다** (2026-11-06).
    #   예전에는 PlatformIO 의 자동 고르기로 넘겼는데, 이 보드가 안 꽂힌 채 다른 ESP32 하나만
    #   꽂혀 있으면 그쪽을 잡아 **엉뚱한 보드의 펌웨어를 덮어썼다**(실제로 일어났다).
    #   빌드(`pio run`)는 보드 없이도 되게 두고, 막는 것은 업로드 바로 앞뿐이다.
    #   다른 보드에 굽고 싶으면 위 BOARD_SERIAL 을 그 보드의 SER= 값으로 바꾼다.
    print("[포트] 시리얼 %s 인 보드를 못 찾았습니다 — 업로드는 하지 않습니다" % BOARD_SERIAL)

    def _stop_upload(target, source, env):
        from serial.tools import list_ports
        print("\n[포트] 업로드를 멈춥니다 — 시리얼 %s 인 보드가 꽂혀 있지 않습니다." % BOARD_SERIAL)
        seen = [p for p in list_ports.comports() if "SER=" in (p.hwid or "")]
        if seen:
            print("[포트] 지금 꽂혀 있는 것 (다른 보드다 — 여기에 굽지 않는다):")
            for p in seen:
                print("         %s  %s" % (p.device, p.hwid))
        else:
            print("[포트] 지금 꽂혀 있는 USB 보드가 없습니다.")
        print("[포트] 이 보드를 꽂거나, 보드를 바꿨다면 find_port.py 의 BOARD_SERIAL 을 고치세요.\n")
        env.Exit(1)

    env.AddPreAction("upload", _stop_upload)
