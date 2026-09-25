#!/usr/bin/env python3
"""Send text commands to the robot arm from a PC (or from an AI agent).

Two ways to reach the arm:
  * Wi-Fi: join the arm's access point "RobotArm" (password robotarm123) -> --http
  * USB:   the ESP32's serial port (needs `pip install pyserial`)      -> --serial COM5

Examples
  python3 tools/arm_client.py --http STATUS
  python3 tools/arm_client.py --http ON "MOVE 160 0 40 -90" "GRIP CLOSE" "UP 50" HOME
  python3 tools/arm_client.py --serial /dev/ttyUSB0 --file demo.txt     # one command per line
  python3 tools/arm_client.py --http                                    # interactive prompt

An AI agent only needs this one tool: "send_arm_command(text) -> reply". The command list is in
firmware/README.md; units are mm and degrees, gripper 0 = open .. 100 = closed.
"""
import argparse
import sys
import time
import urllib.parse
import urllib.request

WAIT_WORDS = ("MOVE", "MOVEBY", "J", "JOINTS", "HOME", "UP", "DOWN", "LEFT", "RIGHT", "FORWARD", "BACK",
              "TURN", "PITCH", "ROLL", "GRIP", "OPEN", "CLOSE", "ON", "OFF", "PARK")


class HttpLink:
    def __init__(self, host):
        self.base = f"http://{host}"

    def send(self, cmd):
        url = f"{self.base}/cmd?c={urllib.parse.quote(cmd)}"
        with urllib.request.urlopen(url, timeout=3) as r:
            return r.read().decode("utf-8", "replace").strip()

    def busy(self):
        import json
        with urllib.request.urlopen(f"{self.base}/api", timeout=3) as r:
            return json.loads(r.read())["act"] != "IDLE"


class SerialLink:
    def __init__(self, port):
        import serial  # pyserial
        self.s = serial.Serial(port, 115200, timeout=0.3)
        time.sleep(2.0)            # the ESP32 resets when the port opens
        self.s.reset_input_buffer()

    def send(self, cmd):
        self.s.write((cmd + "\n").encode())
        lines, end = [], time.time() + 1.0
        while time.time() < end:
            line = self.s.readline().decode("utf-8", "replace").strip()
            if line:
                lines.append(line)
                if line.startswith(("ok", "error")):
                    break
        return "\n".join(lines)

    def busy(self):
        return " IDLE " not in self.send("STATUS")


def run(link, cmd, wait):
    reply = link.send(cmd)
    print(f"> {cmd}\n{reply}")
    if wait and reply.startswith("ok") and cmd.split()[0].upper() in WAIT_WORDS:
        time.sleep(0.2)
        while link.busy():        # wait until the move has finished
            time.sleep(0.2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--http", nargs="?", const="192.168.4.1", metavar="HOST", help="Wi-Fi (default 192.168.4.1)")
    g.add_argument("--serial", metavar="PORT", help="USB serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--file", help="text file with one command per line")
    ap.add_argument("--no-wait", action="store_true", help="do not wait for each move to finish")
    ap.add_argument("commands", nargs="*")
    a = ap.parse_args()

    link = SerialLink(a.serial) if a.serial else HttpLink(a.http or "192.168.4.1")
    cmds = list(a.commands)
    if a.file:
        with open(a.file, encoding="utf-8") as f:
            cmds += [ln.strip() for ln in f if ln.strip() and not ln.startswith("#")]
    if cmds:
        for c in cmds:
            run(link, c, not a.no_wait)
        return
    print("type commands (HELP, STATUS, ON, MOVE x y z ...), empty line to quit")
    for line in sys.stdin:
        if not line.strip():
            break
        run(link, line.strip(), not a.no_wait)


if __name__ == "__main__":
    main()
