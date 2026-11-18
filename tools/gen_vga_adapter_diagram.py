P=34; OX=350; OY=120; COLS=18; ROWS=24
def x(c): return OX+(c-1)*P
def y(r): return OY+(r-1)*P
LC=7; RC=17; R0=2
def hr(n): return y(n+R0-1)
J1=["3V3","3V3","RST","GPIO4","GPIO5","GPIO6","GPIO7","GPIO15","GPIO16","GPIO17","GPIO18","GPIO8","GPIO3","GPIO46","GPIO9","GPIO10","GPIO11","GPIO12","GPIO13","GPIO14","5V","GND"]
J3=["GND","TX","RX","GPIO1","GPIO2","GPIO42","GPIO41","GPIO40","GPIO39","GPIO38","GPIO37","GPIO36","GPIO35","GPIO0","GPIO45","GPIO48","GPIO47","GPIO21","GPIO20","GPIO19","GND","GND"]
RED="#c0392b"; GRN="#1e8449"; BLU="#1f5fa8"; GRY="#555"
slots={4:("510",RED),5:("1 k",RED),6:("2 k",RED),7:("510",GRN),8:("1 k",GRN),9:("2 k",GRN),10:("510",BLU),11:("1 k",BLU),12:("2 k",BLU),15:("100",GRY),16:("100",GRY)}
chan=[("RED","VGA pin 1",RED,4,6),("GREEN","VGA pin 2",GRN,7,9),("BLUE","VGA pin 3",BLU,10,12)]
W=x(COLS)+330; H=y(ROWS)+400
o=[];a=o.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica,Arial,sans-serif">')
a(f'<rect width="{W}" height="{H}" fill="#fff"/>')
a(f'<text x="{W/2}" y="44" text-anchor="middle" font-size="25" font-weight="bold">Adapter board: VGA in 256 colours, audio out, beeper and supply capacitor</text>')
a(f'<text x="{W/2}" y="74" text-anchor="middle" font-size="17" fill="#444">5x7 cm perfboard (18 x 24 holes). Top view, component side, antenna end at the top.</text>')
a(f'<rect x="{x(1)-P*0.7}" y="{y(1)-P*0.7}" width="{(COLS-1)*P+P*1.4}" height="{(ROWS-1)*P+P*1.4}" rx="10" fill="#e9d9a6" stroke="#8a7332" stroke-width="2"/>')
for c in range(1,COLS+1):
    for r in range(1,ROWS+1):
        if 8<=c<=16 and 2<=r<=23: continue
        a(f'<circle cx="{x(c)}" cy="{y(r)}" r="4" fill="#fff" stroke="#b09a55"/>')
    a(f'<text x="{x(c)}" y="{y(1)-P*0.95}" text-anchor="middle" font-size="12" fill="#666">{c}</text>')
a(f'<rect x="{x(LC)-P*0.55}" y="{hr(1)-P*0.6}" width="{(RC-LC)*P+P*1.1}" height="{21*P+P*1.2}" rx="6" fill="#2b2b2b" fill-opacity="0.10" stroke="#333" stroke-dasharray="7 5" stroke-width="2"/>')
mx=(x(LC)+x(RC))/2
a(f'<text x="{mx}" y="{hr(11)}" text-anchor="middle" font-size="19" fill="#333">ESP32-S3 dev board</text>')
a(f'<text x="{mx}" y="{hr(11)+24}" text-anchor="middle" font-size="15" fill="#333">(plugs in here)</text>')
a(f'<text x="{mx}" y="{hr(1)+6}" text-anchor="middle" font-size="14" fill="#333">antenna end</text>')
a(f'<text x="{mx}" y="{hr(22)+4}" text-anchor="middle" font-size="14" fill="#333">USB sockets end</text>')
for col,names,side in ((LC,J1,1),(RC,J3,-1)):
    a(f'<rect x="{x(col)-13}" y="{hr(1)-13}" width="26" height="{21*P+26}" fill="#222"/>')
    for i,n in enumerate(names,1):
        a(f'<rect x="{x(col)-6}" y="{hr(i)-6}" width="12" height="12" fill="#d8b23a"/>')
        used = col==LC and (i in slots or i in (1,19,20,22))
        a(f'<text x="{x(col)+side*20}" y="{hr(i)+5}" text-anchor="{"start" if side==1 else "end"}" font-size="{14 if used else 12}" font-weight="{"bold" if used else "normal"}" fill="{"#000" if used else "#888"}">{n}</text>')
