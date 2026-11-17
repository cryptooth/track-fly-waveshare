// Web pages served by the device. Plain HTML/CSS/JS, no external files
// (the setup page must work offline on the device's own WiFi).
#pragma once

#define TF_CSS R"CSS(
:root{--bg:#18181a;--card:#212123;--line:#313134;--tx:#f2efea;--dim:#8e8b86;--or:#ff5a1f;--slot:#3a3a3d}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.45 ui-monospace,"SF Mono",Menlo,Consolas,monospace;display:flex;justify-content:center;padding:28px 16px}
main{width:100%;max-width:460px}
.mark{display:flex;gap:9px;align-items:flex-end;height:30px;margin-bottom:14px}
.mark i{width:11px;height:11px;border-radius:50%;background:var(--tx);opacity:.9}
.mark i:nth-child(1){opacity:.45}.mark i:nth-child(2){opacity:.6}.mark i:nth-child(3){opacity:.75}
.mark i.up{background:var(--or);transform:translateY(-15px);box-shadow:0 0 12px rgba(255,90,31,.7)}
.mark i.slot{background:var(--slot);opacity:1;position:absolute;margin-left:0}
h1{font-size:19px;font-weight:600;margin:0;letter-spacing:.02em}
.sub{color:var(--dim);font-size:12px;margin:2px 0 22px}
section{border-top:1px solid var(--line);padding:16px 0 6px}
h2{font-size:11px;font-weight:600;color:var(--dim);text-transform:uppercase;letter-spacing:.14em;margin:0 0 10px}
label{display:block;font-size:12px;color:var(--dim);margin:12px 0 5px}
input[type=text],input[type=password],select{width:100%;padding:10px 11px;border-radius:7px;border:1px solid var(--line);background:var(--card);color:var(--tx);font:inherit;font-size:15px}
input:focus,select:focus{outline:none;border-color:var(--or)}
.chk{display:flex;gap:9px;align-items:center;color:var(--tx);font-size:14px;margin-top:12px}
.chk input{accent-color:var(--or);width:17px;height:17px}
.seg{display:flex;gap:6px}.seg button{flex:1;margin:0;padding:9px 0;background:var(--card);border:1px solid var(--line);color:var(--dim);font-weight:500}
.seg button.on{border-color:var(--or);color:var(--tx)}
button{width:100%;margin-top:16px;padding:12px;border:0;border-radius:7px;background:var(--or);color:#18181a;font:inherit;font-weight:700;font-size:15px;cursor:pointer}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--tx);font-weight:500}
button.warn{background:transparent;border:1px solid #5a2c22;color:#ff9b80;font-weight:500}
.now{background:var(--card);border-radius:9px;padding:12px 13px;font-size:13px}
.now b{font-size:15px;display:block;color:var(--tx);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.now .k{color:var(--dim)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:3px 12px;font-size:12px;margin-top:10px}
.kv span:nth-child(odd){color:var(--dim)}
.st{min-height:18px;font-size:12px;color:var(--or);margin-top:8px;text-align:center}
.net{display:flex;justify-content:space-between;padding:10px 11px;border:1px solid var(--line);border-radius:7px;margin-bottom:6px;cursor:pointer;background:var(--card)}
.net.sel{border-color:var(--or)}.net small{color:var(--dim)}
.hint{font-size:12px;color:var(--dim);margin:6px 0 0}
)CSS"

#define TF_MARK "<div class=mark><i></i><i></i><i></i><i></i><i class=up></i><i></i><i></i></div>"

// ---------------- Setup page (device's own WiFi) ----------------
static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Track Fly display setup</title>
<style>)HTML" TF_CSS R"HTML(</style></head><body><main>
)HTML" TF_MARK R"HTML(
<h1>Track Fly display</h1><p class=sub>Setup &middot; v__FW__</p>
<section><h2>Home WiFi</h2>
<div id=nets><p class=hint>Scanning&hellip;</p></div>
<button class=ghost onclick=scan()>Scan again</button>
<label>Network name</label><input type=text id=ssid autocomplete=off>
<label>Password</label><input type=password id=pass>
</section>
<section><h2>Track Fly</h2>
<label>Address of the Mac running Track Fly</label><input type=text id=feed value="__FEED__">
<p class=hint>Shown in Track Fly's menu, e.g. http://192.168.1.20:8765</p>
<button onclick=save()>Save and connect</button><div class=st id=st></div>
</section></main>
<script>
const $=i=>document.getElementById(i);
async function scan(){
 $('nets').innerHTML='<p class=hint>Scanning&hellip;</p>';
 try{const r=await fetch('/scan');const l=await r.json();
  const seen={};$('nets').innerHTML='';
  l.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{if(!n.ssid||seen[n.ssid])return;seen[n.ssid]=1;
   const d=document.createElement('div');d.className='net';
   d.innerHTML='<span></span><small>'+(n.lock?'locked ':'')+n.rssi+' dBm</small>';
   d.firstChild.textContent=n.ssid;
   d.onclick=()=>{document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));d.classList.add('sel');$('ssid').value=n.ssid;$('pass').focus();};
   $('nets').appendChild(d);});
  if(!l.length)$('nets').innerHTML='<p class=hint>No networks found. Type the name below.</p>';
 }catch(e){$('nets').innerHTML='<p class=hint>Scan failed. Type the name below.</p>';}
}
async function save(){
 if(!$('ssid').value){$('st').textContent='Pick a network';return;}
 const b=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value,feed:$('feed').value});
 $('st').textContent='Saved. The device is connecting, watch its screen.';
 try{await fetch('/wifi',{method:'POST',body:b});}catch(e){}
}
scan();
</script></body></html>)HTML";

