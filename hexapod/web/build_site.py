#!/usr/bin/env python3
"""Build the single-page website (hexapod/web/index.html) from the hexapod docs.

    pip install markdown-it-py==3.0.0 mdit-py-plugins==0.4.2
    python3 hexapod/web/build_site.py

Every section of the page is generated from a Markdown file in this folder's
parent, so the website and the docs never drift apart: edit the .md files,
re-run this script, commit both.

The output is an Artifact page body (no <html>/<body> wrapper): it carries its
own <title>, <style> and <script>. SVG diagrams are inlined, PNG previews are
embedded as data: URIs, so the file is fully self-contained.
"""
import base64
import html
import os
import re
import sys
import unicodedata
from urllib.parse import unquote

from markdown_it import MarkdownIt
from mdit_py_plugins.dollarmath import dollarmath_plugin
from mdit_py_plugins.tasklists import tasklists_plugin

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "index.html")
REPO = "https://github.com/ouhanjun-wq/ou"
BRANCH = "claude/ros-esp32-s3-project-ypzx2b"

# (section id, source file relative to the repo root, navigation label)
DOCS = [
    ("home", "hexapod/README.md", "总览"),
    ("start", "hexapod/docs/beginner-plan.md", "新手计划"),
    ("bom", "hexapod/docs/bom.md", "详细材料清单"),
    ("plan", "hexapod/docs/build-plan.md", "总计划与步骤"),
    ("wire", "hexapod/docs/wiring-guide.md", "接线与装配"),
    ("soft", "hexapod/docs/software-setup.md", "软件与烧录"),
    ("phone", "hexapod/docs/phone-control.md", "手机控制"),
    ("cad", "hexapod/cad/README.md", "3D 打印件"),
    ("links", "hexapod/docs/links.md", "网站汇总"),
    ("research", "hexapod/docs/project-research.md", "项目调研"),
]
DOC_BY_PATH = {path: key for key, path, _ in DOCS}


def gh_slug(text):
    """GitHub's heading anchor: lowercase, drop punctuation, spaces -> '-'."""
    out = []
    for ch in text.strip().lower():
        cat = unicodedata.category(ch)
        if ch in " -" or cat[0] in "LNM" or cat == "Pc":
            out.append(ch)
    return "".join(out).replace(" ", "-")


def inline_text(token):
    return "".join(c.content for c in (token.children or []) if c.type in ("text", "code_inline"))


def math_renderer(content, opts):
    body = html.escape(content)
    if opts.get("display_mode"):
        return f'<div class="math-block">\\[{body}\\]</div>\n'
    return f'<span class="math">\\({body}\\)</span>'


def new_md():
    md = MarkdownIt("commonmark", {"html": True})
    md.enable("table")
    md.use(dollarmath_plugin, allow_space=True, allow_digits=True, double_inline=True, renderer=math_renderer)
    md.use(tasklists_plugin, enabled=True)
    return md


def github_url(repo_path):
    kind = "tree" if os.path.isdir(os.path.join(ROOT, repo_path)) else "blob"
    return f"{REPO}/{kind}/{BRANCH}/{repo_path}"


