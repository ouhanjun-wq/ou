# Claude — *Ignite every idea.* · 点亮每一个想法

一支 **30 秒、4K（3840×2160）/ 60 fps** 的动态图形宣传片，展示 Claude 的能力。
画面里的每一帧、配乐里的每一个音符，**全部由 Claude 用代码生成**：没有素材库，没有采样，没有剪辑软件。

▶ 成片：[`claude-promo.mp4`](claude-promo.mp4)（仓库里放的是 1080p60 网页版；4K60 母版由 `./build.sh` 生成到 `build/claude-promo-4k.mp4`，约 150 MB，不进 git）

![poster](poster.jpg)

## 分镜 · Storyboard

节拍是 120 BPM（一小节 = 2 s），所以每一个转场都落在强拍上，声画同步。

| 时间 | 段落 | 画面 | 声音 |
|---|---|---|---|
| 0 – 4 s | **Ignition** 引子 | 1500 颗粒子螺旋坍缩，文字被吸入中心的火花：*Every great idea begins with a spark.* / 每一个伟大的想法，都始于一个火花。 | 钢琴单音、星点铃声、噪声上升音 |
| 4 s | 💥 | 火花绽放，冲击波 + 闪白 + 镜头震动 | 次低频轰鸣 + 镲片 + Dm 和弦 |
| 4 – 8 s | **01 Reason** 推理 | 240 个节点的思维网络从火花生长出来，每一拍一道信号波沿树传播；左侧"思考链"逐条打勾 | 四拍底鼓、跟随信号的高音 ping |
| 8 – 12 s | **02 Code** 编程 | 3D 透视的编辑器里代码以超人速度敲出（带语法高亮），终端跑测试 `248 passed` → 部署 | 16 分音符琶音、拍手、踩镲；测试通过时的"叮" |
| 12 – 16 s | **03 Mathematics** 数学 | KaTeX 排版的公式星座（欧拉恒等式、高斯积分、巴塞尔问题、薛定谔方程、麦克斯韦方程、旋转矩阵…）；本轮圆傅里叶级数在节拍上逐级增加项数 $N=1\to34$，逼近方波 | 玻璃质感 FM 铃声琶音 |
| 16 – 20 s | **04 Language** 语言 | 100 种文字的问候组成旋转的 3D 词球；中心句子每拍换一种语言：*Ideas know no borders.* → 中文 → 日本語 → 한국어 → Español → العربية → हिन्दी | 人声合唱 pad、每次换语言的高音 |
| 20 – 24 s | **05 Create** 创造 | 760 条流场笔触像画笔一样从左到右铺满画面：以语言为笔，以思想为墨。 | 钢琴旋律 |
| 24 – 26 s | **06 Agents** 智能体 | 120 个并行任务卡片同时运行、逐个完成：*One mind. A thousand hands.* / 一个心智，千手并行。随后全部坍缩回一点 | 加速军鼓滚奏 + 上升音，落点前 80 ms 静音 |
| 26 s | 💥 | 最终爆发 | A → D 大调终止式（属到主，"皮卡第三度"） |
| 26 – 30 s | **Finale** | 火花滑入 **Claude** 字标：*Ignite every idea.* / 点亮每一个想法。 | 长混响尾音淡出 |

## 怎么做的 · How it's made

- **`film.html` + `film.js`** — 整部片子是一个网页。`window.renderAt(t)` 是时间的**纯函数**：给定 t 就画出那一帧（Canvas 2D 负责粒子、网络、波形、流场；DOM 负责排版和 KaTeX 公式）。所有随机数都用固定种子，所以任意顺序、任意进程渲染出的帧都完全一致。直接用浏览器打开就是一个带进度条的实时预览（空格播放/暂停）。
- **`tools/render.mjs`** — Playwright 驱动无头 Chromium，4 个进程并行逐帧截图，PNG 直接管道给 ffmpeg 编码。
- **`tools/score.py`** — 用 numpy / scipy 从零合成配乐：PolyBLEP 锯齿波 supersaw pad、加法合成的拨弦与钢琴、FM 铃声、共振峰合唱、合成鼓组、噪声上升音、卷积混响、底鼓侧链压缩。
- **`tools/fetch_fonts.py`** — 下载 Newsreader / Inter / JetBrains Mono / Noto Serif SC 以及十几种 Noto 文字字体。

## 重新生成 · Rebuild

```bash
./build.sh            # 字体 + KaTeX → 配乐 → 1800 帧 4K → build/claude-promo-4k.mp4 + claude-promo.mp4
```

需要 Python 3、Node 18+ 和 Playwright（带 Chromium）。4K 渲染在 4 核机器上约 12 分钟：页面以 1920×1080 布局、`deviceScaleFactor = 2` 截图，所以文字、公式和画布都是原生 3840×2160，不是放大。渲染按 1 秒分块写盘，中断后重跑会从断点继续。

只想看某几帧：

```bash
node tools/render.mjs --stills 5.2,13.6,29              # -> build/stills/*.png (1080p)
node tools/render.mjs --stills 29 --scale 2 --jpeg      # 4K
```