a(f'<text x="{x(LC)}" y="{hr(22)+40}" text-anchor="middle" font-size="13">socket strip 1x22</text>')
a(f'<text x="{x(RC)}" y="{hr(22)+40}" text-anchor="middle" font-size="13">socket strip 1x22</text>')
PUR="#7d3c98"; ORG="#b9770e"
a(f'<polyline points="{x(4)},{hr(22)} {x(4)},{hr(17)} {x(2)},{hr(17)}" fill="none" stroke="#111" stroke-width="5" stroke-linejoin="round"/>')
a(f'<polyline points="{x(3)},{hr(2)} {x(5)},{hr(2)} {x(5)},{hr(20)} {x(6)},{hr(20)}" fill="none" stroke="{PUR}" stroke-width="5" stroke-linejoin="round"/>')
REDW="#c0392b"
a(f'<polyline points="{x(3)},{hr(13)} {x(5)+P/2},{hr(13)} {x(5)+P/2},{hr(2)} {x(6)},{hr(2)}" fill="none" stroke="{REDW}" stroke-width="5" stroke-linejoin="round"/>')
a(f'<polyline points="{x(3)},{hr(14)} {x(3)},{hr(22)}" fill="none" stroke="#111" stroke-width="5" stroke-linejoin="round"/>')
a(f'<text x="{x(5)+16}" y="{hr(13)+20}" font-size="12" fill="{PUR}" transform="rotate(-90 {x(5)+16} {hr(13)+20})">beeper wire</text>')
for pin,(val,col) in slots.items():
    yy=hr(pin)
    a(f'<line x1="{x(2)}" y1="{yy}" x2="{x(6)}" y2="{yy}" stroke="#888" stroke-width="3"/>')
    a(f'<rect x="{x(3)-8}" y="{yy-11}" width="{2*P+16}" height="22" rx="9" fill="#f3e3c0" stroke="{col}" stroke-width="3"/>')
    a(f'<text x="{x(4)}" y="{yy+5}" text-anchor="middle" font-size="14" font-weight="bold" fill="{col}">{val} Ω</text>')
    a(f'<line x1="{x(6)}" y1="{yy}" x2="{x(7)}" y2="{yy}" stroke="#9aa0a6" stroke-width="9" stroke-linecap="round"/>')
    a(f'<circle cx="{x(2)}" cy="{yy}" r="5" fill="#666"/><circle cx="{x(6)}" cy="{yy}" r="5" fill="#666"/>')
a(f'<rect x="{x(1)-13}" y="{hr(4)-13}" width="26" height="{13*P+26}" fill="#222"/>')
for i in range(4,18): a(f'<rect x="{x(1)-6}" y="{hr(i)-6}" width="12" height="12" fill="#d8b23a"/>')
def out(r,lab,col):
    a(f'<line x1="{x(1)}" y1="{hr(r)}" x2="{x(2)}" y2="{hr(r)}" stroke="#9aa0a6" stroke-width="9" stroke-linecap="round"/>')
    a(f'<line x1="{x(1)-20}" y1="{hr(r)}" x2="{x(1)-42}" y2="{hr(r)}" stroke="{col}" stroke-width="2"/>')
    a(f'<text x="{x(1)-48}" y="{hr(r)+5}" text-anchor="end" font-size="15" font-weight="bold" fill="{col}">{lab}</text>')
for name,vp,col,lo,hi in chan:
    a(f'<line x1="{x(2)}" y1="{hr(lo)}" x2="{x(2)}" y2="{hr(hi)}" stroke="#9aa0a6" stroke-width="9" stroke-linecap="round"/>')
    for r in range(lo,hi+1): a(f'<circle cx="{x(2)}" cy="{hr(r)}" r="5" fill="#666"/>')
    out(lo,f"{name}  →  {vp}",col)
