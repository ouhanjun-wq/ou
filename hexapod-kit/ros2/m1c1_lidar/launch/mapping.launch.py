"""Lidar + slam_toolbox + RViz in one go:

    ros2 launch m1c1_lidar mapping.launch.py host:=192.168.1.50

host = the IP the XIAO prints on its USB serial (or 192.168.4.1 when it runs its own hotspot).
The hexapod does not publish odometry, so odom -> base_link is a fixed transform and slam_toolbox
finds the motion by scan matching alone: walk slowly (speed level 1) while mapping.
"""
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    share = get_package_share_directory("m1c1_lidar")
    host = LaunchConfiguration("host")
    offset = LaunchConfiguration("angle_offset_deg")
    lidar_z = LaunchConfiguration("lidar_z")
    rviz = LaunchConfiguration("rviz")
    return LaunchDescription([
        DeclareLaunchArgument("host", default_value="192.168.4.1", description="XIAO lidar bridge IP"),
        DeclareLaunchArgument("angle_offset_deg", default_value="0.0",
                              description="turn the scan so that 0 deg = robot front"),
        DeclareLaunchArgument("lidar_z", default_value="0.16", description="lidar height above the floor (m)"),
        DeclareLaunchArgument("rviz", default_value="true"),
        Node(package="m1c1_lidar", executable="tcp_node", name="m1c1_lidar", output="screen",
             parameters=[{"host": host, "angle_offset_deg": offset}]),
        Node(package="tf2_ros", executable="static_transform_publisher", name="base_to_laser",
             arguments=["--x", "0", "--y", "0", "--z", lidar_z, "--frame-id", "base_link", "--child-frame-id", "laser"]),
        Node(package="tf2_ros", executable="static_transform_publisher", name="odom_to_base",
             arguments=["--frame-id", "odom", "--child-frame-id", "base_link"]),
        Node(package="slam_toolbox", executable="async_slam_toolbox_node", name="slam_toolbox", output="screen",
             parameters=[os.path.join(share, "config", "slam.yaml")]),
        Node(package="rviz2", executable="rviz2", name="rviz2", condition=IfCondition(rviz),
             arguments=["-d", os.path.join(share, "config", "mapping.rviz")]),
    ])
