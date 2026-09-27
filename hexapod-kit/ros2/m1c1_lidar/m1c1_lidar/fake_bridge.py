"""Fake XIAO lidar bridge: serves M1C1-Mini packets of a simulated rectangular room on TCP.

Test the PC side (node, slam_toolbox, RViz) before the real lidar arrives:

    ros2 run m1c1_lidar fake_bridge            # terminal 1 (listens on port 3333)
    ros2 launch m1c1_lidar mapping.launch.py host:=127.0.0.1

Like the real lidar it stays quiet until it receives the start command.
"""
import argparse
import math
import socket
import threading
import time

from m1c1_lidar import protocol

ROOM = (-1.5, 2.0, -1.2, 1.2)     # x_min, x_max, y_min, y_max (m) around the lidar; x = ahead


def cast(angle_cw_deg, room=ROOM):
    """Distance (mm) to the room wall along a clockwise lidar angle (0 = ahead, 90 = right)."""
    a = math.radians(-angle_cw_deg)            # to counter-clockwise
    dx, dy = math.cos(a), math.sin(a)
    best = math.inf
    x0, x1, y0, y1 = room
    for t in ((x1 / dx) if dx > 1e-9 else math.inf, (x0 / dx) if dx < -1e-9 else math.inf,
              (y1 / dy) if dy > 1e-9 else math.inf, (y0 / dy) if dy < -1e-9 else math.inf):
        if 0 < t < best:
            best = t
    return int(min(best, 8.0) * 1000)


def revolution_packets(points_per_rev=360, per_packet=40):
    """One revolution: a start packet, then packets whose raw angles map back onto the room."""
    step = 360.0 / points_per_rev
    out = [protocol.build_packet(1, 1, 1, [cast(0.0)])]
    for first in range(0, points_per_rev, per_packet):
        idx = range(first, min(first + per_packet, points_per_rev))
        raw = [i * step for i in idx]
        dists = []
        for r in raw:
            d = cast(r)
            d = cast(r - protocol.ang_correct(d))     # the device reports raw = true + correction
            dists.append(d)
        fsa = (int(round(raw[0] * 64)) << 1) | 1
        lsa = (int(round(raw[-1] * 64)) << 1) | 1
        out.append(protocol.build_packet(0, fsa, lsa, dists))
    return out


def serve(port=3333, hz=10.0, once=False, ready=None):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)
    if ready is not None:
        ready.append(srv.getsockname()[1])
    packets = b"".join(revolution_packets())
    while True:
        conn, _ = srv.accept()
        with conn:
            conn.settimeout(0.1)
            spinning = False
            try:
                while True:
                    try:
                        cmd = conn.recv(64)
                        if cmd == b"":
                            break
                        if protocol.START_CMD in cmd:
                            spinning = True
                        if protocol.STOP_CMD in cmd:
                            spinning = False
                    except socket.timeout:
                        pass
                    if spinning:
                        conn.sendall(packets)
                        time.sleep(1.0 / hz)
            except OSError:
                pass
        if once:
            srv.close()
            return


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=3333)
    args = ap.parse_args()
    print(f"fake M1C1 bridge on port {args.port}; room {ROOM} m")
    serve(args.port)


if __name__ == "__main__":
    main()
