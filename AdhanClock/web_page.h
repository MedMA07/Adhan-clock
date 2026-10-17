#pragma once
#include <Arduino.h>
// Settings page served by the ESP32 (Arabic, RTL, mobile first)
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="ar" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ساعة الأذان</title>
<style>
:root{--g:#0b4a33;--g2:#06301f;--au:#e9bf60;--bg:#f3f1e8;--tx:#1b2b24}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,Tahoma,sans-serif;background:var(--bg);color:var(--tx);line-height:1.5}
header{background:linear-gradient(var(--g),var(--g2));color:var(--au);padding:18px 16px;text-align:center;font-size:20px;font-weight:700}
header small{display:block;color:#cfe5da;font-size:13px;font-weight:400}
main{max-width:640px;margin:auto;padding:12px}
section{background:#fff;border-radius:14px;padding:14px;margin:12px 0;box-shadow:0 1px 4px #0002}
h2{margin:0 0 10px;font-size:16px;color:var(--g)}
label{display:block;font-size:14px;margin:10px 0 3px}
input,select,button{font:inherit;width:100%;padding:10px;border:1px solid #bbb;border-radius:10px;background:#fff}
input[type=checkbox]{width:auto;margin-left:8px}
.row{display:flex;gap:8px}.row>*{flex:1}
button{background:var(--g);color:#fff;border:0;font-weight:700;cursor:pointer}
button.sec{background:#e5e2d3;color:var(--tx)}button.red{background:#a33}
.chk{display:flex;align-items:center;margin:10px 0}.chk label{margin:0}
#st{font-size:14px;background:#eef6f1;border-radius:10px;padding:10px}
#msg{position:sticky;bottom:8px;background:var(--g2);color:#fff;border-radius:10px;padding:10px;text-align:center;display:none}
.hint{font-size:12px;color:#666}
</style></head><body>
<header>ساعة الأذان<small>اعدادات الجهاز</small></header><main>
<section><h2>الحالة</h2><div id="st">...</div>
<div class="row" style="margin-top:8px"><button class="sec" onclick="act('time')">ضبط الوقت من الهاتف</button><button class="sec" onclick="act('fetch')">تحديث المواقيت الآن</button></div></section>

<section><h2>الاتصال بالإنترنت (Wi-Fi)</h2>
<label>اسم الشبكة</label><div class="row"><input id="ssid" list="nets" autocomplete="off"><button class="sec" style="flex:0 0 90px" onclick="scan()">بحث</button></div>
<datalist id="nets"></datalist>
<label>كلمة السر (اتركها فارغة اذا لم تتغير)</label><input id="pass" type="password" autocomplete="new-password">
<label>كلمة سر شبكة الجهاز (8 احرف فأكثر)</label><input id="apPass"></section>

<section><h2>المدينة والمواقيت</h2>
<div class="row"><div><label>المدينة (بالانجليزية)</label><input id="city"></div><div><label>الدولة (بالانجليزية)</label><input id="country"></div></div>
<label>طريقة الحساب</label><select id="method">
<option value="19">الجزائر - وزارة الشؤون الدينية</option><option value="21">المغرب</option><option value="18">تونس</option>
<option value="5">مصر</option><option value="4">ام القرى (مكة)</option><option value="3">رابطة العالم الاسلامي</option>
<option value="2">امريكا الشمالية ISNA</option><option value="12">فرنسا UOIF</option><option value="1">كراتشي</option>
<option value="13">تركيا (ديانت)</option><option value="8">الخليج</option><option value="9">الكويت</option><option value="10">قطر</option>
<option value="16">دبي</option><option value="17">ماليزيا</option><option value="11">سنغافورة</option></select>
<div class="row"><div><label>المذهب (العصر)</label><select id="school"><option value="0">الجمهور (الشافعي)</option><option value="1">الحنفي</option></select></div>
<div><label>فرق التوقيت UTC</label><input id="tz" type="number" step="0.5"></div>
<div><label>تعديل الهجري (يوم)</label><select id="hadj"><option>-2</option><option>-1</option><option selected>0</option><option>1</option><option>2</option></select></div></div>
<div class="row"><div><label>خط العرض (للحساب دون انترنت)</label><input id="lat" type="number" step="0.0001"></div><div><label>خط الطول</label><input id="lng" type="number" step="0.0001"></div></div>
<label>تعديل دقائق (+/-) : الفجر / الظهر / العصر / المغرب / العشاء</label>
<div class="row"><input id="t0" type="number"><input id="t1" type="number"><input id="t2" type="number"><input id="t3" type="number"><input id="t4" type="number"></div></section>

<section><h2>التنبيهات</h2>
<div class="chk"><input type="checkbox" id="beep"><label for="beep">تشغيل الصوت (Buzzer)</label></div>
<div class="chk"><input type="checkbox" id="adhan"><label for="adhan">تنبيه عند دخول وقت الصلاة</label></div>
<label>تنبيه قبل الصلاة بـ (دقيقة، 0 = ايقاف)</label><input id="pre" type="number" min="0" max="60">
<label>تذكير بالاستغفار كل (دقيقة، 0 = ايقاف)</label><input id="istg" type="number" min="0" max="720">
<div class="row"><div><label>صمت الاستغفار من الساعة</label><input id="qfrom" type="number" min="0" max="23"></div><div><label>الى الساعة</label><input id="qto" type="number" min="0" max="23"></div></div>
<div class="chk"><input type="checkbox" id="morn"><label for="morn">أذكار الصباح</label></div>
<label>بعد الفجر بـ (دقيقة)</label><input id="mornOff" type="number" min="0" max="240">
<div class="chk"><input type="checkbox" id="eve"><label for="eve">أذكار المساء</label></div>
<label>بعد العصر بـ (دقيقة)</label><input id="eveOff" type="number" min="0" max="240">
<div class="chk"><input type="checkbox" id="sleep"><label for="sleep">أذكار النوم</label></div>
<label>الوقت</label><input id="sleepT" type="time">
<div class="chk"><input type="checkbox" id="kahf"><label for="kahf">تذكير سورة الكهف يوم الجمعة</label></div>
<label>الوقت</label><input id="kahfT" type="time"></section>

<section><div class="row"><button onclick="save()">حفظ</button><button class="sec" onclick="act('test')">تجربة الصوت</button></div>
<div class="row" style="margin-top:8px"><button class="sec" onclick="act('reboot')">اعادة تشغيل</button><button class="red" onclick="if(confirm('مسح كل الاعدادات؟'))act('reset')">مسح الاعدادات</button></div>
<p class="hint">ملاحظة: كل من يتصل بشبكتك المنزلية يمكنه فتح هذه الصفحة.</p></section>
</main><div id="msg"></div>
<p style="text-align:center;opacity:.65;font-size:.8em;margin:18px 0">Adhan Clock v1 · by El-Amine (MedMA07) · <a href="https://github.com/MedMA07" style="color:inherit">github.com/MedMA07</a></p>
<script>
const $=i=>document.getElementById(i);
const K=['ssid','apPass','city','country','method','school','tz','hadj','lat','lng','pre','istg','qfrom','qto','mornOff','eveOff','sleepT','kahfT','t0','t1','t2','t3','t4'];
const NUM=['method','school','tz','hadj','lat','lng','pre','istg','qfrom','qto','mornOff','eveOff','t0','t1','t2','t3','t4'];
const B=['beep','adhan','morn','eve','sleep','kahf'];
function say(t){const m=$('msg');m.textContent=t;m.style.display='block';clearTimeout(say.t);say.t=setTimeout(()=>m.style.display='none',3500)}
async function load(){try{const s=await (await fetch('/api/get')).json();K.forEach(k=>{if(k in s)$(k).value=s[k]});B.forEach(k=>$(k).checked=!!s[k])}catch(e){say('تعذر التحميل')}}
async function save(){const o={};K.forEach(k=>o[k]=NUM.includes(k)?Number($(k).value):$(k).value);B.forEach(k=>o[k]=$(k).checked);o.pass=$('pass').value;
 try{const r=await fetch('/api/set',{method:'POST',body:JSON.stringify(o)});say(r.ok?'تم الحفظ ✔':'خطأ في الحفظ')}catch(e){say('تعذر الحفظ')}}
async function act(a){try{const u=a=='time'?'/api/time?e='+Math.floor(Date.now()/1000):'/api/'+a;const r=await fetch(u,{method:'POST'});say(r.ok?'تم ✔':'خطأ')}catch(e){say('تم / انقطع الاتصال')}}
async function scan(){say('جاري البحث...');try{const a=await (await fetch('/api/scan')).json();$('nets').innerHTML=a.map(n=>'<option value="'+n.replace(/"/g,'&quot;')+'">').join('');say('تم العثور على '+a.length+' شبكة - اضغط على الخانة لاختيارها')}catch(e){say('فشل البحث')}}
async function stat(){try{const s=await (await fetch('/api/status')).json();
 $('st').innerHTML='الوقت: <b>'+s.time+'</b><br>الانترنت: <b>'+(s.wifi?'متصل ('+s.ip+')':'غير متصل')+'</b><br>مصدر المواقيت: <b>'+s.src+'</b><br>'+s.last}catch(e){}}
load();stat();setInterval(stat,5000);
</script></body></html>)HTML";