class Site:
    def __init__(self):
        self.md = new_md()
        self.slugs = {}        # doc key -> {github slug: page id}
        self.headings = {}     # doc key -> [(level, id, text)]
        self.titles = {}       # doc key -> H1 text
        self.tokens = {}
        self.errors = []

    # -- pass 1: parse and give every heading a stable ASCII id -------------
    def parse(self):
        for key, path, _ in DOCS:
            src = open(os.path.join(ROOT, path), encoding="utf-8").read()
            toks = self.md.parse(src)
            slugs, heads, n = {}, [], 0
            for i, t in enumerate(toks):
                if t.type != "heading_open":
                    continue
                text = inline_text(toks[i + 1])
                if t.tag == "h1" and key not in self.titles:
                    self.titles[key] = text
                    t.attrSet("data-drop", "1")
                    continue
                n += 1
                hid = f"{key}-{n}"
                slugs[gh_slug(text)] = hid
                t.attrSet("id", hid)
                level = int(t.tag[1])
                heads.append((level, hid, text))
            self.slugs[key], self.headings[key], self.tokens[key] = slugs, heads, toks

    # -- link / image rewriting ---------------------------------------------
    def resolve(self, href, doc_path):
        if re.match(r"^[a-z]+:", href):
            return href, True
        path, _, anchor = unquote(href).partition("#")
        target = os.path.normpath(os.path.join(os.path.dirname(doc_path), path)) if path else doc_path
        if target in DOC_BY_PATH:
            key = DOC_BY_PATH[target]
            if not anchor:
                return "#" + key, False
            hid = self.slugs[key].get(anchor)
            if hid is None:
                self.errors.append(f"{doc_path}: unknown anchor {href}")
                return "#" + key, False
            return "#" + hid, False
        if not os.path.exists(os.path.join(ROOT, target)):
            self.errors.append(f"{doc_path}: missing file {href}")
        return github_url(target), True

    def image_html(self, src, alt, doc_path):
        path = os.path.normpath(os.path.join(os.path.dirname(doc_path), src))
        full = os.path.join(ROOT, path)
        cap = f"<span class=\"fig-cap\">{html.escape(alt)}</span>" if alt else ""
        if path.endswith(".svg"):
            svg = open(full, encoding="utf-8").read()
            svg = re.sub(r'<svg([^>]*?) width="\d+" height="\d+"', r'<svg\1', svg, count=1)
            svg = svg.replace("<svg ", f'<svg role="img" aria-label="{html.escape(alt)}" ', 1)
            return f'<span class="fig fig-svg">{svg}{cap}</span>'
        data = base64.b64encode(open(full, "rb").read()).decode()
        mime = "image/png" if path.endswith(".png") else "image/jpeg"
        return (f'<span class="fig"><img src="data:{mime};base64,{data}" alt="{html.escape(alt)}" '
                f'loading="lazy">{cap}</span>')

    # -- pass 2: render --------------------------------------------------------
    def render_doc(self, key, path):
        toks = self.tokens[key]
        out, skip = [], False
        for t in toks:
            if t.type == "heading_open" and t.attrGet("data-drop"):
                skip = True
                continue
            if skip:
                if t.type == "heading_close":
                    skip = False
                continue
            if t.type in ("heading_open", "heading_close"):
                t.tag = f"h{min(int(t.tag[1]) + 1, 6)}"          # doc ## -> page <h3>
            for c in t.children or []:
                if c.type == "link_open":
                    href, external = self.resolve(c.attrGet("href"), path)
                    c.attrSet("href", href)
                    if external:
                        c.attrSet("target", "_blank")
                        c.attrSet("rel", "noopener")
                elif c.type == "image":
                    c.type, c.content = "html_inline", self.image_html(
                        c.attrGet("src"), inline_text(c) or c.content, path)
                    c.children = None
            out.append(t)
        body = self.md.renderer.render(out, self.md.options, {})
        body = body.replace("<table>", '<div class="tbl"><table>').replace("</table>", "</table></div>")
        body = re.sub(r'<pre><code class="language-mermaid">(.*?)</code></pre>',
                      r'<pre class="mermaid">\1</pre>', body, flags=re.S)
        n = iter(range(1, 10000))
        body = re.sub(r'<input class="task-list-item-checkbox"',
                      lambda m: f'<input id="task-{key}-{next(n)}" class="task-list-item-checkbox"', body)
        # a paragraph that only holds a figure becomes a plain block
        body = re.sub(r'<p>(<span class="fig[^"]*">.*?</span>)</p>', r'<div class="fig-row">\1</div>', body, flags=re.S)
        return body

    def build(self):
        self.parse()
        sections, nav = [], []
        for key, path, label in DOCS:
            body = self.render_doc(key, path)
            sub = "".join(
                f'<li><a href="#{hid}">{html.escape(text)}</a></li>'
                for level, hid, text in self.headings[key] if level == 2)
            nav.append(f'<li><a class="nav-sec" href="#{key}">{html.escape(label)}</a>'
                       f'<ol class="nav-sub">{sub}</ol></li>')
            sections.append(
                f'<section class="doc" id="{key}" aria-labelledby="{key}-title">\n'
                f'<header class="doc-head"><p class="eyebrow"><a href="{github_url(path)}" target="_blank" '
                f'rel="noopener">{html.escape(path)}</a></p>'
                f'<h2 class="sec-title" id="{key}-title">{html.escape(label)}</h2>'
                f'<p class="doc-sub">{html.escape(self.titles.get(key, ""))}</p></header>\n'
                f'{body}</section>')
        page = TEMPLATE.replace("%NAV%", "\n".join(nav)).replace("%SECTIONS%", "\n".join(sections))
        page = page.replace("%HERO_IMG%", self.image_html("cad/img/assembly.png", "整机装配预览（站立膝角 60°）",
                                                          "hexapod/README.md"))
        ids = set(re.findall(r'\sid="([^"]+)"', page))
        for ref in set(re.findall(r'href="#([^"]+)"', page)):
            if ref not in ids:
                self.errors.append(f"dangling in-page link #{ref}")
        if self.errors:
            sys.exit("\n".join(self.errors))
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(page)
        print(f"wrote {os.path.relpath(OUT, ROOT)} ({len(page) // 1024} KB)")


