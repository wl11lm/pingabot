#!/usr/bin/env python3
import re
import serial
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import LaserScan
import math

class LidarSerialNode(Node):
    def __init__(self):
        super().__init__('lidar_serial_node')
        self.pub = self.create_publisher(LaserScan, '/scan', 10)
        self.ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
        self.readings = {}  # angulo -> distancia_m
        self.timer = self.create_timer(0.02, self.read_loop)

    def read_loop(self):
        line = self.ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            return
        if line.startswith('Scan completed'):
            self.publish_scan()
            return
        m = re.match(r'^(\d+)\s+([\d.]+)\s+([\d.]+)$', line)
        if not m:
            return
        angle = int(m.group(1))
        dist_mm = float(m.group(2))
        self.readings[angle] = dist_mm / 1000.0  # metros

    def publish_scan(self):
        angles = sorted(self.readings.keys())
        if not angles:
            return
        scan = LaserScan()
        scan.header.stamp = self.get_clock().now().to_msg()
        scan.header.frame_id = 'laser'
        scan.angle_min = 0.0
        scan.angle_max = math.radians(340.0)
        scan.angle_increment = math.radians(20.0)
        scan.range_min = 0.05
        scan.range_max = 8.0
        scan.ranges = [self.readings.get(a, 0.0) for a in angles]
        self.pub.publish(scan)
        self.readings.clear()

def main():
    rclpy.init()
    node = LidarSerialNode()
    rclpy.spin(node)

if __name__ == '__main__':
    main()
