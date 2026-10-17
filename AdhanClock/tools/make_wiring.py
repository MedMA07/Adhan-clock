from PIL import Image, ImageDraw, ImageFont
F="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"; FB="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
f=lambda s,b=False: ImageFont.truetype(FB if b else F,s)
W=1240
groups=[
 ("1) Ecran TFT 1.8\" ST7735S (8 pins)",(124,32,48),[("VCC","3V3","#e03030"),("GND","GND","#111"),("CS","GPIO 5","#20a030"),("RESET","GPIO 22","#8030c0"),("A0 (= DC)","GPIO 21","#f07010"),("SDA (= MOSI)","GPIO 23","#2070d0"),("SCK","GPIO 18","#e0a000"),("LED (backlight)","3V3","#e03030")],None),
 ("2) Capteur tactile TTP223 (3 pins)",(44,110,76),[("VCC","3V3","#e03030"),("GND","GND","#111"),("I/O","GPIO 27","#20a030")],None),
 ("3) Buzzer via transistor S8050 (NPN)",(110,80,22),[("Resistance 1k  ->  Base (B)","GPIO 25","#8030c0"),("Emetteur (E)","GND","#111"),("Buzzer (+)","VIN = 5V","#e03030")],
  "S8050 (TO-92, face plate vers toi):  E - B - C  de gauche a droite.   Buzzer (-) va sur le Collecteur (C)."),
]
RH=39
H=90+sum(60+len(r)*RH+(40 if n else 20)+30 for _,_,r,n in groups)
im=Image.new("RGB",(W,H),"white"); d=ImageDraw.Draw(im)
d.text((W//2,26),"Adhan Clock - Cablage / Wiring",font=f(26,True),fill="black",anchor="mm")
d.text((170,62),"MODULE",font=f(15),fill="#555",anchor="mm"); d.text((930,62),"ESP32 (38 pin)",font=f(15),fill="#555",anchor="mm")
y=84
for title,col,rows,note in groups:
    d.rounded_rectangle((35,y,W-60,y+40),10,fill=col); d.text((55,y+20),title,font=f(19,True),fill="white",anchor="lm")
    y+=60
    if note: d.text((55,y-8),note,font=f(15),fill="#333"); y+=26
    for a,b,c in rows:
        d.rectangle((80,y,385,y+27),fill="#f6f2e4",outline="#555"); d.text((97,y+14),a,font=f(16),fill="black",anchor="lm")
        d.rectangle((795,y,1050,y+27),fill="#dae6f4",outline="#555"); d.text((812,y+14),b,font=f(16,True),fill="black",anchor="lm")
        d.line((385,y+14,795,y+14),fill=c,width=5); y+=RH
    y+=34
d.text((55,y-12),"Tous les GND sont relies ensemble.  Buzzer actif ou passif: les deux marchent.",font=f(15),fill="#333")
im.crop((0,0,W,y+16)).save(__import__("os").path.join(__import__("os").path.dirname(__import__("os").path.abspath(__file__)),"..","..","docs","wiring.png"))