out(15,"HSYNC  →  VGA pin 13","#333"); out(16,"VSYNC  →  VGA pin 14","#333")
SB='stroke="#9aa0a6" stroke-width="9" stroke-linecap="round"'
def bridge(c1,r1,c2,r2): a(f'<line x1="{x(c1)}" y1="{hr(r1)}" x2="{x(c2)}" y2="{hr(r2)}" {SB}/>')
def dot(c,r,col="#666"): a(f'<circle cx="{x(c)}" cy="{hr(r)}" r="5" fill="{col}"/>')
PUR="#7d3c98"; ORG="#b9770e"
# ground: bare bus along the GND row, insulated wire up to the VGA ground pin
bridge(6,22,7,22)
a(f'<line x1="{x(1)}" y1="{hr(22)}" x2="{x(6)}" y2="{hr(22)}" stroke="#111" stroke-width="7" stroke-dasharray="2 5"/>')
a(f'<text x="{x(4)}" y="{hr(22)+22}" text-anchor="middle" font-size="13">bare ground wire</text>')
dot(2,17,"#111"); dot(4,22,"#111"); dot(6,22,"#111")
out(17,"GND  →  VGA pins 5 6 7 8 10","#000")
a(f'<text x="{x(1)-48}" y="{hr(4)-34}" text-anchor="end" font-size="13" fill="#444">pin header 1x14, wires to the VGA socket</text>')
# beeper wire (drawn first so parts sit on top)
# audio: 1 k on the GPIO13 row, 100 nF to ground, series capacitor to the jack pin
yy=hr(19)
a(f'<line x1="{x(2)}" y1="{yy}" x2="{x(6)}" y2="{yy}" stroke="#888" stroke-width="3"/>')
a(f'<rect x="{x(3)-8}" y="{yy-11}" width="{2*P+16}" height="22" rx="9" fill="#f3e3c0" stroke="{ORG}" stroke-width="3"/>')
a(f'<text x="{x(4)}" y="{yy+5}" text-anchor="middle" font-size="14" font-weight="bold" fill="{ORG}">1 k Ω</text>')
bridge(6,19,7,19); bridge(1,19,2,19); bridge(2,19,2,20); bridge(1,17,1,18); bridge(1,21,2,21); bridge(1,22,1,22)
a(f'<rect x="{x(1)-11}" y="{hr(18)-9}" width="22" height="{P+18}" rx="8" fill="#e0a030" stroke="#7a5200" stroke-width="2"/>')
a(f'<text x="{x(1)-20}" y="{hr(18)+5}" text-anchor="end" font-size="13" fill="{ORG}" font-weight="bold">100 nF</text>')
a(f'<rect x="{x(2)-13}" y="{hr(20)-12}" width="26" height="{P+24}" rx="13" fill="#34495e" stroke="#111" stroke-width="2"/>')
a(f'<text x="{x(2)}" y="{hr(20)+5}" text-anchor="middle" font-size="15" font-weight="bold" fill="#fff">+</text>')
a(f'<text x="{x(1)-20}" y="{hr(20)+5}" text-anchor="end" font-size="13" fill="{ORG}" font-weight="bold">1 to 10 µF, + up</text>')
for c,r in ((2,19),(6,19),(1,18),(1,19)): dot(c,r)
a(f'<rect x="{x(1)-13}" y="{hr(21)-13}" width="26" height="{P+26}" fill="#222"/>')
for r in (21,22): a(f'<rect x="{x(1)-6}" y="{hr(r)-6}" width="12" height="12" fill="#d8b23a"/>')
for r,lab in ((21,"AUDIO OUT  →  jack tip and ring"),(22,"AUDIO GND  →  jack sleeve")):
    a(f'<line x1="{x(1)-20}" y1="{hr(r)}" x2="{x(1)-42}" y2="{hr(r)}" stroke="{ORG}" stroke-width="2"/>')
    a(f'<text x="{x(1)-48}" y="{hr(r)+5}" text-anchor="end" font-size="15" font-weight="bold" fill="{ORG}">{lab}</text>')
