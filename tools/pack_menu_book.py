"""Deterministically pack the original transparent book sheet for the DLL resource.

Requires Pillow only when rebuilding the art, not to build or run the patch.
Cells retain a shared scale and bottom/spine anchor; no game art is bundled.
"""
import argparse
from array import array
import json
import math
import statistics
from pathlib import Path
import struct
from PIL import Image, ImageChops


def component_cells(sheet,columns,rows):
    """Extract complete sprites even when the generated atlas grid is uneven.

    Slicing at width/4 cut the taller open books and included a fragment from
    the following row. Registration must operate on complete connected books.
    """
    width,height=sheet.size
    mask=bytearray(sheet.getchannel("A").point(lambda value:255 if value>=128 else 0).tobytes())
    parts=[]
    for seed in range(len(mask)):
        if not mask[seed]:continue
        pending=[seed];mask[seed]=0
        left=right=seed%width;top=bottom=seed//width;count=0
        while pending:
            pixel=pending.pop();x=pixel%width;y=pixel//width;count+=1
            left=min(left,x);right=max(right,x);top=min(top,y);bottom=max(bottom,y)
            neighbours=[]
            if x:neighbours.append(pixel-1)
            if x+1<width:neighbours.append(pixel+1)
            if y:neighbours.append(pixel-width)
            if y+1<height:neighbours.append(pixel+width)
            for neighbour in neighbours:
                if mask[neighbour]:mask[neighbour]=0;pending.append(neighbour)
        if count>=64:parts.append((count,(left,top,right+1,bottom+1)))
    wanted=columns*rows
    if len(parts)<wanted:raise ValueError("Not enough separate complete book sprites")
    parts=sorted(parts,reverse=True)[:wanted]
    parts.sort(key=lambda part:part[1][1]+part[1][3])
    cells=[]
    for row in range(rows):
        group=sorted(parts[row*columns:(row+1)*columns],key=lambda part:part[1][0])
        for _,box in group:
            left,top,right,bottom=box
            cells.append(sheet.crop((max(0,left-2),max(0,top-2),
                                      min(width,right+2),min(height,bottom+2))))
    return cells


