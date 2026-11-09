# 이 보드(v3 리더 · ESP32-S3 DevKitC)에만 굽는다 — USB 구멍을 바꿔도, 화면(../display)을 같이 꽂아도.
#
# ── 왜 포트 이름을 박아 두면 안 되나 ──────────────────────────────
# macOS 는 포트 이름을 USB **위치**에서 만든다(/dev/cu.usbmodem<위치><인터페이스>).
# 2-1 구멍이면 usbmodem2101, 허브를 거쳐 2-1.3 이 되면 usbmodem21301 이다. 같은 보드인데
# 선을 옮겼을 뿐인데 이름이 달라져, platformio.ini 에 번호를 박아 두면 그때마다 어긋난다.
#
# ── 게다가 보드가 둘이다 ──────────────────────────────────────────
# 화면(LILYGO T-RGB · ESP32-S3)과 리더(ESP32-S3 DevKitC 의 'USB' 구멍)가 **USB ID 가 303A:1001 로
# 똑같다**(둘 다 내장 USB-JTAG). 둘을 같이 꽂아 두면 PlatformIO 가 알아서 고를 때 엉뚱한
# 쪽에 구울 수 있다 — 리더에 화면 펌웨어가 올라가면 두 보드를 다 다시 구워야 한다.
# 구별되는 것은 **시리얼(칩의 MAC)** 뿐이라, 그것으로 찾는다.
#
# 보드를 바꾸면 아래 한 줄만 고치면 된다 — `pio device list` 의 SER= 값이다.
Import("env")

BOARD_SERIAL = "A4:CB:8F:D3:52:20"      # 리더 — ESP32-S3 DevKitC N16R8 (v2 를 구웠던 그 보드 · fun-2)


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
    print("[포트] 리더 DevKitC(%s) → %s" % (BOARD_SERIAL, port))
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
