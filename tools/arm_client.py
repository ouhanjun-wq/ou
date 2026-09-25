#!/usr/bin/env python3
"""Send text commands to the robot arm (Arduino Uno R3) from a PC or an AI agent.

The Uno is connected with its USB cable; this script talks to its serial port at 9600 baud
(needs `pip install pyserial`). Close the Arduino IDE's Serial Monitor first.

Examples
  python3 tools/arm_client.py COM5 STATUS
  python3 tools/arm_client.py /dev/ttyACM0 ON "MOVE 220 0 40 -60" CLOSE "UP 50" HOME
  python3 tools/arm_client.py COM5 --file demo.txt        # one command per line
  python3 tools/arm_client.py COM5                        # interactive prompt

An AI agent only needs one tool: "send_arm_command(text) -> reply". The command list is in
firmware/README.md; units are mm and degrees, gripper 0 = open .. 100 = closed.
"""
import argparse
import sys
import time

WAIT_WORDS = ("MOVE", "J", "HOME", "UP", "DOWN", "LEFT", "RIGHT", "FORWARD", "BACK",
              "GRIP", "OPEN", "CLOSE", "OFF", "PLAY")


class Arm:
    def __init__(self, port):
        import serial  # pyserial
        self.s = serial.Serial(port, 9600, timeout=0.3)
        time.sleep(2.0)            # the Uno resets when the port opens
        self.s.reset_input_buffer()

    def send(self, cmd):
        """Sends one command and returns the reply lines (up to the 'ok' / 'error' line)."""
        self.s.write((cmd + "\n").encode())
        lines, end = [], time.time() + 1.5
        while time.time() < end:
            line = self.s.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            lines.append(line)
            if line.startswith(("ok", "error")):
                break
            if cmd.upper().startswith(("STATUS", "HELP", "SHOW")):
                end = time.time() + 0.3   # multi-line replies: stop shortly after the last line
        return "\n".join(lines)

    def busy(self):
        return " IDLE " not in " " + self.send("STATUS").split("\n")[0] + " "


def run(arm, cmd, wait):
    reply = arm.send(cmd)
    print(f"> {cmd}\n{reply}")
    if wait and reply.startswith("ok") and cmd.split()[0].upper() in WAIT_WORDS:
        time.sleep(0.3)
        while arm.busy():         # wait until the move has finished
            time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port of the Uno, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--file", help="text file with one command per line")
    ap.add_argument("--no-wait", action="store_true", help="do not wait for each move to finish")
    ap.add_argument("commands", nargs="*")
    a = ap.parse_args()

    arm = Arm(a.port)
    cmds = list(a.commands)
    if a.file:
        with open(a.file, encoding="utf-8") as f:
            cmds += [ln.strip() for ln in f if ln.strip() and not ln.startswith("#")]
    if cmds:
        for c in cmds:
            run(arm, c, not a.no_wait)
        return
    print("type commands (HELP, STATUS, ON, MOVE x y z ...), empty line to quit")
    for line in sys.stdin:
        if not line.strip():
            break
        run(arm, line.strip(), not a.no_wait)


if __name__ == "__main__":
    main()
