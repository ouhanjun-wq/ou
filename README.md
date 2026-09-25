# 仿生蝴蝶 (Bionic Butterfly)

基于 ESP32-S3（Arduino）的双舵机扑翼仿生蝴蝶：盖世小鸡 G7 Pro 手柄操控、陀螺仪增稳、扑翼同步降噪、气压定高、GPS 返航、Type-C 充电，并预留语音 / AI 文本指令接口。

![系统总览](docs/img/fig0-overview.svg)

| 从这里开始 | 内容 |
|---|---|
| 📋 [`docs/build-plan.md`](docs/build-plan.md) | **总计划**：材料清单、分阶段步骤、验收标准、故障排查 |
| 🔌 [`docs/assembly-guide.md`](docs/assembly-guide.md) | **组装指南**：接线图、焊接方法、每一步的注意事项和检查点 |
| 🔎 [`docs/bionic-butterfly-research.md`](docs/bionic-butterfly-research.md) | GitHub 开源项目调研与选型 |
| 🧭 [`docs/gyro-stabilization-and-noise-reduction.md`](docs/gyro-stabilization-and-noise-reduction.md) | 增稳与降噪设计原理 |
| 🔧 [`firmware/README.md`](firmware/README.md) | 固件：接线、烧录、命令行、参数 |
| 🧩 [`cad/README.md`](cad/README.md) | 3D 打印件：舵机座、左右翼根座（OpenSCAD 参数化模型 + STL） |
| 📈 [`tools/gyro_fft.py`](tools/gyro_fft.py) | 陀螺仪频谱采集与绘图 |
