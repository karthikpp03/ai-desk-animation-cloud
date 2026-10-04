const $ = id => document.getElementById(id);
const DEFAULT_API_BASE = '';  // set your Worker URL in the page's Connection settings (saved in this browser)
const savedBase = localStorage.getItem('animationApiBase') || DEFAULT_API_BASE;
// Prototype convenience: remember the admin token on this device (localStorage) so it is typed only once.
const savedToken = localStorage.getItem('animationAdminToken') || sessionStorage.getItem('animationAdminToken') || '';
$('apiBase').value = savedBase;
$('adminToken').value = savedToken;

function apiBase(){ return ($('apiBase').value || '').trim().replace(/\/$/, ''); }
function headers(){ const h={}; const t=$('adminToken').value.trim(); if(t) h['X-Admin-Token']=t; return h; }
async function api(path, options={}){
  const res=await fetch(apiBase()+path,{...options,headers:{...headers(),...(options.headers||{})}});
  const type=res.headers.get('content-type')||'';
  const data=type.includes('json')?await res.json():await res.arrayBuffer();
  if(!res.ok) throw new Error(data?.error || `HTTP ${res.status}`);
  return data;
}
function msg(text, bad=false){ $('message').textContent=text; $('message').style.color=bad?'#ef8d8d':''; }
async function loadAnimations(){
  if(!apiBase()){msg('Set the Worker API URL first.',true);return;}
  const data=await api('/api/animations'); const root=$('animations'); root.innerHTML='';
  $('count').textContent=`${data.animations.length}`; $('empty').style.display=data.animations.length?'none':'block';
  for(const a of data.animations){
    const card=document.createElement('article'); card.className='anim card';
    card.innerHTML=`<h3 title="${escapeHtml(a.name)}">${escapeHtml(a.name)}</h3><div class="meta">${a.frames} frames · ${a.fps} FPS · ${a.frameDurationMs} ms</div><div class="actions"><button class="play">▶ Play</button></div>`;
    card.querySelector('.play').onclick=()=>play(a.id,a.name);
    root.appendChild(card);
  }
}
function escapeHtml(s){return String(s).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));}
async function play(id,name){ try{await api(`/api/animations/${encodeURIComponent(id)}/play`,{method:'POST'});msg(`Play command sent: ${name}`);}catch(e){msg(e.message,true)} }
async function status(){try{const s=await api('/api/device/status');$('deviceStatus').textContent=s.online?`ESP32 online${s.currentAnimationId?' · '+s.currentAnimationId:''}`:'ESP32 offline';$('dot').parentElement.className='status '+(s.online?'online':'offline')}catch(e){$('deviceStatus').textContent='API unavailable';$('dot').parentElement.className='status offline'}}
async function failures(){try{const d=await api('/api/import-failures');$('failures').innerHTML=d.failures.length?d.failures.map(f=>`<div class="failure"><strong>${escapeHtml(f.filename)}</strong><span>${escapeHtml(f.reason)}</span><br><small>${escapeHtml(f.createdAt)}</small></div>`).join(''):'<div class="empty">No failed imports.</div>'}catch(e){$('failures').innerHTML='<div class="empty">Could not load failures.</div>'}}
$('fileInput').onchange=async e=>{
  const file=e.target.files[0];if(!file)return;
  msg('Converting .ino…');const fd=new FormData();fd.append('file',file);
  try{
    const d=await api('/api/animations/upload',{method:'POST',body:fd});
    msg(d.duplicate?`Already exists: ${d.animation.name}`:`Imported ${d.animation.name} (${d.animation.frames} frames${d.aiFallback?' · AI fallback':''}).`);
    await loadAnimations();await failures();
  }catch(err){msg(err.message,true);await failures()}finally{e.target.value=''}
};
$('slideshowBtn').onclick=async()=>{try{await api('/api/slideshow/start',{method:'POST'});msg('Slideshow started.')}catch(e){msg(e.message,true)}};
$('stopBtn').onclick=async()=>{try{await api('/api/animations/stop',{method:'POST'});msg('Stop command sent.')}catch(e){msg(e.message,true)}};
$('refreshBtn').onclick=async()=>{try{await loadAnimations();await failures();await status();msg('Refreshed.')}catch(e){msg(e.message,true)}};
$('saveSettings').onclick=()=>{localStorage.setItem('animationApiBase',apiBase());localStorage.setItem('animationAdminToken',$('adminToken').value.trim());msg('Settings saved.');loadAnimations();status();failures()};
if(savedBase){loadAnimations();failures();status();setInterval(status,5000)}else msg('Set the Worker API URL in Connection settings.');
