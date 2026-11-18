import random, math, sys
variant = sys.argv[1] if len(sys.argv) > 1 else "A"
random.seed(7)
W=1024; CX=W/2; CY=W*0.64; S=165.0
def P(x,y,z=0):
    return (CX+(x-y)*0.866*S, CY+(x+y)*0.5*S - z*S)
def pts(l): return " ".join(f"{a:.1f},{b:.1f}" for a,b in l)
out=[]; A=out.append
A(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{W}" viewBox="0 0 {W} {W}">')
A('''<defs>
<radialGradient id="bg" cx="50%" cy="55%" r="75%"><stop offset="0" stop-color="#13263a"/><stop offset="0.6" stop-color="#09121e"/><stop offset="1" stop-color="#04070d"/></radialGradient>
<radialGradient id="fade" cx="0" cy="0" r="5.5" gradientUnits="userSpaceOnUse"><stop offset="0.2" stop-color="#fff"/><stop offset="1" stop-color="#000"/></radialGradient>
<mask id="m" maskUnits="userSpaceOnUse" x="-10" y="-10" width="20" height="20"><rect x="-10" y="-10" width="20" height="20" fill="url(#fade)"/></mask>
<filter id="fb1" filterUnits="userSpaceOnUse" x="-10" y="-10" width="20" height="20"><feGaussianBlur stdDeviation="0.07"/></filter>
<filter id="fb2" filterUnits="userSpaceOnUse" x="-10" y="-10" width="20" height="20"><feGaussianBlur stdDeviation="0.16"/></filter>
<filter id="blur" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="6"/></filter>
<filter id="blur2" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="14"/></filter>
<filter id="blur3" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="24"/></filter>
<linearGradient id="topg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffb071"/><stop offset="1" stop-color="#e8782f"/></linearGradient>
<linearGradient id="lg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1f2833"/><stop offset="1" stop-color="#11161d"/></linearGradient>
<linearGradient id="rg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#171f29"/><stop offset="1" stop-color="#0b1016"/></linearGradient>
<radialGradient id="vig" cx="50%" cy="52%" r="72%"><stop offset="0.55" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.75"/></radialGradient>
<linearGradient id="pin" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#cfe8f2"/><stop offset="1" stop-color="#7c95a3"/></linearGradient>
</defs>''')
A(f'<rect width="{W}" height="{W}" fill="url(#bg)"/>')
a=0.866*S; b=0.5*S
A(f'<g transform="matrix({a:.3f} {b:.3f} {-a:.3f} {b:.3f} {CX} {CY})" mask="url(#m)">')
A('<rect x="-10" y="-10" width="20" height="20" fill="#0d2a3d" opacity="0.6"/>')
R=9; edges=set(); deg={}
def add(p,q):
    e=tuple(sorted([p,q]))
    if p==q or e in edges: return
    edges.add(e); deg[p]=deg.get(p,0)+1; deg[q]=deg.get(q,0)+1
for i in range(-R,R+1):
    for j in range(-R,R+1):
        if abs(i)<=2 and abs(j)<=2: continue
        if random.random()<0.36 and i<R: add((i,j),(i+1,j))
        if random.random()<0.36 and j<R: add((i,j),(i,j+1))
        if random.random()<0.10 and i<R and j<R: add((i,j),(i+1,j+1))
        if random.random()<0.10 and i<R and j>-R: add((i,j),(i+1,j-1))
lines="".join(f'M{p[0]} {p[1]}L{q[0]} {q[1]}' for p,q in edges)
padl=[p for p,d in deg.items() if d==1 or d>=3]
# traces leaving the chip's pins, bright
N=5; half=1.25; pitch=2*half/(N+1)
pin_tr=""
for k in range(N):
    t=-half+pitch*(k+1)
    for (x0,y0,x1,y1) in ((t,half+0.3,t,3.0),(half+0.3,t,3.0,t),(t,-half-0.3,t,-3.0),(-half-0.3,t,-3.0,t)):
        pin_tr+=f'M{x0:.2f} {y0:.2f}L{x1:.2f} {y1:.2f}'
for sw,op,flt in ((0.32,0.9,'url(#fb2)'),(0.14,1.0,'url(#fb1)')):
    A(f'<g filter="{flt}" opacity="{op}"><path d="{lines}" stroke="#22c8ff" stroke-width="{sw}" fill="none" stroke-linecap="round"/><g fill="#22c8ff">{"".join(f"<circle cx=\"{p[0]}\" cy=\"{p[1]}\" r=\"0.13\"/>" for p in padl)}</g></g>')
A(f'<path d="{lines}" stroke="#d6f8ff" stroke-width="0.05" fill="none" stroke-linecap="round"/>')
A(f'<g fill="none" stroke="#aef0ff" stroke-width="0.07">{"".join(f"<circle cx=\"{p[0]}\" cy=\"{p[1]}\" r=\"0.13\"/>" for p in padl)}</g>')
A(f'<path d="{pin_tr}" stroke="#22c8ff" stroke-width="0.34" fill="none" filter="url(#fb2)"/>')
A(f'<path d="{pin_tr}" stroke="#22c8ff" stroke-width="0.16" fill="none" filter="url(#fb1)"/>')
A(f'<path d="{pin_tr}" stroke="#d8f8ff" stroke-width="0.09" fill="none"/>')
A('</g>')
A(f'<ellipse cx="{CX}" cy="{CY+4}" rx="300" ry="170" fill="#22c8ff" opacity="0.22" filter="url(#blur3)"/>')
def box(x0,y0,z0,x1,y1,z1,top='url(#topg)',left='url(#lg)',right='url(#rg)'):
    A(f'<polygon points="{pts([P(x0,y1,z0),P(x1,y1,z0),P(x1,y1,z1),P(x0,y1,z1)])}" fill="{left}"/>')
    A(f'<polygon points="{pts([P(x1,y0,z0),P(x1,y1,z0),P(x1,y1,z1),P(x1,y0,z1)])}" fill="{right}"/>')
    A(f'<polygon points="{pts([P(x0,y0,z1),P(x1,y0,z1),P(x1,y1,z1),P(x0,y1,z1)])}" fill="{top}"/>')
def line(p,q,w=3,c='#6fe3ff',op=0.9):
    A(f'<line x1="{p[0]:.1f}" y1="{p[1]:.1f}" x2="{q[0]:.1f}" y2="{q[1]:.1f}" stroke="{c}" stroke-width="{w}" stroke-linecap="round" opacity="{op}"/>')
H=0.32; pw=0.13
# back pins (behind the chip) first
for k in range(N):
    t=-half+pitch*(k+1)
    box(t-pw/2,-half-0.32,0,t+pw/2,-half,0.10,top='url(#pin)',left='#8aa3b0',right='#5d7380')
    box(-half-0.32,t-pw/2,0,-half,t+pw/2,0.10,top='url(#pin)',left='#8aa3b0',right='#5d7380')
# chip body
box(-half,-half,0,half,half,H)
line(P(-half,half,H),P(half,half,H),3,'#ffd2a8'); line(P(half,-half,H),P(half,half,H),3,'#ffd2a8')
line(P(-half,-half,H),P(half,-half,H),2,'#ffd2a8',0.6); line(P(-half,-half,H),P(-half,half,H),2,'#ffd2a8',0.6)
# die square on top
d=0.62
A(f'<polygon points="{pts([P(-d,-d,H),P(d,-d,H),P(d,d,H),P(-d,d,H)])}" fill="#0f1822" stroke="#22c8ff" stroke-width="3" opacity="0.95"/>')
# pin-1 dot
cx,cy=P(-half+0.28,-half+0.28,H); A(f'<ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="11" ry="6" fill="#7a3a10" opacity="0.8"/>')
# front pins
for k in range(N):
    t=-half+pitch*(k+1)
    box(t-pw/2,half,0,t+pw/2,half+0.32,0.10,top='url(#pin)',left='#9ab3c0',right='#5d7380')
    box(half,t-pw/2,0,half+0.32,t+pw/2,0.10,top='url(#pin)',left='#9ab3c0',right='#5d7380')
# the mod's wifi signal
def wifi(cx, cy, scale, flat):
    # arcs centred on (cx,cy) opening upwards; flat=True: lying on the chip (isometric squash)
    sy = 0.5 if flat else 1.0
    paths=[]
    for r in (1.0, 0.68, 0.36):
        ang=math.radians(48)
        x0=cx-math.sin(ang)*r*scale; y0=cy-math.cos(ang)*r*scale*sy
        x1=cx+math.sin(ang)*r*scale; y1=y0
        paths.append(f'M{x0:.1f} {y0:.1f}A{r*scale:.1f} {r*scale*sy:.1f} 0 0 1 {x1:.1f} {y1:.1f}')
    p="".join(paths); w=scale*0.17
    A(f'<path d="{p}" stroke="#ffffff" stroke-width="{w:.1f}" fill="none" stroke-linecap="round"/>')
    A(f'<ellipse cx="{cx}" cy="{cy}" rx="{w*0.75:.1f}" ry="{w*0.75*sy:.1f}" fill="#ffffff"/>')
tx,ty=P(0,0,H)
if variant=="A":
    # signal rising from the chip: a beam, then the wifi floating above
    wifi(tx, ty-215, 215, False)
else:
    # signal coming out flat from the chip's die, spreading over it
    wifi(tx, ty+18, 118, True)
A('<rect width="1024" height="1024" fill="url(#vig)"/>')
A('</svg>')
open(f'chip_{variant}.svg','w').write("\n".join(out))