def spine_anchor(cell):
    """Register the rigid lower spine, not the changing page/cover bounds."""
    alpha=cell.getchannel("A")
    box=alpha.point(lambda value:255 if value>=128 else 0).getbbox()
    if not box:
        raise ValueError("Book frame has no opaque spine")
    left,top,right,bottom=box
    quarter=max(1,(right-left)//4)
    for y in range(bottom-1,top-1,-1):
        foot=[x for x in range(left,left+quarter) if alpha.getpixel((x,y))>=128]
        if foot:
            return sum(foot)/len(foot),float(y)
    raise ValueError("Book frame has no lower spine anchor")


def align_cells(cells,width,height):
    """Translation plus ONE shared scale; preserve all painted page motion."""
    if not cells or min(width,height)<=12:raise ValueError("Registration canvas is too small")
    anchors=[spine_anchor(cell) for cell in cells]
    # Union of the entire art relative to its stationary support point. A
    # growing cover is allowed to rise, never to push the whole book down.
    left=min(-anchor[0] for anchor in anchors)
    top=min(-anchor[1] for anchor in anchors)
    right=max(cell.width-anchor[0] for cell,anchor in zip(cells,anchors))
    bottom=max(cell.height-anchor[1] for cell,anchor in zip(cells,anchors))
    scale=min((width-12)/(right-left),(height-12)/(bottom-top))
    target=(6-left*scale,6-top*scale)
    frames=[]
    report=[]
    for cell,anchor in zip(cells,anchors):
        offset=(round(target[0]-anchor[0]*scale),round(target[1]-anchor[1]*scale))
        size=(max(1,math.floor(cell.width*scale)),max(1,math.floor(cell.height*scale)))
        resized=cell.resize(size,Image.Resampling.LANCZOS)
        if min(offset)<0 or offset[0]+size[0]>width or offset[1]+size[1]>height:
            raise ValueError("Registered book would be cropped")
        frame=Image.new("RGBA",(width,height))
        frame.alpha_composite(resized,offset)
        frames.append(frame)
        report.append({"source_anchor":anchor,"offset":offset,
                       "mapped_anchor":[offset[0]+anchor[0]*scale,
                                        offset[1]+anchor[1]*scale],
                       "packed_spine":spine_anchor(frame)})
    return frames,{"scale":scale,"target_anchor":target,"frames":report}


def base_landmarks(cell):
    """Three stationary structural points: spine head, foot and lower board."""
    alpha=cell.getchannel("A")
    box=alpha.point(lambda value:255 if value>=128 else 0).getbbox()
    if not box:raise ValueError("No opaque book structure")
    left,top,right,bottom=box
    xs=range(left,left+max(1,int((right-left)*.16)))
    ys=[y for y in range(top,bottom) if any(alpha.getpixel((x,y))>=128 for x in xs)]
    head_y,foot_y=min(ys),max(ys)
    head_x=[x for x in xs if alpha.getpixel((x,head_y))>=128]
    foot_x=[x for x in xs if alpha.getpixel((x,foot_y))>=128]
    band=range(bottom-max(4,(bottom-top)//12),bottom)
    edge=max(x for y in band for x in range(left,right) if alpha.getpixel((x,y))>=128)
    edge_y=[y for y in band if alpha.getpixel((edge,y))>=128]
    return [(sum(head_x)/len(head_x),float(head_y)),
            (sum(foot_x)/len(foot_x),float(foot_y)),
            (float(edge),sum(edge_y)/len(edge_y))]


def map_point(transform,point):
    a,b,c,d,e,f=transform;x,y=point
    return a*x+b*y+c,d*x+e*y+f


def affine_between(source,target):
    (ax,ay),(bx,by),(cx,cy)=source
    (ta,tb,tc)=target
    ux,uy=ax-bx,ay-by;vx,vy=cx-bx,cy-by
    det=ux*vy-uy*vx
    if abs(det)<1e-8:raise ValueError("Degenerate book structure")
    pux,puy=ta[0]-tb[0],ta[1]-tb[1]
    pvx,pvy=tc[0]-tb[0],tc[1]-tb[1]
    a=(pux*vy-pvx*uy)/det;b=(-pux*vx+pvx*ux)/det
    d=(puy*vy-pvy*uy)/det;e=(-puy*vx+pvy*ux)/det
    return a,b,tb[0]-a*bx-b*by,d,e,tb[1]-d*bx-e*by


def inverse_affine(transform):
    a,b,c,d,e,f=transform;det=a*e-b*d
    if abs(det)<1e-8:raise ValueError("Singular book registration")
    return e/det,-b/det,(b*f-e*c)/det,-d/det,a/det,(d*c-a*f)/det


def resample_registered(cell,transform,size,supersample=4):
    """Filter transformed texels, including alpha, before final minification."""
    if not 1<=supersample<=8:raise ValueError("Supersampling must be between one and eight")
    width,height=size
    inverse=inverse_affine(tuple(value*supersample for value in transform))
    bands=cell.split();alpha=bands[3].tobytes()
    filtered=[]
    for channel,band in enumerate(bands):
        # Float premultiplied channels must not be clipped independently at
        # intermediate cubic-filter overshoots, which creates bright fringes.
        floating=Image.new("F",cell.size)
        floating.putdata([float(value) if channel==3 else value*a/255.0
                          for value,a in zip(band.tobytes(),alpha)])
        high=floating.transform((width*supersample,height*supersample),
            Image.Transform.AFFINE,inverse,resample=Image.Resampling.BICUBIC)
        values=array("f");values.frombytes(high.resize(size,Image.Resampling.LANCZOS).tobytes())
        filtered.append(values)
    rgba=bytearray(width*height*4)
    for pixel,a in enumerate(filtered[3]):
        if a<=.1:continue
        for channel in range(3):
            rgba[pixel*4+channel]=max(0,min(255,round(filtered[channel][pixel]*255.0/a)))
        rgba[pixel*4+3]=max(0,min(255,round(a)))
    return Image.frombytes("RGBA",size,bytes(rgba))


def rigid_base_cells(cells,width,height,supersample=4):
    """Normalize the ENTIRE base, then describe one immutable body layer.

    Affine registration is only texture-preserving 2D stabilization of complete
    painted frames. It does not simulate the cover with a flat polygon.
    """
    if not cells or min(width,height)<=12:raise ValueError("Registration canvas is too small")
    points=[base_landmarks(cell) for cell in cells]
    relative=[[(x-frame[1][0],y-frame[1][1]) for x,y in frame] for frame in points]
    target=[tuple(statistics.median(frame[i][axis] for frame in relative) for axis in [0,1])
            for i in range(3)]
    transforms=[affine_between(frame,target) for frame in points]
    corners=[map_point(t,p) for cell,t in zip(cells,transforms)
             for p in [(0,0),(cell.width,0),(0,cell.height),(cell.width,cell.height)]]
    left=min(p[0] for p in corners);top=min(p[1] for p in corners)
    right=max(p[0] for p in corners);bottom=max(p[1] for p in corners)
    scale=min((width-12)/(right-left),(height-12)/(bottom-top))
    origin=(6-left*scale,6-top*scale)
    frames=[];report=[]
    for cell,transform,landmarks in zip(cells,transforms,points):
        a,b,c,d,e,f=transform
        final=(a*scale,b*scale,c*scale+origin[0],d*scale,e*scale,f*scale+origin[1])
        frames.append(resample_registered(cell,final,(width,height),supersample))
        report.append({"source_landmarks":landmarks,
                       "mapped_landmarks":[map_point(final,p) for p in landmarks]})
    reference=len(frames)//2
    head,foot,edge=target
    # Smooth only the seam between moving and fixed layers. The interior of
    # the spine/board remains exactly 255 and therefore fully stationary.
    union=frames[0].getchannel("A")
    for frame in frames[1:]:union=ImageChops.lighter(union,frame.getchannel("A"))
    high_mask=Image.new("L",(width*supersample,height*supersample))
    for y in range(height*supersample):
        for x in range(width*supersample):
            px,py=((x+.5)/supersample-origin[0])/scale,((y+.5)/supersample-origin[1])/scale
            hinge=head[0]+(foot[0]-head[0])*(py-head[1])/(foot[1]-head[1])
            board=foot[1]+(edge[1]-foot[1])*(px-foot[0])/(edge[0]-foot[0])
            spine=head[1]-8<=py<=foot[1]+26 and px<=hinge+10
            lower_board=py>=board-4
            if (spine or lower_board) and union.getpixel((x//supersample,y//supersample))>=8:
                high_mask.putpixel((x,y),255)
    smooth_mask=high_mask.resize((width,height),Image.Resampling.LANCZOS)
    mask=smooth_mask.point(lambda value:0 if value<=3 else 255 if value>=252 else value).tobytes()
    return frames,{"scale":scale,"target_base":target,"reference_frame":reference,
                   "static_pixels":sum(value==255 for value in mask),
                   "soft_edge_pixels":sum(0<value<255 for value in mask),
                   "supersample":supersample,"frames":report},mask


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--columns", type=int, default=2)
    parser.add_argument("--rows", type=int, default=2)
    parser.add_argument("--width", type=int, default=120)
    parser.add_argument("--height", type=int, default=120)
    parser.add_argument("--registration-report",type=Path)
    parser.add_argument("--components",action="store_true",
                        help="extract complete separate sprites instead of cutting a strict grid")
    parser.add_argument("--rigid-base",action="store_true",
                        help="stabilize all base landmarks and bake an immutable spine/board layer")
    parser.add_argument("--supersample",type=int,default=4)
    args = parser.parse_args()
    sheet = Image.open(args.source).convert("RGBA")
    cells = []
    if min(args.columns,args.rows,args.width,args.height)<1:
        raise ValueError("Positive grid/cell dimensions required")
    if args.components:
        cells=component_cells(sheet,args.columns,args.rows)
    else:
        for y in range(args.rows):
            for x in range(args.columns):
                cell = sheet.crop((x * sheet.width // args.columns, y * sheet.height // args.rows,
                                   (x + 1) * sheet.width // args.columns, (y + 1) * sheet.height // args.rows))
                box = cell.getchannel("A").point(lambda alpha: 255 if alpha>=16 else 0).getbbox()
                if not box:
                    raise ValueError("Empty book animation cell")
                cells.append(cell.crop(box))
    mask=b""
    if args.rigid_base:
        frames,report,mask=rigid_base_cells(cells,args.width,args.height,args.supersample)
        result=bytearray(struct.pack("<4sHHHH",b"N50R",args.width,args.height,len(cells),
                                     report["reference_frame"]))
    else:
        frames,report=align_cells(cells,args.width,args.height)
        result=bytearray(struct.pack("<4sHHH",b"N50B",args.width,args.height,len(cells)))
    for frame in frames:
        result.extend(frame.tobytes())
    result.extend(mask)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    if args.registration_report:
        args.registration_report.write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(f"Packed {len(cells)} {args.width}x{args.height} RGBA frames: {args.output}")


if __name__ == "__main__":
    main()
