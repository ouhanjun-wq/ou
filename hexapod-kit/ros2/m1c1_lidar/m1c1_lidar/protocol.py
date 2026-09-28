"""M1C1-Mini lidar protocol (国科光芯 M1C1_Mini 开发手册 v1.1). Pure Python, no ROS: unit-tested.

Serial 115200 8N1. The lidar is idle after power-up; START_CMD makes it spin and stream packets:

    AA 55 | CT | LSN | FSA(2) | LSA(2) | CS(2) | S1(2) ... S_LSN(2)      (all little endian)

    CT   0 = point-cloud packet, 1 = start of a new revolution (LSN = 1)
    FSA  first sample angle, LSA last sample angle: bit 0 = check bit (always 1), angle = (x >> 1) / 64 deg
    CS   XOR of all 16-bit words except CS itself
    Si   distance = Si >> 2 (mm); 0 = no return

Angles grow clockwise (seen from above). Each sample also gets the triangulation correction
AngCorrect = atan(19.16 * (d - 90.15) / (90.15 * d)) for d > 0.
"""
import math

START_CMD = bytes([0xAA, 0x55, 0xF0, 0x0F])
STOP_CMD = bytes([0xAA, 0x55, 0xF5, 0x0A])
HEADER = b"\xAA\x55"
HEAD_LEN = 10
MAX_LSN = 0x100


def ang_correct(d_mm):
    """Triangulation angle correction (degrees) for one distance."""
    if d_mm == 0:
        return 0.0
    return math.degrees(math.atan(19.16 * (d_mm - 90.15) / (90.15 * d_mm)))


def checksum(ct, lsn, fsa, lsa, samples):
    cs = 0x55AA ^ (ct | (lsn << 8)) ^ fsa ^ lsa
    for s in samples:
        cs ^= s
    return cs


def decode_points(fsa, lsa, samples):
    """Return [(angle_deg_clockwise 0..360, distance_mm)] for one packet."""
    lsn = len(samples)
    a0 = (fsa >> 1) / 64.0
    a1 = (lsa >> 1) / 64.0
    span = a1 - a0
    if span < 0:
        span += 360.0
    step = span / (lsn - 1) if lsn > 1 else 0.0
    out = []
    for i, s in enumerate(samples):
        d = s >> 2
        a = (a0 + step * i - ang_correct(d)) % 360.0
        out.append((a, d))
    return out


def build_packet(ct, fsa, lsa, distances_mm):
    """Encode a packet (used by the tests and the simulator)."""
    samples = [d << 2 for d in distances_mm]
    lsn = len(samples)
    cs = checksum(ct, lsn, fsa, lsa, samples)
    raw = bytearray(HEADER)
    raw += bytes([ct, lsn])
    raw += fsa.to_bytes(2, "little") + lsa.to_bytes(2, "little") + cs.to_bytes(2, "little")
    for s in samples:
        raw += s.to_bytes(2, "little")
    return bytes(raw)


class Parser:
    """Feed raw bytes; get complete revolutions back.

    feed() returns a list of revolutions; each revolution is a list of (angle_deg_cw, distance_mm).
    Anything that is not a valid packet (info packets, spin-up bytes 0xFE / 0xFF, noise, packets
    with a bad checksum) is skipped by re-synchronising on the next AA 55.
    """

    def __init__(self):
        self.buf = bytearray()
        self.current = []
        self.started = False
        self.bad_packets = 0

    def feed(self, data):
        self.buf += data
        revs = []
        while True:
            i = self.buf.find(HEADER)
            if i < 0:
                del self.buf[:-1]          # keep a possible trailing 0xAA
                break
            if i > 0:
                del self.buf[:i]
            if len(self.buf) < HEAD_LEN:
                break
            ct, lsn = self.buf[2], self.buf[3]
            fsa = int.from_bytes(self.buf[4:6], "little")
            lsa = int.from_bytes(self.buf[6:8], "little")
            cs = int.from_bytes(self.buf[8:10], "little")
            if ct not in (0, 1) or lsn == 0 or lsn > MAX_LSN or not (fsa & 1) or not (lsa & 1):
                del self.buf[:2]           # not a scan packet (e.g. the "AA 55 50 07 .." start reply)
                continue
            total = HEAD_LEN + 2 * lsn
            if len(self.buf) < total:
                break
            samples = [int.from_bytes(self.buf[HEAD_LEN + 2 * k:HEAD_LEN + 2 * k + 2], "little") for k in range(lsn)]
            if checksum(ct, lsn, fsa, lsa, samples) != cs:
                self.bad_packets += 1
                del self.buf[:2]
                continue
            del self.buf[:total]
            if ct == 1:
                if self.started and self.current:
                    revs.append(self.current)
                self.current = []
                self.started = True
            if self.started:
                self.current.extend(decode_points(fsa, lsa, samples))
        return revs


def to_ranges(points, bins, offset_deg=0.0, min_mm=10, max_mm=8000):
    """Bin one revolution into a LaserScan range array (metres, inf = no return).

    ROS angles are counter-clockwise, so a clockwise lidar angle a becomes -a (+ offset).
    Bin k covers the angle  -pi + k * 2pi / bins  (angle_min = -pi, angle_increment = 2pi / bins).
    When two samples land in one bin the nearer one wins.
    """
    ranges = [math.inf] * bins
    inc = 360.0 / bins
    for a, d in points:
        if d < min_mm or d > max_mm:
            continue
        ros = (-a + offset_deg + 180.0) % 360.0      # shifted so that -180 deg -> 0
        k = int(round(ros / inc)) % bins
        r = d / 1000.0
        if r < ranges[k]:
            ranges[k] = r
    return ranges
