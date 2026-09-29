"""Download the Google Fonts used by the film into ../assets/fonts and write a local fonts.css."""
import os
import re
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "fonts")
UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/126.0 Safari/537.36")

FAMILIES = [
    "Newsreader:ital,opsz,wght@0,6..72,200..800;1,6..72,200..800",
    "Inter:wght@200..800",
    "JetBrains+Mono:wght@300..700",
    "Noto+Serif+SC:wght@300..900",
    "Noto+Sans+SC:wght@200..800",
    "Noto+Sans+JP:wght@300..700",
    "Noto+Sans+KR:wght@300..700",
    "Noto+Sans+Arabic:wght@300..700",
    "Noto+Sans+Hebrew:wght@300..700",
    "Noto+Sans+Devanagari:wght@300..700",
    "Noto+Sans+Thai:wght@300..700",
    "Noto+Sans+Bengali:wght@300..700",
    "Noto+Sans+Tamil:wght@300..700",
    "Noto+Sans+Georgian:wght@300..700",
    "Noto+Sans+Armenian:wght@300..700",
    "Noto+Sans+Ethiopic:wght@300..700",
    "Noto+Sans:wght@300..700",
]


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def main():
    os.makedirs(OUT, exist_ok=True)
    css_all = []
    n = 0
    for fam in FAMILIES:
        css = get(f"https://fonts.googleapis.com/css2?family={fam}&display=block").decode()
        def repl(m):
            nonlocal n
            url = m.group(1)
            name = f"f{n:04d}.woff2"
            n += 1
            path = os.path.join(OUT, name)
            if not os.path.exists(path):
                with open(path, "wb") as f:
                    f.write(get(url))
            return f"url(fonts/{name})"
        css_all.append(re.sub(r"url\((https://[^)]+)\)", repl, css))
        print(fam.split(":")[0], "ok")
    with open(os.path.join(OUT, "..", "fonts.css"), "w") as f:
        f.write("\n".join(css_all))
    print(n, "files")


if __name__ == "__main__":
    main()
