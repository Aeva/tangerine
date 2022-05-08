import base64
import subprocess
from PIL import Image


if __name__ == "__main__":
    width = 900
    height = 900
    max_iter = 1000

    with open("models/step-pyramid.rkt", "rb") as infile:
        model_source = infile.read()

    proc = subprocess.run(f"./tangerine.exe --cin --headless {width} {height} --iterations {max_iter}", capture_output=True, input=model_source)
    img_header = b"BEGIN RAW IMAGE"
    if proc.stdout.count(img_header) == 0:
        print(proc.stdout.decode("utf-8"))
    assert(proc.stdout.count(img_header) == 1)

    info_end = proc.stdout.index(img_header)
    img_start = info_end + len(img_header)

    print(proc.stdout[:info_end].decode("utf-8"))
    image_bytes = base64.b64decode(proc.stdout[img_start:], validate=True)

    raw_mode = "RGB"
    stride = 0
    orientation = -1
    fnord = Image.frombytes("RGB", (width, height), image_bytes, "raw", raw_mode, stride, orientation)
    fnord.save("test_render.png")
