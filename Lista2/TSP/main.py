import matplotlib.pyplot as plt
from PIL import Image
import os
import io
from datetime import datetime
p = r"/Users/oskarmulcan/Desktop/KZW/sa/results"

def fig2img(fig):
    buf = io.BytesIO()
    fig.savefig(buf)
    buf.seek(0)
    img = Image.open(buf)
    return img

for e in os.scandir(p):
    if e.is_file():
        with open(e.path, "r") as f:
            filename = e.name.replace(".txt", "")
            data = f.read()
            list = [float(x) for x in data.split()]
            plt.title(filename)
            plt.plot(list)
            fig = plt.gcf()
            img = fig2img(fig)
            img.save(filename + ".png")
            plt.close()




