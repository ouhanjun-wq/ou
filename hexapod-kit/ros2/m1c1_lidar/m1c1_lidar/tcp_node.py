"""ROS 2 node: M1C1-Mini lidar over TCP (XIAO ESP32S3 bridge) -> sensor_msgs/LaserScan on /scan.

    ros2 run m1c1_lidar tcp_node --ros-args -p host:=192.168.1.50

The XIAO forwards the lidar's serial bytes both ways on TCP port 3333. On connect this node sends
the start command, parses the packets (protocol.py) and publishes one LaserScan per revolution.
It reconnects on its own when Wi-Fi drops.
"""
import math
import socket
import threading
import time

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import LaserScan

from m1c1_lidar import protocol


class M1C1TcpNode(Node):
    def __init__(self):
        super().__init__("m1c1_lidar")
        self.host = self.declare_parameter("host", "192.168.4.1").value
        self.port = int(self.declare_parameter("port", 3333).value)
        self.frame_id = self.declare_parameter("frame_id", "laser").value
        self.bins = int(self.declare_parameter("bins", 360).value)
        self.offset_deg = float(self.declare_parameter("angle_offset_deg", 0.0).value)
        self.range_min = float(self.declare_parameter("range_min", 0.05).value)
        self.range_max = float(self.declare_parameter("range_max", 8.0).value)
        self.pub = self.create_publisher(LaserScan, "scan", 10)
        self.last_rev_time = None
        self.revs = 0
        self.create_timer(5.0, self.report)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def report(self):
        self.get_logger().info(f"{self.host}:{self.port}  {self.revs / 5.0:.1f} scans/s")
        self.revs = 0

    def run(self):
        while rclpy.ok():
            try:
                with socket.create_connection((self.host, self.port), timeout=5) as sock:
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    sock.settimeout(3)
                    self.get_logger().info(f"connected to {self.host}:{self.port}, starting the lidar")
                    sock.sendall(protocol.START_CMD)
                    parser = protocol.Parser()
                    while rclpy.ok():
                        data = sock.recv(4096)
                        if not data:
                            raise ConnectionError("bridge closed the connection")
                        for rev in parser.feed(data):
                            self.publish(rev)
            except (OSError, ConnectionError) as e:
                self.get_logger().warn(f"lidar bridge {self.host}:{self.port}: {e}; retrying in 2 s")
                time.sleep(2)

    def publish(self, rev):
        now = self.get_clock().now()
        t = time.monotonic()
        scan_time = (t - self.last_rev_time) if self.last_rev_time else 0.1
        self.last_rev_time = t
        msg = LaserScan()
        msg.header.stamp = now.to_msg()
        msg.header.frame_id = self.frame_id
        msg.angle_min = -math.pi
        msg.angle_increment = 2 * math.pi / self.bins
        msg.angle_max = msg.angle_min + msg.angle_increment * (self.bins - 1)
        msg.scan_time = float(min(scan_time, 1.0))
        msg.time_increment = msg.scan_time / self.bins
        msg.range_min = self.range_min
        msg.range_max = self.range_max
        msg.ranges = protocol.to_ranges(rev, self.bins, self.offset_deg,
                                        int(self.range_min * 1000), int(self.range_max * 1000))
        self.pub.publish(msg)
        self.revs += 1


def main():
    rclpy.init()
    node = M1C1TcpNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
