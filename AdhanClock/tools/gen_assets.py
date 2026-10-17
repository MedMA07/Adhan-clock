#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_assets.py  -  يولّد صور النصوص العربية (bitmaps) لشاشة ST7735 160x128
Generates the Arabic bitmaps (4-bit alpha), the splash screen (RGB565) and the
adhkar pages as C headers for the ESP32 sketch.

Requirements:  pip install pillow      (Pillow must be built with libraqm)
Run:           python3 gen_assets.py   (from the tools/ folder)
Output:        ../assets_labels.h  ../assets_duas.h  ../assets_splash.h  + preview PNGs
"""
import os, sys, math, random
from PIL import Image, ImageDraw, ImageFont, features

if not features.check('raqm'):
    sys.exit("Pillow needs libraqm for Arabic shaping (pip install --upgrade pillow)")

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, '..'))
FONT_R = os.path.join(HERE, 'fonts', 'Amiri-Regular.ttf')
FONT_B = os.path.join(HERE, 'fonts', 'Amiri-Bold.ttf')
SS = 4            # supersampling factor for small text
SCR_W, SCR_H = 160, 128
GAMMA = 0.72      # <1 makes light-on-dark text a bit bolder


def font(path, size):
    return ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.RAQM)


def tlen(f, s):
    return f.getlength(s, direction='rtl', language='ar')


_win_cache = {}


def ink_window(path, size, texts, ss=SS):
    """Vertical window (hi-res px) = union of the ink of all texts that share this size."""
    key = (path, size, tuple(texts))
    if key in _win_cache:
        return _win_cache[key]
    f = font(path, size * ss)
    asc, desc = f.getmetrics()
    pad = 3 * ss
    top, bot = 10 ** 9, 0
    for t in texts:
        im = Image.new('L', (int(tlen(f, t)) + 8 * ss, asc + desc + 2 * pad), 0)
        ImageDraw.Draw(im).text((4 * ss, pad), t, font=f, fill=255, direction='rtl', language='ar')
        bb = im.getbbox()
        if bb:
            top = min(top, bb[1]); bot = max(bot, bb[3])
    top = max(0, (top - ss // 2) // ss * ss)
    bot = -(-(bot + ss // 2) // ss) * ss
    _win_cache[key] = (top, bot)
    return top, bot


def render_line(text, path, size, ss=SS, group=None):
    """Render one line -> 8-bit alpha image (hi-res), cropped vertically to the shared ink window."""
    f = font(path, size * ss)
    asc, desc = f.getmetrics()
    pad = 3 * ss
    top, bot = ink_window(path, size, group or [text], ss)
    w = int(math.ceil(tlen(f, text))) + 4 * ss
    im = Image.new('L', (w, asc + desc + 2 * pad), 0)
    ImageDraw.Draw(im).text((2 * ss, pad), text, font=f, fill=255, direction='rtl', language='ar')
    im = im.crop((0, top, w, min(im.height, bot)))
    return im, ss


def downscale(im, ss):
    w = max(1, math.ceil(im.width / ss)); h = max(1, math.ceil(im.height / ss))
    im2 = Image.new('L', (w * ss, h * ss), 0); im2.paste(im, (0, 0))
    return im2.resize((w, h), Image.BOX)


def crop_h(im):
    """crop empty columns left/right (keep full height)."""
    bb = im.getbbox()
    if not bb:
        return im
    return im.crop((max(0, bb[0] - 0), 0, min(im.width, bb[2] + 0), im.height))


def rle_nibbles(q):
    """Nibble-level RLE: nonzero nibble = literal; 0,k = (k+1) zeros (k<15); 0,15,hi,lo = 16+(hi<<4|lo) zeros."""
    o, i = [], 0
    while i < len(q):
        if q[i] == 0:
            j = i
            while j < len(q) and q[j] == 0 and j - i < 16 + 255:
                j += 1
            r = j - i; i = j
            o += [0, r - 1] if r <= 15 else [0, 15, (r - 16) >> 4, (r - 16) & 15]
        else:
            o.append(q[i]); i += 1
    if len(o) % 2:
        o.append(0)       # pad (decoder never reads past w*h pixels)
    return [(o[k] << 4) | o[k + 1] for k in range(0, len(o), 2)]


def to_nibbles(im):
    px = im.load()
    flat = [px[x, y] for y in range(im.height) for x in range(im.width)]
    q = [min(15, int(round(((v / 255.0) ** GAMMA) * 15))) for v in flat]
    data = rle_nibbles(q)
    # self-check: decode and compare
    n, out, z = 0, [], 0
    nb = []
    for b in data: nb += [b >> 4, b & 15]
    it = iter(nb)
    while len(out) < len(q):
        v = next(it)
        if v: out.append(v)
        else:
            k = next(it)
            r = k + 1 if k < 15 else 16 + (next(it) << 4) + next(it)
            out += [0] * r
    assert out[:len(q)] == q
    return data


def c_array(name, data, per=24):
    lines = []
    for i in range(0, len(data), per):
        lines.append('  ' + ','.join('0x%02X' % b for b in data[i:i + per]) + ',')
    return 'static const uint8_t %s[] PROGMEM = {\n%s\n};\n' % (name, '\n'.join(lines))


# ----------------------------------------------------------------------------
# 1) small labels / names
# ----------------------------------------------------------------------------
# key, text, size, bold
LABELS = [
    # prayers: small (S) and large (L)
    ('FAJR_S', 'الفجر', 15, True), ('SHURUQ_S', 'الشروق', 15, True), ('DHUHR_S', 'الظهر', 15, True),
    ('ASR_S', 'العصر', 15, True), ('MAGHRIB_S', 'المغرب', 15, True), ('ISHA_S', 'العشاء', 15, True),
    ('JUMUA_S', 'الجمعة', 15, True),
    ('FAJR_L', 'الفجر', 34, True), ('SHURUQ_L', 'الشروق', 34, True), ('DHUHR_L', 'الظهر', 34, True),
    ('ASR_L', 'العصر', 34, True), ('MAGHRIB_L', 'المغرب', 34, True), ('ISHA_L', 'العشاء', 34, True),
    ('JUMUA_L', 'الجمعة', 34, True),
    # weekdays (Sunday first)
    ('DAY0', 'الأحد', 15, True), ('DAY1', 'الاثنين', 15, True), ('DAY2', 'الثلاثاء', 15, True),
    ('DAY3', 'الأربعاء', 15, True), ('DAY4', 'الخميس', 15, True), ('DAY5', 'الجمعة', 15, True),
    ('DAY6', 'السبت', 15, True),
    # hijri months
    ('HM1', 'محرم', 14, True), ('HM2', 'صفر', 14, True), ('HM3', 'ربيع الأول', 14, True),
    ('HM4', 'ربيع الآخر', 14, True), ('HM5', 'جمادى الأولى', 14, True), ('HM6', 'جمادى الآخرة', 14, True),
    ('HM7', 'رجب', 14, True), ('HM8', 'شعبان', 14, True), ('HM9', 'رمضان', 14, True),
    ('HM10', 'شوال', 14, True), ('HM11', 'ذو القعدة', 14, True), ('HM12', 'ذو الحجة', 14, True),
    # ui labels
    ('NEXT', 'الصلاة القادمة', 13, True),
    ('T_TIMES', 'مواقيت الصلاة', 13, True),
    ('T_DUAS', 'أذكار وأدعية', 13, True),
    ('T_SETUP', 'ربط الجهاز بالهاتف', 13, True),
    ('NOW_TIME', 'حان الآن وقت صلاة', 18, True),
    ('SOON', 'اقترب موعد صلاة', 17, True),
    ('REMAIN', 'متبقي', 14, True),
    ('KAHF_BANNER', 'اقرأ سورة الكهف', 14, True),
    ('KAHF_T1', 'يوم الجمعة', 20, True),
    ('KAHF_T2', 'لا تنس قراءة سورة الكهف', 14, True),
    ('KAHF_T3', 'نورٌ لك بين الجمعتين', 15, False),
    ('NO_TIME', 'جاري ضبط الوقت…', 14, True),
    ('NO_DATA', 'لا توجد مواقيت - افتح الإعدادات', 12, True),
    ('UPDATING', 'جاري التحديث…', 14, True),
    ('QR1', '١ - اتصل بشبكة الجهاز', 13, True),
    ('QR2', '٢ - افتح صفحة الإعدادات', 13, True),
    ('QR_SCAN', 'امسح الرمز بكاميرا الهاتف', 13, False),
    ('QR_PASS', 'كلمة السر', 13, False),
    ('WIFI_OK', 'متصل بالإنترنت', 13, False),
    ('WIFI_NO', 'غير متصل بالإنترنت', 13, False),
    ('LONG_HINT', 'ضغطة طويلة: التالي', 12, False),
    ('DONE', 'تقبّل الله', 14, True),
    ('PRAY_NABI', 'اللهم صل وسلم على نبينا محمد', 14, True),
]

# ----------------------------------------------------------------------------
# 2) adhkar / duas   (cats)  -- plain Arabic (no tashkeel) so it stays crisp at 15px
# ----------------------------------------------------------------------------
CATS = [
    'أذكار الصباح',        # 0
    'أذكار المساء',        # 1
    'أذكار بعد الصلاة',    # 2
    'النوم والاستيقاظ',    # 3
    'أدعية يومية',         # 4
    'أدعية من القرآن',     # 5
    'استغفار وذكر',        # 6  (also used by the periodic reminder popup)
    'بعد الأذان',          # 7
    'من القرآن الكريم',    # 8
]
# (text, [cats], count-note)
DUAS = [
    # --- morning / evening
    ('أصبحنا وأصبح الملك لله والحمد لله، لا إله إلا الله وحده لا شريك له، له الملك وله الحمد وهو على كل شيء قدير', [0], ''),
    ('أمسينا وأمسى الملك لله والحمد لله، لا إله إلا الله وحده لا شريك له، له الملك وله الحمد وهو على كل شيء قدير', [1], ''),
    ('اللهم بك أصبحنا وبك أمسينا وبك نحيا وبك نموت وإليك النشور', [0], ''),
    ('اللهم بك أمسينا وبك أصبحنا وبك نحيا وبك نموت وإليك المصير', [1], ''),
    ('اللهم أنت ربي لا إله إلا أنت، خلقتني وأنا عبدك، وأنا على عهدك ووعدك ما استطعت، أعوذ بك من شر ما صنعت، أبوء لك بنعمتك علي، وأبوء بذنبي فاغفر لي فإنه لا يغفر الذنوب إلا أنت', [0, 1, 6], 'سيد الاستغفار'),
    ('اللهم ما أصبح بي من نعمة أو بأحد من خلقك فمنك وحدك لا شريك لك، فلك الحمد ولك الشكر', [0], ''),
    ('اللهم ما أمسى بي من نعمة أو بأحد من خلقك فمنك وحدك لا شريك لك، فلك الحمد ولك الشكر', [1], ''),
    ('اللهم عافني في بدني، اللهم عافني في سمعي، اللهم عافني في بصري، لا إله إلا أنت', [0, 1], '٣ مرات'),
    ('اللهم إني أعوذ بك من الكفر والفقر، وأعوذ بك من عذاب القبر، لا إله إلا أنت', [0, 1], '٣ مرات'),
    ('بسم الله الذي لا يضر مع اسمه شيء في الأرض ولا في السماء وهو السميع العليم', [0, 1], '٣ مرات'),
    ('رضيت بالله ربا وبالإسلام دينا وبمحمد صلى الله عليه وسلم نبيا', [0, 1], '٣ مرات'),
    ('حسبي الله لا إله إلا هو عليه توكلت وهو رب العرش العظيم', [0, 1], '٧ مرات'),
    ('أعوذ بكلمات الله التامات من شر ما خلق', [1], '٣ مرات'),
    ('اللهم إني أسألك العفو والعافية في ديني ودنياي وأهلي ومالي، اللهم استر عوراتي وآمن روعاتي، اللهم احفظني من بين يدي ومن خلفي وعن يميني وعن شمالي ومن فوقي، وأعوذ بعظمتك أن أغتال من تحتي', [0, 1], ''),
    ('يا حي يا قيوم برحمتك أستغيث، أصلح لي شأني كله ولا تكلني إلى نفسي طرفة عين', [0, 1], ''),
    ('سبحان الله وبحمده', [0, 1, 6], '١٠٠ مرة'),
    ('لا إله إلا الله وحده لا شريك له، له الملك وله الحمد وهو على كل شيء قدير', [0, 1, 6], '١٠ مرات'),
    # --- after prayer
    ('أستغفر الله، أستغفر الله، أستغفر الله. اللهم أنت السلام ومنك السلام، تباركت يا ذا الجلال والإكرام', [2], ''),
    ('سبحان الله ٣٣ مرة، الحمد لله ٣٣ مرة، الله أكبر ٣٣ مرة. ثم: لا إله إلا الله وحده لا شريك له، له الملك وله الحمد وهو على كل شيء قدير', [2], ''),
    ('اللهم أعني على ذكرك وشكرك وحسن عبادتك', [2, 4], ''),
    ('اللهم أجرني من النار', [2], '٧ مرات بعد الفجر والمغرب'),
    # --- sleep / wake
    ('باسمك اللهم أموت وأحيا', [3], 'عند النوم'),
    ('اللهم قني عذابك يوم تبعث عبادك', [3], 'عند النوم'),
    ('الحمد لله الذي أحيانا بعد ما أماتنا وإليه النشور', [3], 'عند الاستيقاظ'),
    # --- daily
    ('بسم الله توكلت على الله ولا حول ولا قوة إلا بالله', [4], 'عند الخروج من البيت'),
    ('بسم الله ولجنا وبسم الله خرجنا وعلى ربنا توكلنا', [4], 'عند دخول البيت'),
    ('بسم الله', [4], 'قبل الأكل'),
    ('الحمد لله الذي أطعمني هذا ورزقنيه من غير حول مني ولا قوة', [4], 'بعد الأكل'),
    ('اللهم افتح لي أبواب رحمتك', [4], 'عند دخول المسجد'),
    ('اللهم إني أسألك من فضلك', [4], 'عند الخروج من المسجد'),
    ('لا إله إلا الله العظيم الحليم، لا إله إلا الله رب العرش العظيم، لا إله إلا الله رب السماوات ورب الأرض ورب العرش الكريم', [4], 'دعاء الكرب'),
    ('اللهم إني أعوذ بك من الهم والحزن، والعجز والكسل، والبخل والجبن، وضلع الدين وغلبة الرجال', [4], ''),
    ('يا مقلب القلوب ثبت قلبي على دينك', [4], ''),
    ('اللهم إني أسألك علما نافعا ورزقا طيبا وعملا متقبلا', [4], ''),
    # --- quranic duas
    ('ربنا آتنا في الدنيا حسنة وفي الآخرة حسنة وقنا عذاب النار', [5], ''),
    ('ربنا ظلمنا أنفسنا وإن لم تغفر لنا وترحمنا لنكونن من الخاسرين', [5], ''),
    ('رب اشرح لي صدري ويسر لي أمري واحلل عقدة من لساني يفقهوا قولي', [5], ''),
    ('رب زدني علما', [5], ''),
    ('ربنا لا تؤاخذنا إن نسينا أو أخطأنا، ربنا ولا تحمل علينا إصرا كما حملته على الذين من قبلنا، ربنا ولا تحملنا ما لا طاقة لنا به، واعف عنا واغفر لنا وارحمنا، أنت مولانا فانصرنا على القوم الكافرين', [5], ''),
    ('ربنا هب لنا من أزواجنا وذرياتنا قرة أعين واجعلنا للمتقين إماما', [5], ''),
    ('لا إله إلا أنت سبحانك إني كنت من الظالمين', [5, 6], 'دعاء يونس'),
    ('رب اغفر لي ولوالدي وللمؤمنين يوم يقوم الحساب', [5], ''),
    # --- istighfar / dhikr popup pool
    ('أستغفر الله العظيم الذي لا إله إلا هو الحي القيوم وأتوب إليه', [6], ''),
    ('سبحان الله وبحمده، سبحان الله العظيم', [6], ''),
    ('لا حول ولا قوة إلا بالله', [6], ''),
    ('اللهم صل وسلم على نبينا محمد', [6], ''),
    ('أستغفر الله وأتوب إليه', [6], ''),
    ('سبحان الله، والحمد لله، ولا إله إلا الله، والله أكبر', [6], ''),
    ('لا إله إلا الله وحده لا شريك له، له الملك وله الحمد، يحيي ويميت وهو على كل شيء قدير', [6], ''),
    ('اللهم إنك عفو تحب العفو فاعف عني', [6], ''),
    # --- after adhan
    ('اللهم رب هذه الدعوة التامة والصلاة القائمة، آت محمدا الوسيلة والفضيلة، وابعثه مقاما محمودا الذي وعدته', [7], 'دعاء بعد الأذان'),
    # --- Quran
    ('الله لا إله إلا هو الحي القيوم، لا تأخذه سنة ولا نوم، له ما في السماوات وما في الأرض، من ذا الذي يشفع عنده إلا بإذنه، يعلم ما بين أيديهم وما خلفهم، ولا يحيطون بشيء من علمه إلا بما شاء، وسع كرسيه السماوات والأرض، ولا يؤوده حفظهما وهو العلي العظيم', [8], 'آية الكرسي'),
    ('قل هو الله أحد، الله الصمد، لم يلد ولم يولد، ولم يكن له كفوا أحد', [8], 'سورة الإخلاص'),
    ('قل أعوذ برب الفلق، من شر ما خلق، ومن شر غاسق إذا وقب، ومن شر النفاثات في العقد، ومن شر حاسد إذا حسد', [8], 'سورة الفلق'),
    ('قل أعوذ برب الناس، ملك الناس، إله الناس، من شر الوسواس الخناس، الذي يوسوس في صدور الناس، من الجنة والناس', [8], 'سورة الناس'),
]

BODY_SIZE = 15
BODY_LH = 21          # line height px
BODY_W = 152
BODY_H = 108          # max body height px
BODY_PATH = FONT_B


def wrap(text, f, maxw):
    words = text.split()
    lines, cur = [], ''
    for w in words:
        t = (cur + ' ' + w).strip()
        if tlen(f, t) <= maxw * SS or not cur:
            cur = t
        else:
            lines.append(cur); cur = w
    if cur:
        lines.append(cur)
    return lines


def render_page(lines):
    """lines: list of (text, size, bold). Returns 8-bit alpha image BODY_W wide."""
    h = len(lines) * BODY_LH
    big = Image.new('L', (BODY_W * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    for i, (t, size, bold) in enumerate(lines):
        f = font(FONT_B if bold else FONT_R, size * SS)
        w = tlen(f, t)
        x = (BODY_W * SS - w) / 2
        asc, desc = f.getmetrics()
        y = i * BODY_LH * SS + (BODY_LH * SS - (asc + desc)) / 2 - 2 * SS
        d.text((x, y), t, font=f, fill=255, direction='rtl', language='ar')
    return big.resize((BODY_W, h), Image.BOX)


def build_pages():
    f = font(BODY_PATH, BODY_SIZE * SS)
    pages = []   # (img, cats)
    maxlines = BODY_H // BODY_LH
    for text, cats, note in DUAS:
        lines = wrap(text, f, BODY_W - 2)
        note_line = [(note, 13, False)] if note else []
        room = maxlines - len(note_line)
        npg = max(1, -(-len(lines) // room))
        per = -(-len(lines) // npg)          # balanced pages
        if npg > 1:
            pass
            npg = -(-len(lines) // per)
        chunks = [lines[i:i + per] for i in range(0, len(lines), per)] or [[]]
        # try to avoid a lonely single-line last chunk by balancing
        for ci, ch in enumerate(chunks):
            L = [(l, BODY_SIZE, True) for l in ch]
            if ci == 0 and note_line:
                L = note_line + L
            pages.append((render_page(L), cats))
    return pages


# ----------------------------------------------------------------------------
# 3) splash screen
# ----------------------------------------------------------------------------
def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def star8(d, cx, cy, r, color, ratio=0.62, width=0, fill=True):
    pts = []
    for i in range(16):
        ang = math.pi / 8 * i - math.pi / 2
        rr = r if i % 2 == 0 else r * ratio
        pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
    if fill:
        d.polygon(pts, fill=color)
    else:
        d.line(pts + [pts[0]], fill=color, width=width)


def make_splash():
    S = 8
    W, H = SCR_W * S, SCR_H * S
    GOLD = (233, 191, 96)
    GOLD_D = (168, 128, 52)
    CREAM = (245, 236, 210)
    top, bot = (4, 38, 28), (10, 72, 52)
    im = Image.new('RGB', (W, H))
    px = ImageDraw.Draw(im)
    for y in range(H):
        px.line([(0, y), (W, y)], fill=lerp(top, bot, y / H))
    # faint geometric lattice of small 8-point stars
    d = ImageDraw.Draw(im)
    step = 20 * S
    for gy in range(0, H + step, step):
        for gx in range(0, W + step, step):
            off = step // 2 if (gy // step) % 2 else 0
            base = lerp(top, bot, min(1, gy / H))
            star8(d, gx + off, gy, 5.5 * S, lerp(base, (60, 140, 100), 0.16), ratio=0.55, fill=True)

    # frame
    def rect(m, color, w):
        d.rounded_rectangle([m * S, m * S, W - m * S, H - m * S], radius=5 * S, outline=color, width=int(w * S))
    rect(2.5, GOLD, 1.3)
    rect(5.5, GOLD_D, 0.7)
    # corner ornaments
    for (cx, cy, sx, sy) in [(2.5, 2.5, 1, 1), (SCR_W - 2.5, 2.5, -1, 1), (2.5, SCR_H - 2.5, 1, -1), (SCR_W - 2.5, SCR_H - 2.5, -1, -1)]:
        star8(d, cx * S, cy * S, 5.2 * S, GOLD, ratio=0.5)
        d.ellipse([(cx + sx * 10 - 1.1) * S, (cy - 1.1) * S, (cx + sx * 10 + 1.1) * S, (cy + 1.1) * S], fill=GOLD)
        d.ellipse([(cx - 1.1) * S, (cy + sy * 10 - 1.1) * S, (cx + 1.1) * S, (cy + sy * 10 + 1.1) * S], fill=GOLD)
    # top & bottom center ornaments (a star flanked by dots)
    for cy in (2.5, SCR_H - 2.5):
        cx = SCR_W / 2
        d.rectangle([(cx - 11) * S, (cy - 3.2) * S, (cx + 11) * S, (cy + 3.2) * S], fill=(6, 50, 36))
        star8(d, cx * S, cy * S, 4.2 * S, GOLD, ratio=0.5)
        for dx in (-8, 8):
            d.ellipse([(cx + dx - 1) * S, (cy - 1) * S, (cx + dx + 1) * S, (cy + 1) * S], fill=GOLD)

    # crescent
    cx, cy, r = SCR_W / 2 * S, 12.5 * S, 4.6 * S
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GOLD)
    d.ellipse([cx - r + 1.9 * S, cy - r - 0.6 * S, cx + r + 1.9 * S, cy + r - 0.6 * S], fill=lerp(top, bot, 12.5 / SCR_H))
    star8(d, (cx + 3.4 * S), cy - 0.8 * S, 1.8 * S, GOLD, ratio=0.45)

    # Bismillah (with tashkeel), gold with vertical shade
    def text_layer(text, path, px_size, color, y_center, shade=None, maxw=None):
        f = font(path, int(px_size * S))
        w = tlen(f, text)
        if maxw and w > maxw * S:
            f = font(path, int(px_size * S * maxw * S / w)); w = tlen(f, text)
        asc, desc = f.getmetrics()
        mask = Image.new('L', (W, H), 0)
        ImageDraw.Draw(mask).text(((W - w) / 2, y_center * S - (asc + desc) / 2), text, font=f, fill=255, direction='rtl', language='ar')
        col = Image.new('RGB', (W, H), color)
        if shade:
            cd = ImageDraw.Draw(col)
            for y in range(H):
                t = abs(y / S - y_center) / (px_size * 0.8)
                cd.line([(0, y), (W, y)], fill=lerp(color, shade, min(1, t)))
        im.paste(col, (0, 0), mask)

    text_layer('بِسْمِ اللَّهِ الرَّحْمَٰنِ الرَّحِيمِ', FONT_B, 25, (255, 224, 140), 41, shade=GOLD_D, maxw=136)
    # divider
    y = 62 * S
    d.line([(24 * S, y), (SCR_W / 2 * S - 8 * S, y)], fill=GOLD, width=int(0.8 * S))
    d.line([(SCR_W / 2 * S + 8 * S, y), ((SCR_W - 24) * S, y)], fill=GOLD, width=int(0.8 * S))
    d.polygon([(SCR_W / 2 * S, y - 4 * S), (SCR_W / 2 * S + 4 * S, y), (SCR_W / 2 * S, y + 4 * S), (SCR_W / 2 * S - 4 * S, y)], fill=GOLD)
    for dx in (-12, 12):
        d.ellipse([(SCR_W / 2 + dx) * S - S, y - S, (SCR_W / 2 + dx) * S + S, y + S], fill=GOLD)

    # verse (two lines, with tashkeel)
    text_layer('﴿ إِنَّ الصَّلَاةَ كَانَتْ عَلَى', FONT_R, 15, CREAM, 77, maxw=128)
    text_layer('الْمُؤْمِنِينَ كِتَابًا مَّوْقُوتًا ﴾', FONT_R, 15, CREAM, 94, maxw=128)
    text_layer('سورة النساء  ١٠٣', FONT_R, 10, GOLD, 110, maxw=90)

    img = im.resize((SCR_W, SCR_H), Image.LANCZOS)
    return img


def rgb565(img):
    out = []
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = px[x, y][:3]
            out.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return out


# ----------------------------------------------------------------------------
def main():
    prev = []

    # labels
    hdr = ['// AUTO-GENERATED by tools/gen_assets.py - do not edit', '#pragma once', '#include <Arduino.h>',
           '#include "bmp.h"', '']
    body, table, enum = [], [], []
    for idx, (key, text, size, bold) in enumerate(LABELS):
        grp = [t for (_, t, sz, b) in LABELS if sz == size and b == bold]
        im, ss = render_line(text, FONT_B if bold else FONT_R, size, group=grp)
        im = crop_h(downscale(im, ss))
        data = to_nibbles(im)
        body.append(c_array('lbl_%s' % key, data))
        table.append('  {%d,%d,lbl_%s},  // %s' % (im.width, im.height, key, text))
        enum.append('  L_%s = %d,' % (key, idx))
        prev.append((key, im))
    open(os.path.join(OUT, 'assets_labels.h'), 'w', encoding='utf-8').write(
        '\n'.join(hdr) + '\n'.join(body) + '\nenum LabelId : uint8_t {\n' + '\n'.join(enum) + '\n  L_COUNT\n};\n\n'
        'static const AlphaBmp LABELS[] = {\n' + '\n'.join(table) + '\n};\n')

    # duas
    pages = build_pages()
    # titles of categories
    cat_imgs = []
    for c in CATS:
        im, ss = render_line(c, FONT_B, 14, group=CATS)
        cat_imgs.append(crop_h(downscale(im, ss)))
    hdr = ['// AUTO-GENERATED by tools/gen_assets.py - do not edit', '#pragma once', '#include <Arduino.h>', '#include "bmp.h"', '']
    body, table, refs = [], [], []
    for i, im in enumerate(cat_imgs):
        body.append(c_array('cat_%d' % i, to_nibbles(im)))
    for i, (im, cats) in enumerate(pages):
        body.append(c_array('dua_%d' % i, to_nibbles(im)))
        table.append('  {%d,%d,dua_%d},' % (im.width, im.height, i))
        for c in cats:
            refs.append((c, i))
    refs.sort(key=lambda t: (t[0], t[1]))
    catbmp = ['  {%d,%d,cat_%d},' % (im.width, im.height, i) for i, im in enumerate(cat_imgs)]
    txt = '\n'.join(hdr) + '\n'.join(body)
    txt += '\nstatic const AlphaBmp CAT_TITLES[] = {\n' + '\n'.join(catbmp) + '\n};\n'
    txt += '#define CAT_COUNT %d\n' % len(CATS)
    txt += '\nstatic const AlphaBmp DUA_PAGES[] = {\n' + '\n'.join(table) + '\n};\n'
    txt += '\nstruct DuaRef { uint8_t cat; uint16_t page; };\n'
    txt += 'static const DuaRef DUA_REFS[] = {\n' + ',\n'.join('  {%d,%d}' % r for r in refs) + '\n};\n'
    txt += '#define DUA_REF_COUNT %d\n' % len(refs)
    open(os.path.join(OUT, 'assets_duas.h'), 'w', encoding='utf-8').write(txt)

    # splash
    sp = make_splash()
    sp.save(os.path.join(HERE, 'preview_splash.png'))
    sp.resize((SCR_W * 4, SCR_H * 4), Image.NEAREST).save(os.path.join(HERE, 'preview_splash_x4.png'))
    vals = rgb565(sp)
    lines = []
    for i in range(0, len(vals), 16):
        lines.append('  ' + ','.join('0x%04X' % v for v in vals[i:i + 16]) + ',')
    open(os.path.join(OUT, 'assets_splash.h'), 'w').write(
        '// AUTO-GENERATED by tools/gen_assets.py - do not edit\n#pragma once\n#include <Arduino.h>\n'
        'static const uint16_t SPLASH_IMG[%d] PROGMEM = {\n%s\n};\n' % (len(vals), '\n'.join(lines)))

    # previews (decode what we actually stored)
    sheet = Image.new('RGB', (1200, 900), (10, 40, 30))
    x = y = 4
    for key, im in prev:
        q = im.point(lambda v: int(round(((v / 255.0) ** GAMMA) * 15)) * 17)
        tile = Image.new('RGB', q.size, (10, 40, 30)); tile.paste((255, 255, 255), (0, 0), q)
        tile = tile.resize((tile.width * 2, tile.height * 2), Image.NEAREST)
        if x + tile.width > 1190:
            x = 4; y += 80
        sheet.paste(tile, (x, y)); x += tile.width + 8
    sheet.save(os.path.join(HERE, 'preview_labels.png'))
    # duas preview
    n = len(pages)
    cols = 6
    rows = (n + cols - 1) // cols
    sh = Image.new('RGB', (cols * 330, rows * 240), (10, 40, 30))
    for i, (im, cats) in enumerate(pages):
        q = im.point(lambda v: int(round(((v / 255.0) ** GAMMA) * 15)) * 17)
        tile = Image.new('RGB', (160, 128), (8, 50, 36)); tile.paste((255, 255, 255), (4, 18), q)
        tile = tile.resize((320, 256), Image.NEAREST).crop((0, 0, 320, 230))
        sh.paste(tile, ((i % cols) * 330, (i // cols) * 240))
    sh.save(os.path.join(HERE, 'preview_duas.png'))
    print('labels:', len(LABELS), 'pages:', len(pages), 'refs:', len(refs))
    print('bytes (nibbles) approx:', sum((im.width * im.height + 1) // 2 for _, im in prev) + sum((im.width * im.height + 1) // 2 for im, _ in pages))


if __name__ == '__main__':
    main()
