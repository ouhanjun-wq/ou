from glob import glob

from setuptools import setup

package_name = "m1c1_lidar"

setup(
    name=package_name,
    version="0.1.0",
    packages=[package_name],
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/launch", glob("launch/*.py")),
        ("share/" + package_name + "/config", glob("config/*")),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="hexapod-kit",
    maintainer_email="hexapod-kit@example.com",
    description="M1C1-Mini lidar over TCP (XIAO ESP32S3 bridge) -> /scan, plus a slam_toolbox mapping launch",
    license="MIT",
    entry_points={"console_scripts": ["tcp_node = m1c1_lidar.tcp_node:main",
                                      "fake_bridge = m1c1_lidar.fake_bridge:main"]},
)