# supply capacitor between 3V3 and GND, in the empty rows of GPIO3 and GPIO46
a(f'<circle cx="{x(3)}" cy="{(hr(13)+hr(14))/2}" r="{P*1.25}" fill="#34495e" fill-opacity="0.88" stroke="#111" stroke-width="2"/>')
a(f'<text x="{x(3)+P*0.55}" y="{hr(13)+4}" text-anchor="middle" font-size="15" font-weight="bold" fill="#fff">+</text>')
a(f'<text x="{x(3)+P*0.55}" y="{hr(14)+6}" text-anchor="middle" font-size="15" font-weight="bold" fill="#fff">−</text>')
dot(3,13,REDW); dot(3,14,"#111"); dot(3,22,"#111")
a(f'<text x="{x(1)-20}" y="{(hr(13)+hr(14))/2-3}" text-anchor="end" font-size="13" fill="{REDW}" font-weight="bold">100 to 470 µF, + up</text>')
a(f'<text x="{x(1)-20}" y="{(hr(13)+hr(14))/2+13}" text-anchor="end" font-size="12" fill="#444">3V3 to GND</text>')
# beeper between 3V3 and GPIO14
bridge(6,1,7,1); bridge(3,1,3,2)
a(f'<circle cx="{(x(3)+x(6))/2}" cy="{hr(1)}" r="{P*2.0}" fill="#222" fill-opacity="0.82" stroke="#000" stroke-width="2"/>')
a(f'<circle cx="{(x(3)+x(6))/2}" cy="{hr(1)}" r="7" fill="#555"/>')
a(f'<text x="{(x(3)+x(6))/2}" y="{hr(1)-22}" text-anchor="middle" font-size="14" font-weight="bold" fill="#fff">beeper</text>')
dot(3,1,"#ddd"); dot(6,1,"#ddd"); dot(3,2,PUR); dot(6,20,PUR)
bridge(6,2,7,2); dot(6,2,REDW)
bridge(6,20,7,20); dot(6,20,PUR)
ly=y(ROWS)+70
a(f'<line x1="60" y1="{ly}" x2="100" y2="{ly}" {SB}/><text x="112" y="{ly+5}" font-size="15">solder bridge between neighbouring holes, on the underside</text>')
a(f'<line x1="60" y1="{ly+24}" x2="100" y2="{ly+24}" stroke="#111" stroke-width="5"/><line x1="60" y1="{ly+36}" x2="100" y2="{ly+36}" stroke="{PUR}" stroke-width="5"/><line x1="60" y1="{ly+48}" x2="100" y2="{ly+48}" stroke="{REDW}" stroke-width="5"/><text x="112" y="{ly+41}" font-size="15">insulated wires on the underside: grounds (black), beeper to GPIO14 (purple), capacitor to 3V3 (red)</text>')
a(f'<line x1="60" y1="{ly+74}" x2="100" y2="{ly+74}" stroke="#111" stroke-width="7" stroke-dasharray="2 5"/><text x="112" y="{ly+79}" font-size="15">bare wire along the GND row, soldered at every hole it passes (the ground rail)</text>')
for k,t in enumerate(["Video: each colour has 510 Ω, 1 kΩ and 2 kΩ joined at their left ends. The rows of GPIO3 and GPIO46 stay empty.",
 "Audio: GPIO13 through 1 kΩ. The 100 nF capacitor goes from that point to ground, the 1 to 10 µF capacitor (+ towards the resistor) to AUDIO OUT.",
 "Beeper: between 3V3 and GPIO14, for a piezo beeper of about 10 mm; a 12 mm one overhangs the edge a little. A magnetic or 5 V buzzer needs a transistor instead.",
 "Supply capacitor: 100 to 470 µF, 10 V or more, + to the second 3V3 pin, − to the ground rail. Stand it a little above the resistors if it is wide.",
 "VGA ground: the three colour returns (pins 6, 7, 8), the sync ground (pin 10) and the cable's shield all go to the GND pin or the ground rail, as far as the cable has them.",
 "The right socket strip is drawn 10 holes from the left one; if your board is 9 apart, use column 16.",
 "Pin names follow the ESP32-S3-DevKitC-1 layout. Compare them with the labels printed on your board before soldering."]):
    a(f'<text x="60" y="{ly+118+k*26}" font-size="15">{t}</text>')
a('</svg>')
open('docs/hardware/vga-adapter-256.svg','w').write('\n'.join(o))
