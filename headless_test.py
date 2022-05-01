import subprocess
from PIL import Image


if __name__ == "__main__":
    width = 200
    height = 200

    proc = subprocess.run(f"./tangerine.exe -s {width} {height}", capture_output=True)
    img_header = b"BEGIN RAW IMAGE"
    assert(proc.stdout.count(img_header) == 1)

    info_end = proc.stdout.index(img_header)
    img_start = info_end + len(img_header)

    print(proc.stdout[:info_end].decode("utf-8"))
    raw = proc.stdout[img_start:]

    fnord = Image.frombuffer("RGB", (width, height), raw)
    fnord.save("test_render.png")
