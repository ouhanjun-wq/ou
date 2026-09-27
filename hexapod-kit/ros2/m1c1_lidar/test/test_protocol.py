"""Unit tests for the M1C1-Mini protocol parser (no ROS needed):

    python3 hexapod-kit/ros2/m1c1_lidar/test/test_protocol.py
"""
import math
import os
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from m1c1_lidar import fake_bridge  # noqa: E402
from m1c1_lidar import protocol as p  # noqa: E402


def datasheet_packet():
    # 开发手册 v1.1 第 6 页的例子：AA 55 00 19 39 18 97 23，Distance_1 = 1000，Distance_LSN = 8000
    dists = [1000] + [2000] * 23 + [8000]
    return p.build_packet(0, 0x1839, 0x2397, dists)


class ProtocolTest(unittest.TestCase):
    def test_datasheet_example(self):
        pkt = datasheet_packet()
        self.assertEqual(pkt[:8], bytes([0xAA, 0x55, 0x00, 0x19, 0x39, 0x18, 0x97, 0x23]))
        pts = p.decode_points(0x1839, 0x2397, [int.from_bytes(pkt[10 + 2 * k:12 + 2 * k], "little") for k in range(25)])
        self.assertEqual(len(pts), 25)
        self.assertAlmostEqual(pts[0][0], 37.4933, places=3)
        # 手册写 71.1718 - 11.8653 = 59.3065；按手册公式精确算修正角是 11.8674，差 0.002 度是手册的舍入
        self.assertAlmostEqual(pts[-1][0], 59.3065, delta=0.01)
        self.assertEqual(pts[0][1], 1000)
        self.assertEqual(pts[-1][1], 8000)

    def test_distance_example(self):
        # 手册：采样数据 E4 6F -> 0x6FE4 >> 2 = 7161 mm
        self.assertEqual(p.decode_points(0x0001, 0x0001, [0x6FE4])[0][1], 7161)

    def test_revolutions_and_resync(self):
        start = p.build_packet(1, 0x0001, 0x0001, [500])
        body = datasheet_packet()
        noise = bytes([0xFE, 0xFF, 0xFF, 0xAA, 0x55, 0x50, 0x07, 0, 0, 0, 0, 0, 0, 0, 0xA8])  # spin-up + start reply
        corrupt = bytearray(body)
        corrupt[12] ^= 0x40                          # bad checksum -> dropped
        stream = noise + start + body + bytes(corrupt) + body + start + body + start
        parser = p.Parser()
        revs = []
        for k in range(0, len(stream), 7):           # arrives in small pieces
            revs += parser.feed(stream[k:k + 7])
        self.assertEqual(len(revs), 2)
        self.assertEqual(len(revs[0]), 1 + 25 + 25)  # start point + 2 good packets
        self.assertEqual(len(revs[1]), 1 + 25)
        self.assertEqual(parser.bad_packets, 1)

    def test_wraparound_angles(self):
        fsa = (int(350 * 64) << 1) | 1
        lsa = (int(10 * 64) << 1) | 1
        pts = p.decode_points(fsa, lsa, [0] * 21)    # distance 0: no correction
        self.assertAlmostEqual(pts[0][0], 350.0, places=3)
        self.assertAlmostEqual(pts[10][0], 0.0, places=3)
        self.assertAlmostEqual(pts[20][0], 10.0, places=3)

    def test_to_ranges(self):
        # clockwise 90 deg (to the right) -> ROS -90 deg; clockwise 0 -> ROS 0 (straight ahead)
        r = p.to_ranges([(90.0, 1500), (0.0, 2000), (180.0, 5), (270.0, 0)], 360)
        inc = 2 * math.pi / 360
        self.assertAlmostEqual(r[int(round((-math.pi / 2 + math.pi) / inc))], 1.5)
        self.assertAlmostEqual(r[180], 2.0)
        self.assertEqual(sum(1 for x in r if not math.isinf(x)), 2)   # 5 mm and 0 mm dropped


class FakeBridgeTest(unittest.TestCase):
    def test_room_over_tcp(self):
        """End to end without ROS: fake bridge -> TCP -> start command -> Parser -> ranges."""
        ready = []
        t = threading.Thread(target=fake_bridge.serve, kwargs={"port": 0, "hz": 50, "once": True, "ready": ready},
                             daemon=True)
        t.start()
        for _ in range(100):
            if ready:
                break
            time.sleep(0.01)
        sock = socket.create_connection(("127.0.0.1", ready[0]), timeout=2)
        sock.sendall(p.START_CMD)
        parser = p.Parser()
        revs = []
        while len(revs) < 2:
            revs += parser.feed(sock.recv(4096))
        sock.close()
        r = p.to_ranges(revs[1], 360)
        ahead, left, right, back = r[180], r[270], r[90], r[0]
        self.assertAlmostEqual(ahead, 2.0, delta=0.02)      # room: x_max = 2.0 m ahead
        self.assertAlmostEqual(left, 1.2, delta=0.02)       # y_max = 1.2 m to the left
        self.assertAlmostEqual(right, 1.2, delta=0.02)
        self.assertAlmostEqual(back, 1.5, delta=0.02)


if __name__ == "__main__":
    unittest.main()