TEMPLATE = r"""<title>XIAO 六足机器人</title>
<meta name="description" content="XIAO ESP32S3 + micro-ROS + ROS 2 Jazzy 六足机器人：材料清单、接线、装配、软件安装、3D 打印件">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700&family=JetBrains+Mono:wght@400;600&family=Noto+Sans+SC:wght@400;500;700&display=swap">
<style>
:root{
  --bg:#F4F6F1; --surface:#FFFFFF; --surface-2:#E8EEE6; --ink:#16201B; --muted:#5A695F;
  --line:#D2DBD1; --accent:#1F6B4E; --accent-ink:#FFFFFF; --accent-soft:#DCEBE2;
  --amber:#A96608; --blue:#2360A0; --code-bg:#17221C; --code-ink:#DCE7E0; --mark:#FFF0C9;
  --fig-bg:#FFFFFF; --shadow:0 1px 0 rgba(22,32,27,.04);
  --f-body:"Noto Sans SC","PingFang SC","Microsoft YaHei",system-ui,sans-serif;
  --f-disp:"Chakra Petch","Noto Sans SC","PingFang SC",system-ui,sans-serif;
  --f-mono:"JetBrains Mono",ui-monospace,"SFMono-Regular",Menlo,Consolas,monospace;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    color-scheme:dark;
    --bg:#0E1411; --surface:#151E19; --surface-2:#1C2721; --ink:#E3EBE5; --muted:#98A89E;
    --line:#2A3730; --accent:#62C197; --accent-ink:#0E1411; --accent-soft:#1F3A2D;
    --amber:#E3A24C; --blue:#86B6EC; --code-bg:#0A0F0C; --code-ink:#D3E0D8; --mark:#3B3321;
    --fig-bg:#F4F6F1;
  }
}
:root[data-theme="dark"]{
  color-scheme:dark;
  --bg:#0E1411; --surface:#151E19; --surface-2:#1C2721; --ink:#E3EBE5; --muted:#98A89E;
  --line:#2A3730; --accent:#62C197; --accent-ink:#0E1411; --accent-soft:#1F3A2D;
  --amber:#E3A24C; --blue:#86B6EC; --code-bg:#0A0F0C; --code-ink:#D3E0D8; --mark:#3B3321;
  --fig-bg:#F4F6F1;
}
*{box-sizing:border-box}
html{scroll-behavior:smooth;scroll-padding-top:calc(env(safe-area-inset-top,0px) + 64px)}
@media (prefers-reduced-motion: reduce){html{scroll-behavior:auto}}
body{background:var(--bg);color:var(--ink);font:15.5px/1.75 var(--f-body);-webkit-font-smoothing:antialiased}
a{color:var(--accent);text-underline-offset:3px;text-decoration-thickness:1px}
a:hover{text-decoration-thickness:2px}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px;border-radius:3px}
.shell{display:grid;grid-template-columns:260px minmax(0,1fr);gap:0 48px;max-width:1240px;margin:0 auto;padding-inline:24px}

/* ---- navigation ---- */
.nav{position:sticky;top:env(safe-area-inset-top,0px);align-self:start;max-height:100vh;overflow-y:auto;padding-block:28px 40px;font-size:13.5px}
.nav .brand{font:600 15px/1.3 var(--f-disp);letter-spacing:.02em;margin:0 0 18px;display:block;color:var(--ink);text-decoration:none}
.nav .brand small{display:block;font:400 12px/1.5 var(--f-mono);color:var(--muted);letter-spacing:0}
.nav ol{list-style:none;margin:0;padding:0}
.nav > ol{display:grid;gap:2px}
.nav-sec{display:block;padding:6px 10px;border-radius:6px;color:var(--ink);text-decoration:none;font-weight:500}
.nav-sec:hover{background:var(--surface-2)}
.nav-sec.on{background:var(--accent);color:var(--accent-ink)}
.nav-sub{margin:2px 0 8px 10px !important;border-left:1px solid var(--line);display:none}
.nav li.open .nav-sub{display:block}
.nav-sub a{display:block;padding:3px 10px;color:var(--muted);text-decoration:none;line-height:1.45}
.nav-sub a:hover{color:var(--ink)}
.nav-sub a.on{color:var(--accent);font-weight:500}
.progress-mini{margin-top:18px;padding:12px;border:1px solid var(--line);border-radius:8px;background:var(--surface);font-size:12.5px;color:var(--muted);display:grid;gap:8px}
.bar{height:6px;border-radius:3px;background:var(--surface-2);overflow:hidden}
.bar i{display:block;height:100%;width:0;background:var(--accent);transition:width .3s}

/* ---- hero ---- */
main{min-width:0;padding-block:28px 96px}
.hero{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(0,1fr);gap:32px;align-items:center;padding-block:12px 40px;border-bottom:1px solid var(--line)}
.hero h1{font:700 clamp(34px,5vw,54px)/1.08 var(--f-disp);letter-spacing:-.01em;margin:0 0 14px;text-wrap:balance}
.hero h1 span{color:var(--accent)}
.hero p.lede{font-size:16.5px;color:var(--muted);margin:0 0 20px;max-width:34em}
.pins{display:flex;flex-wrap:wrap;gap:4px;margin:0 0 20px;font:600 11.5px/1 var(--f-mono)}
.pins span{padding:5px 7px;border:1px solid var(--line);border-radius:4px;background:var(--surface);color:var(--muted)}
.pins span b{color:var(--ink);font-weight:600}
.facts{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:0}
.facts div{padding:10px 12px;border-left:3px solid var(--accent);background:var(--surface)}
.facts dt{font-size:12px;color:var(--muted)}
.facts dd{margin:0;font:600 17px/1.3 var(--f-disp);font-variant-numeric:tabular-nums}
.hero .fig{margin:0}
.hero .fig img{border-radius:10px;border:1px solid var(--line);background:#F7F7F7}

/* ---- document sections ---- */
.doc{padding-block:48px 8px;border-bottom:1px solid var(--line);max-width:900px}
.doc-head{margin-bottom:8px}
.eyebrow{margin:0;font:400 12px/1.4 var(--f-mono);letter-spacing:.02em}
.eyebrow a{color:var(--muted);text-decoration:none}
.eyebrow a:hover{color:var(--accent)}
.sec-title{font:700 clamp(28px,3.4vw,38px)/1.15 var(--f-disp);margin:6px 0 6px;text-wrap:balance}
.doc-sub{margin:0 0 10px;color:var(--muted)}
.doc h3{font:600 23px/1.3 var(--f-disp);margin:44px 0 12px;padding-top:14px;border-top:2px solid var(--ink);text-wrap:balance}
.doc h4{font:600 18px/1.4 var(--f-disp);margin:30px 0 8px}
.doc h5{font-size:16px;margin:22px 0 6px}
.doc p,.doc li{max-width:68ch}
.doc ul,.doc ol{padding-left:1.4em}
.doc li{margin:4px 0}
.doc li > ul,.doc li > ol{margin:4px 0}
.doc blockquote{margin:18px 0;padding:12px 16px;background:var(--surface);border:1px solid var(--line);border-left:4px solid var(--amber);border-radius:0 8px 8px 0}
.doc blockquote p{margin:6px 0}
.doc hr{border:0;border-top:1px dashed var(--line);margin:36px 0}
.doc strong{font-weight:700}
.doc code{font:13px/1.5 var(--f-mono);background:var(--surface-2);padding:1px 5px;border-radius:4px;overflow-wrap:anywhere}
.doc pre{position:relative;background:var(--code-bg);color:var(--code-ink);border-radius:8px;padding:14px 16px;overflow-x:auto;font:13px/1.6 var(--f-mono)}
.doc pre code{background:none;padding:0;color:inherit;font:inherit;overflow-wrap:normal;white-space:pre}
.doc pre.mermaid{background:var(--surface);color:var(--ink);border:1px solid var(--line);text-align:center}
.copy{position:absolute;top:8px;right:8px;font:600 11.5px/1 var(--f-body);padding:6px 9px;border-radius:5px;border:1px solid #3A4A41;background:#22302A;color:#DCE7E0;cursor:pointer}
.copy:hover{background:#2C3D35}
.tbl{overflow-x:auto;margin:16px 0;border:1px solid var(--line);border-radius:8px;background:var(--surface)}
.tbl table{border-collapse:collapse;width:100%;font-size:14px;line-height:1.6}
.tbl th{position:sticky;top:0;background:var(--surface-2);text-align:left;font-weight:600;white-space:nowrap}
.tbl th,.tbl td{padding:8px 12px;border-bottom:1px solid var(--line);vertical-align:top}
.tbl tr:last-child td{border-bottom:0}
.tbl td:first-child{white-space:nowrap}
.tbl tr.got td{color:var(--muted)}
.tbl tr.got td:not(:first-child){text-decoration:line-through;text-decoration-color:var(--line)}
.fig-row{margin:18px 0}
.fig{display:block;max-width:100%}
.fig-svg{overflow-x:auto;background:var(--fig-bg);border:1px solid var(--line);border-radius:8px}
.fig-svg svg{display:block;min-width:640px;width:100%;height:auto}
.fig img{display:block;max-width:100%;height:auto;border-radius:8px}
.doc .fig img{max-height:460px;width:auto;margin-inline:auto;background:#F7F7F7}
.fig-cap{display:block;font-size:12.5px;color:var(--muted);padding:6px 10px}
.tbl .fig-svg svg{min-width:420px}
.math-block{overflow-x:auto;padding:6px 0;margin:10px 0}
.task-list-item{list-style:none;margin-left:-1.4em}
.task-list-item-checkbox{width:17px;height:17px;margin:0 8px 0 0;vertical-align:-3px;accent-color:var(--accent);cursor:pointer}
.task-list-item.done{color:var(--muted);text-decoration:line-through}

/* ---- live BOM tally ---- */
.tally{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin:18px 0;padding:14px;border:1px solid var(--accent);border-radius:10px;background:var(--accent-soft)}
.tally div{display:grid;gap:2px}
.tally span{font-size:12.5px;color:var(--muted)}
.tally b{font:700 20px/1.2 var(--f-disp);font-variant-numeric:tabular-nums}
.tally p{grid-column:1/-1;margin:0;font-size:13px;color:var(--muted)}
.tally button{justify-self:start;font:500 12.5px var(--f-body);padding:6px 10px;border-radius:6px;border:1px solid var(--line);background:var(--surface);color:var(--ink);cursor:pointer}
.buy{width:17px;height:17px;accent-color:var(--accent);cursor:pointer}
.footer{padding-block:32px;color:var(--muted);font-size:13px}

@media (max-width: 900px){
  .shell{grid-template-columns:minmax(0,1fr);padding-inline:16px}
  .nav{position:sticky;top:env(safe-area-inset-top,0px);z-index:5;max-height:none;overflow:visible;padding-block:8px;margin-inline:-16px;padding-inline:16px;background:var(--bg);border-bottom:1px solid var(--line)}
  .nav .brand,.nav .progress-mini,.nav-sub{display:none !important}
  .nav > ol{display:flex;gap:6px;overflow-x:auto;scrollbar-width:none}
  .nav-sec{white-space:nowrap;border:1px solid var(--line);padding:5px 10px}
  .hero{grid-template-columns:minmax(0,1fr)}
  html{scroll-padding-top:calc(env(safe-area-inset-top,0px) + 70px)}
}
</style>

<div class="shell">
<nav class="nav" aria-label="目录">
  <a class="brand" href="#top">XIAO 六足机器人<small>ESP32S3 · micro-ROS · ROS 2</small></a>
  <ol>
%NAV%
  </ol>
  <div class="progress-mini" aria-live="polite">
    <span id="prog-text">新手计划：0 / 0 步</span>
    <div class="bar"><i id="prog-bar"></i></div>
    <span id="buy-text">材料：还要花 —</span>
  </div>
</nav>

<main id="top">
<div class="hero">
  <div>
    <h1>XIAO <span>六足</span>机器人</h1>
    <p class="lede">XIAO ESP32S3 Plus 跑 micro-ROS，通过 Wi-Fi 连到电脑上的 ROS 2 Jazzy：12 个舵机三角步态行走，LD14P 雷达建图，手机浏览器遥控。材料、接线、装配、软件，全部在这一页。纯新手从 <a href="#start">新手计划</a> 开始，一步步打勾做到最后。</p>
    <div class="pins" aria-label="XIAO 引脚分配">
      <span><b>D0</b> OE</span><span><b>D1</b> INT</span><span><b>D2</b> LED</span><span><b>D3</b> 电池</span>
      <span><b>D4</b> SDA</span><span><b>D5</b> SCL</span><span><b>D6</b> 雷达 RX</span><span><b>D7</b> 雷达 TX</span>
      <span><b>D8–D10</b> I²S</span>
    </div>
    <dl class="facts">
      <div><dt>舵机</dt><dd>12 × MG90S</dd></div>
      <div><dt>电脑端</dt><dd>ROS 2 Jazzy</dd></div>
      <div><dt>电池</dt><dd>3S 11.1 V</dd></div>
      <div><dt>遥控</dt><dd>手机 Wi-Fi</dd></div>
    </dl>
  </div>
  %HERO_IMG%
</div>

%SECTIONS%

<p class="footer">由 <code>hexapod/web/build_site.py</code> 从仓库里的 Markdown 文档生成。软件来自开源项目 <a href="https://github.com/SeekerRobot/seeker-robot" target="_blank" rel="noopener">SeekerRobot/seeker-robot</a>（Apache-2.0）。勾选状态只保存在你自己的浏览器里。</p>
</main>
</div>

<script>
window.MathJax = { tex: { inlineMath: [["\\(", "\\)"]], displayMath: [["\\[", "\\]"]] }, svg: { fontCache: "global" } };
</script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/mathjax/3.2.2/es5/tex-svg.js" async></script>
<script>
(function () {
  var KEY = "hexapod-web-v1";
  var state = {};
  try { state = JSON.parse(localStorage.getItem(KEY) || "{}") || {}; } catch (e) { state = {}; }
  function save() { try { localStorage.setItem(KEY, JSON.stringify(state)); } catch (e) {} }

  /* step checklists */
  var tasks = Array.prototype.slice.call(document.querySelectorAll(".task-list-item-checkbox"));
  var planTasks = Array.prototype.slice.call(document.querySelectorAll("#start .task-list-item-checkbox"));
  if (!planTasks.length) planTasks = tasks;
  function taskUpdate() {
    var done = 0;
    tasks.forEach(function (t) { t.parentElement.classList.toggle("done", t.checked); });
    planTasks.forEach(function (t) { if (t.checked) done++; });
    document.getElementById("prog-text").textContent = "新手计划：" + done + " / " + planTasks.length + " 步";
    document.getElementById("prog-bar").style.width = (planTasks.length ? 100 * done / planTasks.length : 0) + "%";
  }
  tasks.forEach(function (t) {
    t.checked = !!state[t.id];
    t.setAttribute("aria-label", t.parentElement.textContent.trim());
    t.addEventListener("change", function () { state[t.id] = t.checked; save(); taskUpdate(); });
  });
  taskUpdate();

  /* bill of materials: an "已买" column and a live remaining-cost tally */
  var rows = [];
  function parseRow(tr, head) {
    var cells = tr.children, txt = tr.textContent;
    var price = cells[head.price] ? cells[head.price].textContent : "";
    var m = price.match(/¥\s*(\d+)(?:\s*[–-]\s*(\d+))?/);
    if (!m) return null;
    var lo = +m[1], hi = +(m[2] || m[1]), mult = 1;
    if (/\/\s*个/.test(price) && cells[head.qty]) {
      var q = cells[head.qty].textContent.match(/\d+/g) || ["1"];
      mult = q.reduce(function (a, b) { return a + +b; }, 0);
    }
    return { lo: lo * mult, hi: hi * mult, optional: /可选/.test(txt), owned: /已有/.test(price) };
  }
  document.querySelectorAll("#bom .tbl table").forEach(function (table) {
    var ths = Array.prototype.map.call(table.querySelectorAll("thead th"), function (th) { return th.textContent.trim(); });
    var head = { price: ths.indexOf("参考价"), qty: ths.indexOf("数量") };
    if (head.price < 0) return;
    var th = document.createElement("th"); th.textContent = "已买";
    table.querySelector("thead tr").insertBefore(th, table.querySelector("thead th"));
    table.querySelectorAll("tbody tr").forEach(function (tr) {
      var info = parseRow(tr, head);
      var td = document.createElement("td");
      if (info) {
        var id = "buy-" + tr.children[0].textContent.trim().replace(/[^A-Za-z0-9]/g, "");
        var box = document.createElement("input");
        box.type = "checkbox"; box.className = "buy"; box.id = id;
        box.setAttribute("aria-label", "已买 " + tr.children[1].textContent.trim());
        box.checked = id in state ? !!state[id] : info.owned;
        box.addEventListener("change", function () { state[id] = box.checked; save(); tally(); });
        td.appendChild(box);
        rows.push({ tr: tr, box: box, info: info });
      }
      tr.insertBefore(td, tr.firstChild);
    });
  });
  var tallyBox = null;
  if (rows.length) {
    tallyBox = document.createElement("div");
    tallyBox.className = "tally";
    tallyBox.setAttribute("aria-live", "polite");
    var first = rows[0].tr.closest(".tbl");
    first.parentNode.insertBefore(tallyBox, first);
  }
  function fmt(a, b) { return "¥" + a + (b !== a ? "–" + b : ""); }
  function tally() {
    if (!tallyBox) return;
    var need = [0, 0], opt = [0, 0], left = 0;
    rows.forEach(function (r) {
      r.tr.classList.toggle("got", r.box.checked);
      if (r.box.checked) return;
      var t = r.info.optional ? opt : need;
      t[0] += r.info.lo; t[1] += r.info.hi; if (!r.info.optional) left++;
    });
    tallyBox.innerHTML = "<div><span>必需材料还要花</span><b>" + fmt(need[0], need[1]) + "</b></div>" +
      "<div><span>可选材料</span><b>" + fmt(opt[0], opt[1]) + "</b></div>" +
      "<div><span>必需还差</span><b>" + left + " 项</b></div>" +
      "<p>在下面各表的“已买”列打勾，数字会跟着变。按表里的参考价估算，勾选只保存在你的浏览器里。</p>";
    var reset = document.createElement("button");
    reset.type = "button"; reset.textContent = "清空勾选";
    reset.addEventListener("click", function () {
      rows.forEach(function (r) { r.box.checked = r.info.owned; state[r.box.id] = r.box.checked; });
      save(); tally();
    });
    tallyBox.appendChild(reset);
    document.getElementById("buy-text").textContent = "材料：还要花 " + fmt(need[0], need[1]);
  }
  tally();

  /* copy buttons on code blocks */
  document.querySelectorAll(".doc pre > code").forEach(function (code) {
    var btn = document.createElement("button");
    btn.type = "button"; btn.className = "copy"; btn.textContent = "复制";
    btn.addEventListener("click", function () {
      var text = code.textContent;
      function selectIt() {
        var r = document.createRange(); r.selectNodeContents(code);
        var s = window.getSelection(); s.removeAllRanges(); s.addRange(r);
        btn.textContent = "已选中，按 Ctrl+C";
      }
      try {
        navigator.clipboard.writeText(text).then(function () { btn.textContent = "已复制"; }, selectIt);
      } catch (e) { selectIt(); }
      setTimeout(function () { btn.textContent = "复制"; }, 2000);
    });
    code.parentNode.appendChild(btn);
  });

  /* highlight the section being read */
  var navLinks = {};
  document.querySelectorAll(".nav a[href^='#']").forEach(function (a) { navLinks[a.getAttribute("href").slice(1)] = a; });
  var marks = document.querySelectorAll(".doc, .doc h3[id]");
  var current = {};
  function setOn(sec, sub) {
    document.querySelectorAll(".nav .on").forEach(function (a) { a.classList.remove("on"); });
    document.querySelectorAll(".nav li.open").forEach(function (li) { li.classList.remove("open"); });
    if (navLinks[sec]) { navLinks[sec].classList.add("on"); navLinks[sec].parentElement.classList.add("open"); }
    if (sub && navLinks[sub]) navLinks[sub].classList.add("on");
  }
  if ("IntersectionObserver" in window) {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (!e.isIntersecting) return;
        var el = e.target;
        if (el.tagName === "SECTION") { current.sec = el.id; current.sub = null; }
        else { current.sec = el.closest("section").id; current.sub = el.id; }
        setOn(current.sec, current.sub);
      });
    }, { rootMargin: "-10% 0px -80% 0px" });
    marks.forEach(function (m) { io.observe(m); });
  }
  setOn("home");
})();
</script>
"""

if __name__ == "__main__":
    Site().build()
