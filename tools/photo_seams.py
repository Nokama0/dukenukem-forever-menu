import sys, numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS=None
p=sys.argv[1]; tw,th=3440,1440
im=np.asarray(Image.open(p).convert('RGB'),dtype=np.int16); H,W,_=im.shape
seg=128; out=[]
for k in range(1,H//th):   # horizontal seams: rows y-1|y
    y=k*th; a=np.abs(im[y]-im[y-1]).mean(axis=1); r=np.abs(im[y-1]-im[y-2]).mean(axis=1)+np.abs(im[y+1]-im[y]).mean(axis=1)
    for s in range(0,W,seg):
        sa=a[s:s+seg].mean(); sr=r[s:s+seg].mean()/2
        out.append((sa-sr, 'H', y, s, sa, sr))
for k in range(1,W//tw):
    x=k*tw; a=np.abs(im[:,x]-im[:,x-1]).mean(axis=1); r=np.abs(im[:,x-1]-im[:,x-2]).mean(axis=1)+np.abs(im[:,x+1]-im[:,x]).mean(axis=1)
    for s in range(0,H,seg):
        sa=a[s:s+seg].mean(); sr=r[s:s+seg].mean()/2
        out.append((sa-sr,'V',x,s,sa,sr))
out.sort(reverse=True)
print(p.split('/')[-1])
for o in out[:10]: print(f' {o[1]} seam at {o[2]} segment {o[3]}: seam diff {o[4]:.1f} vs neighbours {o[5]:.1f}')
