"""Pack a transparent caption into the DLL's bounded RGBA resource format."""
import argparse
from pathlib import Path
import struct
from PIL import Image

p=argparse.ArgumentParser()
p.add_argument("source",type=Path)
p.add_argument("output",type=Path)
a=p.parse_args()
image=Image.open(a.source).convert("RGBA")
box=image.getchannel("A").point(lambda value:255 if value>=16 else 0).getbbox()
if not box:raise ValueError("Caption is empty")
image=image.crop(box)
scale=min(151/image.width,30/image.height)
image=image.resize((round(image.width*scale),round(image.height*scale)),Image.Resampling.LANCZOS)
canvas=Image.new("RGBA",(155,34))
canvas.alpha_composite(image,((155-image.width)//2,(34-image.height)//2))
a.output.write_bytes(struct.pack("<4sHH",b"N50L",155,34)+canvas.tobytes())
print(f"Packed transparent 155x34 caption: {a.output}")