// ---------------- Control page (home network) ----------------
static const char CONTROL_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Track Fly display</title>
<style>)HTML" TF_CSS R"HTML(</style></head><body><main>
)HTML" TF_MARK R"HTML(
<h1>Track Fly display</h1><p class=sub id=sub>&nbsp;</p>

<section><h2>Now</h2>
<div class=now><span class=k id=nst>&ndash;</span><b id=ntt>&nbsp;</b><span id=nar></span>
<div class=kv><span>battery</span><span id=bat></span><span>wifi</span><span id=wifi></span><span>address</span><span id=addr></span></div></div>
</section>

<section><h2>Feed</h2>
<label>Track Fly address</label><input type=text id=feed placeholder="http://192.168.1.20:8765">
<label>Follow</label><select id=follow></select>
<p class=hint>Auto shows whatever is playing. Pick a Roon zone or a player to stick to it. The same list is on the device: tap, then the &lt; &gt; corners.</p>
</section>

<section><h2>Display</h2>
<label>Brightness</label><div class=seg id=bright></div>
<label>Text scroll speed</label><div class=seg id=speed></div>
<label>Show on screen</label>
<label class=chk><input type=checkbox data-bit=2> Source and zone (top line)</label>
<label class=chk><input type=checkbox data-bit=64> Track position (3/12, playlist 14/52, tracks left)</label>
<label class=chk><input type=checkbox data-bit=1> Album after the artist</label>
<label class=chk><input type=checkbox data-bit=4> Progress dots</label>
<label class=chk><input type=checkbox data-bit=8> Elapsed / total time</label>
<label class=chk><input type=checkbox data-bit=16> Battery</label>
<label class=chk><input type=checkbox data-bit=32> Clock when nothing plays (off: screen goes dark, a touch lights it)</label>
<label>Orientation</label><select id=rotate><option value=2>Automatic (turn the device over)</option><option value=0>Normal</option><option value=1>Upside down</option></select>
<label class=chk><input type=checkbox id=sleep> On battery, sleep when nothing plays</label>
<div class=seg style="align-items:center;margin-top:8px;color:var(--dim);font-size:13px">after <select id=sleepm style="width:auto;flex:0 0 auto;padding:6px 8px"><option>2</option><option>5</option><option>15</option><option>30</option><option>60</option></select> minutes</div>
<p class=hint>Saves the battery (the clock is not shown then). It checks every 2 minutes and wakes when music starts; BOOT or PWR wakes it at once. Plugged into a computer it never sleeps. On a USB wall charger the display cannot tell it is not on battery, so turn this off there.</p>
<label class=chk><input type=checkbox id=night> Night mode</label>
<div class=seg style="align-items:center;margin-top:8px"><input type=time id=nfrom style="flex:1;padding:9px;border-radius:7px;border:1px solid var(--line);background:var(--card);color:var(--tx);font:inherit"><span style="color:var(--dim)">&ndash;</span><input type=time id=nto style="flex:1;padding:9px;border-radius:7px;border:1px solid var(--line);background:var(--card);color:var(--tx);font:inherit"></div>
<label>At night the screen</label><select id=nmode><option value=0>dims to the lowest step (25%)</option><option value=1>goes dark (a touch lights it for 15 s)</option></select>
<label>Time zone</label><select id=tz></select>
</section>

<section><h2>Device</h2>
<label>Name (opens as name.local)</label><input type=text id=name maxlength=31>
<label>New web password (blank keeps the current one, user is admin)</label><input type=password id=wpass maxlength=31>
<label class=chk><input type=checkbox id=nopass> Remove the password</label>
<button onclick=save()>Save</button><div class=st id=st></div>
</section>

<section><h2>Firmware</h2>
<p class=hint>Running v<span id=fw></span>. Track Fly on your Mac offers new versions by itself. To install another one, download it from <a href="https://trackfly.stream/firmware/" target=_blank style="color:var(--or)">trackfly.stream/firmware</a> and pick the .bin here. Settings are kept.</p>
<label>&nbsp;</label><input type=file id=fwf accept=.bin style="color:var(--dim);font:inherit;font-size:13px">
<button class=ghost onclick=fwup()>Install update</button><div class=st id=fwst></div>
</section>

<section><h2>Reset</h2>
<button class=ghost onclick="act('/forget','Forget the WiFi network and restart in setup mode?')">Forget WiFi</button>
<button class=warn onclick="act('/factory','Erase every setting and restart?')">Factory reset</button>
</section></main>
<script>
const $=i=>document.getElementById(i);let BR=3,SP=1,first=true;
const m2t=m=>('0'+(m/60|0)).slice(-2)+':'+('0'+(m%60)).slice(-2);
const t2m=s=>{const a=(s||'0:0').split(':');return (+a[0])*60+(+a[1]);};
const BITS=[...document.querySelectorAll('[data-bit]')];
['Slow','Normal','Fast'].forEach((t,i)=>{const b=document.createElement('button');b.textContent=t;b.onclick=()=>{SP=i;seg();};$('speed').appendChild(b);});
const TZ=[[-28800,'UTC-8'],[-18000,'UTC-5'],[-10800,'UTC-3'],[0,'UTC+0'],[3600,'UTC+1'],[7200,'UTC+2'],[10800,'UTC+3 Türkiye'],[14400,'UTC+4'],[19800,'UTC+5:30'],[28800,'UTC+8'],[32400,'UTC+9'],[36000,'UTC+10']];
TZ.forEach(([s,l])=>{const o=new Option(l,s);$('tz').add(o);});
['25%','50%','75%','100%'].forEach((t,i)=>{const b=document.createElement('button');b.textContent=t;b.onclick=()=>{BR=i;seg();};$('bright').appendChild(b);});
function seg(){[...$('bright').children].forEach((b,i)=>b.classList.toggle('on',i==BR));[...$('speed').children].forEach((b,i)=>b.classList.toggle('on',i==SP));}
const ST={playing:'playing',paused:'paused',stopped:'stopped',not_running:'player closed'};
async function poll(){
 try{const j=await (await fetch('/api/state')).json();
  $('sub').textContent='v'+j.fw+' · '+j.name+'.local';$('fw').textContent=j.fw;
  const n=j.now;$('nst').textContent=n.reachable?(ST[n.state]||n.state||'–')+(n.source?' · '+n.source:'')+(n.zone?' · '+n.zone:''):'Track Fly not reachable';
  $('ntt').textContent=n.title||n.text||' ';$('nar').textContent=n.artist||'';
  $('bat').textContent=j.bat+'% · '+j.volts.toFixed(2)+' V'+(j.charging?' · charging':'');
  $('wifi').textContent=j.ssid+' · '+j.rssi+' dBm';$('addr').textContent=j.ip;
  if(first){first=false;
   $('feed').value=j.feed;$('name').value=j.name;$('rotate').value=j.rotate;$('night').checked=j.night;$('nfrom').value=m2t(j.nightFrom);$('nto').value=m2t(j.nightTo);$('nmode').value=j.nightMode;$('sleep').checked=j.sleep;$('sleepm').value=j.sleepMin;
   $('tz').value=j.tz;BR=j.bright;SP=j.speed;seg();
   BITS.forEach(c=>c.checked=(j.show&+c.dataset.bit)!=0);
   const s=$('follow');s.add(new Option('Auto (whatever is playing)','|'));
   (j.targets||[]).forEach(t=>s.add(new Option(t.label,t.source+'|'+t.zone)));
   const cur=j.source+'|'+j.zone;
   if(![...s.options].some(o=>o.value==cur))s.add(new Option((j.zone?'Roon · '+j.zone:j.source)+' (not seen now)',cur));
   s.value=cur;
   $('wpass').placeholder=j.hasPass?'••••••':'no password set';}
 }catch(e){$('nst').textContent=first?'Device not reachable':'Page error: '+e.message;}
}
async function save(){
 const fv=$('follow').value.split('|');
 const b=new URLSearchParams({feed:$('feed').value,source:fv[0],zone:fv.slice(1).join('|'),
  bright:BR,speed:SP,show:BITS.reduce((m,c)=>c.checked?m|+c.dataset.bit:m,0xFF80),rotate:$('rotate').value,night:$('night').checked?1:0,nightFrom:t2m($('nfrom').value),nightTo:t2m($('nto').value),nightMode:$('nmode').value,sleep:$('sleep').checked?1:0,sleepMin:$('sleepm').value,tz:$('tz').value,name:$('name').value});
 if($('wpass').value)b.append('wpass',$('wpass').value);
 if($('nopass').checked)b.append('nopass','1');
 $('st').textContent='Saving…';
 try{const r=await fetch('/api/settings',{method:'POST',body:b});$('st').textContent=r.ok?'Saved':'Error: '+await r.text();
  $('wpass').value='';$('nopass').checked=false;}catch(e){$('st').textContent='Connection error';}
}
function fwup(){
 const f=$('fwf').files[0],st=$('fwst');
 if(!f){st.textContent='Choose a .bin file first';return;}
 if(!confirm('Install '+f.name+'? The device restarts afterwards.'))return;
 const fd=new FormData();fd.append('fw',f,f.name);const x=new XMLHttpRequest();x.open('POST','/update');
 x.upload.onprogress=e=>{if(e.lengthComputable)st.textContent='Uploading '+Math.round(e.loaded*100/e.total)+'%';};
 x.onload=()=>{if(x.status==200){st.textContent='Done. Restarting, this page reloads in 15 s';setTimeout(()=>location.reload(),15000);}else st.textContent='Failed: '+x.responseText;};
 x.onerror=()=>{st.textContent='Connection lost. If it reached 100%, wait and reload.';};
 st.textContent='Starting…';x.send(fd);
}
async function act(u,q){if(!confirm(q))return;$('st').textContent='Restarting…';try{await fetch(u,{method:'POST'});}catch(e){}}
poll();setInterval(poll,3000);
</script></body></html>)HTML";
