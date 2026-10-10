const $=id=>document.getElementById(id);
let deviceToken='', unlocked=false, pending=0, activeView='automations', loaded={}, configSnapshot=null, editIndex=-1;
let authWaiting=false, authTimer=0, authAttempt=0;
let caps={sources:[], actions:[], gpio_profiles:[], hat_sources:[]};
function banner(kind, msg) { $('banner').className='banner '+(kind||'info'); $('banner').textContent=msg||''; }
function dump(id, d) { $(id).textContent=typeof d==='string'?d:JSON.stringify(d, null, 2)||'Operation complete.'; }
function controls() {
  document.querySelectorAll('button').forEach(b=>b.disabled=b.hasAttribute('popovertarget')?false:pending>0||(!unlocked&&b.id!=='request_access'));
  $('request_access').disabled=pending>0||authWaiting;
  $('cancel_access').disabled=pending>0||!deviceToken;
  $('cancel_access').hidden=!deviceToken;
  $('new_automation').disabled=pending>0||!unlocked||(configSnapshot&&configSnapshot.rules.length>=8);
}
function busy(on) { pending += on?1:-1; controls(); }
function session(on) {
  unlocked=on;
  $('auth_panel').hidden=on;
  $('workspace').hidden=!on;
  document.body.classList.toggle('auth',!on);
  document.querySelectorAll('.controls').forEach(f=>f.disabled=!on);
  if(on&&$('connection_pill').textContent==='Locked') $('connection_pill').textContent='Approved';
  if(!on) { authMessage('Request access to continue.'); ++authAttempt; clearTimeout(authTimer); authWaiting=false; try { sessionStorage.removeItem('stick-auth'); } catch(e) {} deviceToken=''; loaded={}; configSnapshot=null; if($('rule_editor').open) $('rule_editor').close(); renderRules(); ['wifi_password','ap_password','http_bearer_token','config_json'].forEach(id=>$(id).value=''); $('connection_pill').textContent='Locked'; }
  controls();
}
async function api(u, o, token=deviceToken) {
  if(!/^[a-f0-9]{32}$/.test(token)||(!unlocked&&u!=='/api/auth')) throw new Error('Device approval required');
  busy(true);
  try {
    const r=await fetch(u, Object.assign({headers:{'Content-Type':'application/json', 'X-Device-Token':token}, signal:AbortSignal.timeout(20000)}, o||{}));
    if(r.status===401&&token===deviceToken) session(false);
    const t=await r.text();
    let d;
    try { d=t?JSON.parse(t):{}; } catch (e) { throw new Error('Invalid device response. Refresh to retry.'); }
    if(!r.ok||d.error||d.ok===false) throw new Error(d.error||d.reason||r.statusText||'Operation failed');
    return d;
  } finally { busy(false); }
}
async function op(task, msg) {
  const view=activeView;
  banner('info', msg||'Working…');
  try { const d=await task(); dump('op_result', d); banner('ok', 'Operation complete'); return d; }
  catch (e) { const msg=e.name==='TimeoutError'?'Device did not respond. Check your connection and refresh.':e.message; banner('err', msg); dump('op_result', {ok:false, error:msg}); return null; }
  finally { if(unlocked&&view!==activeView&&!loaded[activeView]&&!pending) op(refresh, 'Loading…'); }
}
function opt(sel, v, t) { const o=document.createElement('option'); o.value=v; o.textContent=t||v; sel.appendChild(o); }
function fill() {
  ['source','action','gpio_profile','hat_source'].forEach(i=>$(i).replaceChildren());
  (caps.sources||[]).forEach(v=>opt($('source'), v));
  (caps.actions||[]).forEach(v=>opt($('action'), v));
  (caps.gpio_profiles||[]).forEach(g=>opt($('gpio_profile'), g.name, g.supported===false?g.name+' (disabled)':g.name));
  (caps.hat_sources||[]).forEach(h=>opt($('hat_source'), h.name, h.name+(h.supported?' (present)':' ('+h.reason+')')));
}
async function capabilities() { if(!caps.sources.length) { caps=await api('/api/capabilities'); fill(); dump('capabilities', caps); } }
function showNetPanel(n) {
  ['wifi','ap','net_status'].forEach(p=>{ $('panel_'+p).hidden=p!==n; $('tab_'+p).className=p===n?'selected':''; $('tab_'+p).setAttribute('aria-pressed', String(p===n)); });
}
function thresholdBody(b) {
  const v=$('threshold').value.trim();
  if(v==='true'||v==='false') { b.threshold_kind='bool'; b.threshold_bool=v==='true'; }
  else { if(!/^-?\d+$/.test(v)||+v<-2147483648||+v>2147483647) throw new Error('Threshold must be true, false, or a 32-bit whole number.'); b.threshold_kind='i32'; b.threshold_i32=+v; }
}
function body() {
  const b={enabled:$('rule_enabled').checked, name:$('rule_name').value, source:$('source').value, action:$('action').value, comparator:$('comparator').value, http_url:$('http_url').value};
  if($('http_bearer_token').value) b.http_bearer_token=$('http_bearer_token').value;
  thresholdBody(b);
  if(b.source.startsWith('gpio.')) Object.assign(b, {gpio_pin:+$('gpio_pin').value, gpio_profile:$('gpio_profile').value, gpio_debounce_ms:+$('gpio_debounce_ms').value, gpio_active_low:$('gpio_active_low').checked});
  return JSON.stringify(b);
}
function ruleFields() { $('http_fields').hidden=$('action').value!=='http_post'; $('gpio_fields').hidden=!$('source').value.startsWith('gpio.'); }
function friendly(value) { return String(value||'—').replace(/[._]/g, ' '); }
function renderRules() {
  const root=$('rule_list'); root.replaceChildren();
  const rules=configSnapshot?configSnapshot.rules:[];
  $('rule_count').textContent=configSnapshot?rules.length+' of 8 automations':'Waiting for device approval.';
  if(!rules.length) { const p=document.createElement('p'); p.className='empty'; p.textContent=configSnapshot?'No automations yet. Create your first rule.':'Waiting for device approval.'; root.appendChild(p); }
  rules.forEach((r, i)=>{
    const row=document.createElement('button'); row.className='rule-row'; row.type='button';
    const count=r.action_count||(r.actions||[]).length;
    const values=[r.name||'Untitled automation', friendly((r.source)), friendly((r.actions||[])[0]?.action)+(count>1?' +'+(count - 1):''), r.enabled?'Enabled':'Disabled'];
    values.forEach((v, n)=>{ const cell=document.createElement('span'); cell.className=n===0?'rule-name':n===3?'pill':'rule-meta'; cell.textContent=v; row.appendChild(cell); });
    row.onclick=()=>op(()=>loadEditor(r.id), 'Opening automation…');
    root.appendChild(row);
  });
  controls();
}
function applyConfig(c) { configSnapshot=c; if(!Array.isArray(c.rules)) c.rules=[]; renderRules(); }
async function loadEditor(id) {
  await capabilities(); applyConfig(await api('/api/config'));
  const index=id===undefined?-1:configSnapshot.rules.findIndex(r=>r.id===id);
  if(id!==undefined&&index<0) throw new Error('This automation was removed. Refresh the list.');
  openEditor(index);
}
function openEditor(index) {
  if(!configSnapshot) throw new Error('Refresh automations first.');
  if(index<0&&configSnapshot.rules.length>=8) throw new Error('The device supports up to 8 automations.');
  editIndex=index;
  const c=configSnapshot.rules[index]||{}, a=(c.actions||[])[0]||{};
  $('editor_title').textContent=index<0?'New automation':'Edit automation';
  for(const [id, value] of [['source', c.source], ['action', a.action]]) {
    if(value&&![...$(id).options].some(o=>o.value===value)) opt($(id), value);
    $(id).value=value||($(id).options[0]||{}).value||'';
  }
  $('rule_name').value=c.name||'';
  $('rule_enabled').checked=c.enabled!==false;
  $('comparator').value=c.comparator||'eq';
  $('threshold').value=c.threshold_kind==='i32'?String(c.threshold_i32||0):String(c.threshold_bool!==false);
  $('gpio_pin').value=c.gpio_pin===undefined?4:c.gpio_pin;
  if(c.gpio_profile) $('gpio_profile').value=c.gpio_profile;
  $('gpio_active_low').checked=!!c.gpio_active_low;
  $('gpio_debounce_ms').value=c.gpio_debounce_ms===undefined?20:c.gpio_debounce_ms;
  $('http_url').value=a.http_url||'';
  $('http_bearer_token').value='';
  ruleFields(); $('rule_editor').showModal(); $('rule_name').focus();
}
function editedConfig() {
  const patch=JSON.parse(body());
  if(!patch.name.trim()) throw new Error('Enter an automation name.');
  if(!patch.source||!patch.action) throw new Error('Choose a trigger and an action.');
  const rules=JSON.parse(JSON.stringify(configSnapshot.rules));
  const old=rules[editIndex], first=old&&old.actions[0];
  const action=first&&first.action===patch.action?Object.assign({}, first):{};
  action.action=patch.action; action.http_url=patch.http_url;
  if(patch.http_bearer_token) action.http_bearer_token=patch.http_bearer_token;
  delete patch.action; delete patch.http_url; delete patch.http_bearer_token;
  const r=Object.assign({}, old||{}, patch);
  if(old&&old.source!==patch.source) delete r.source_key;
  r.actions=[action, ...((old&&old.actions.slice(1))||[])];
  if(old) rules[editIndex]=r;
  else { if(rules.length>=8) throw new Error('The device supports up to 8 automations.'); let id=1; while(rules.some(r=>r.id===id)) id++; r.id=id; rules.push(r); }
  return {schema_version:configSnapshot.schema_version, rules};
}
function renderNetworks(ns) {
  const root=$('wifi_networks'); root.replaceChildren();
  if(!ns||!ns.length) { root.textContent='No networks found. Enter an SSID or scan again.'; return; }
  ns.forEach(n=>{
    const row=document.createElement('button'); row.type='button'; row.className='network-row';
    row.textContent=(n.ssid||'(hidden)')+' · '+n.rssi+' dBm · ch '+n.channel+(n.secure?' · Secured':' · Open');
    row.onclick=()=>{ $('wifi_ssid').value=n.ssid||''; $('wifi_password').value=''; $('wifi_ssid').focus(); };
    root.appendChild(row);
  });
}
function summary(id, rows) {
  const root=$(id); root.replaceChildren();
  rows.forEach(([label,value])=>{ const key=document.createElement('b'), text=document.createElement('span'); key.textContent=label; text.textContent=value; root.appendChild(key); root.appendChild(text); });
}
function netSummary(d) {
  $('connection_pill').textContent=d.sta_connected?'Wi-Fi connected':d.ap_started?'Hotspot active':'Network off';
  summary('net_summary', [['Mode', d.mode], ['Station', d.sta_connected?d.sta_ssid+' @ '+d.sta_ip:d.sta_ssid||'Not connected'], ['Setup AP', d.ap_started?d.ap_ssid+' @ '+d.ap_ip:'Off'], ['Web URL', d.web_url||'—']]);
}
function timeSummary(d) {
  summary('time_summary', [['Timezone', d.timezone||'UTC'], ['Device time', d.time_24h||'--:--']]);
  const z=d.timezone||'UTC', sel=$('timezone');
  if(![...sel.options].some(o=>o.value===z)) opt(sel, z);
  sel.value=z;
}
async function timeStatus() { const d=await api('/api/time'); dump('time_status', d); timeSummary(d); return d; }
async function wifiStatus() {
  const d=await api('/api/wifi/status'); dump('wifi_status', d); netSummary(d);
  if(d.ap_ssid&&!$('ap_ssid').value) $('ap_ssid').value=d.ap_ssid;
  if(d.ap_channel) $('ap_channel').value=d.ap_channel;
  $('wifi_saved').textContent=d.sta_ssid||'No saved network';
  return d;
}
async function refresh() {
  const view=activeView;
  if(view==='automations') applyConfig(await api('/api/config?view=list'));
  else { await wifiStatus(); if($('device_settings').open) await timeStatus(); if($('diagnostic_settings').open) { await capabilities(); dump('status', await api('/api/status')); } }
  loaded[view]=true;
  if(activeView!==view&&!loaded[activeView]) await refresh();
}
function navigate(load=true) {
  const requested=window.location.hash.slice(1);
  activeView=requested==='settings'?'settings':'automations';
  document.querySelectorAll('[data-page]').forEach(p=>p.hidden=p.dataset.page!==activeView);
  document.querySelectorAll('[data-view]').forEach(a=>{ if(a.dataset.view===activeView) a.setAttribute('aria-current','page'); else a.removeAttribute('aria-current'); });
  $('page_title').textContent=activeView==='settings'?'Settings':'Automations';
  if(load&&unlocked&&!pending&&!loaded[activeView]) op(refresh, 'Loading…');
}
async function setWifiMode(mode) { await api('/api/wifi/mode', {method:'POST',body:JSON.stringify({mode})}); await wifiStatus(); }
$('tab_wifi').onclick=()=>showNetPanel('wifi');
$('tab_ap').onclick=()=>showNetPanel('ap');
$('tab_net_status').onclick=()=>showNetPanel('net_status');
$('mode_wifi').onclick=()=>op(()=>setWifiMode('wifi'), 'Changing mode…');
$('mode_ap').onclick=()=>op(()=>setWifiMode('ap'), 'Changing mode…');
$('wifi_reconnect').onclick=$('mode_wifi').onclick;
$('wifi_scan').onclick=()=>op(async ()=>{ const d=await api('/api/wifi/scan', {method:'POST'}); renderNetworks(d.networks); return d; }, 'Scanning Wi-Fi…');
$('wifi_connect').onclick=()=>op(async ()=>{
  if(!$('wifi_ssid').value.trim()) throw new Error('Enter a network name.');
  const d=await api('/api/wifi/connect', {method:'POST',body:JSON.stringify({ssid:$('wifi_ssid').value,password:$('wifi_password').value})});
  $('wifi_password').value=''; await wifiStatus(); return d;
}, 'Connecting… If disconnected, reopen the device URL.');
$('wifi_forget').onclick=()=>{ if(window.confirm('Forget the saved Wi-Fi network and password?')) return op(async ()=>{ const d=await api('/api/wifi/forget', {method:'POST'}); await wifiStatus(); return d; }, 'Forgetting network…'); };
$('ap_start').onclick=()=>op(async ()=>{ const d=await api('/api/wifi/ap', {method:'POST',body:JSON.stringify({ssid:$('ap_ssid').value,password:$('ap_password').value,channel:+$('ap_channel').value})}); $('ap_password').value=''; await wifiStatus(); return d; }, 'Starting hotspot… Changing networks may disconnect this page.');
$('refresh').onclick=()=>op(refresh, 'Refreshing…');
$('load_config').onclick=$('export_config').onclick=()=>op(async ()=>{ const c=await api('/api/config'); applyConfig(c); $('config_json').value=JSON.stringify(c, null, 2); }, 'Loading configuration…');
$('save_time').onclick=()=>op(async ()=>{ const d=await api('/api/time', {method:'POST',body:JSON.stringify({timezone:$('timezone').value})}); dump('time_status', d); timeSummary(d); return d; }, 'Saving timezone…');
$('save_config').onclick=()=>op(async ()=>{ const c=editedConfig(); const d=await api('/api/config', {method:'POST',body:JSON.stringify(c)}); c.rules.forEach(r=>r.actions.forEach(a=>{ if(a.http_bearer_token&&a.http_bearer_token!=='empty') a.http_bearer_token='masked'; })); applyConfig(c); $('http_bearer_token').value=''; $('rule_editor').close(); applyConfig(await api('/api/config')); return d; }, 'Saving automation…');
$('import_config').onclick=()=>{ if(window.confirm('Replace all rules with this JSON configuration?')) return op(async ()=>{ JSON.parse($('config_json').value); const d=await api('/api/config', {method:'POST',body:$('config_json').value}); const c=await api('/api/config'); applyConfig(c); $('config_json').value=JSON.stringify(c, null, 2); return d; }, 'Importing…'); };
$('test_rule').onclick=()=>op(async ()=>{ const d=await api('/api/rules/test', {method:'POST'}); dump('status', d); return d; }, 'Testing saved action…');
$('gpio_test').onclick=()=>op(async ()=>{ const d=await api('/api/gpio/test', {method:'POST',body:JSON.stringify({source:$('source').value,gpio_pin:+$('gpio_pin').value,gpio_profile:$('gpio_profile').value,gpio_debounce_ms:+$('gpio_debounce_ms').value,gpio_active_low:$('gpio_active_low').checked})}); dump('status', d); return d; }, 'Checking GPIO…');
$('hat_probe').onclick=()=>op(async ()=>{ const d=await api('/api/hat/probe', {method:'POST',body:JSON.stringify({source:$('hat_source').value})}); dump('hat_status', d); return d; }, 'Probing HAT…');
$('source').onchange=$('action').onchange=ruleFields;
$('new_automation').onclick=()=>op(()=>loadEditor(), 'Opening editor…');
$('cancel_editor').onclick=()=>$('rule_editor').close();
$('rule_editor').onclose=()=>{ editIndex=-1; $('http_bearer_token').value=''; };
$('rule_editor').oncancel=e=>{ if(pending) e.preventDefault(); };
$('device_settings').ontoggle=()=>{ if(unlocked&&$('device_settings').open) op(timeStatus, 'Loading time…'); };
$('diagnostic_settings').ontoggle=()=>{ if(unlocked&&$('diagnostic_settings').open) op(async ()=>{ await capabilities(); dump('status', await api('/api/status')); }, 'Loading diagnostics…'); };
function authMessage(text) { $('auth_status').textContent=text; }
function rememberAuth() { try { sessionStorage.setItem('stick-auth',deviceToken); } catch(e) {} }
async function authResult(d, attempt) {
  if(attempt!==authAttempt) return;
  if(d.state==='approved') {
    authWaiting=false; clearTimeout(authTimer); session(true);
    activeView='automations'; window.history.replaceState(null,'','#automations'); navigate(false);
    $('page_title').tabIndex=-1; $('page_title').focus();
    await op(refresh,'Loading automations…');
  } else if(d.state==='pending') {
    authMessage('Request #'+d.request_id+': waiting for approval. Press KEY1 on your StickS3; KEY2 rejects.');
    authTimer=setTimeout(()=>checkAccess(attempt),1000);
  } else {
    session(false); authMessage(d.state==='denied'?'Request rejected on the StickS3.':d.state==='expired'?'Request expired. Request access again.':'Request access to continue.');
  }
  controls();
}
async function checkAccess(attempt) {
  if(attempt!==authAttempt) return;
  try { await authResult(await api('/api/auth'),attempt); }
  catch(e) { if(attempt===authAttempt) { authWaiting=false; controls(); authMessage(e.message+' Request access to retry, or cancel.'); } }
}
async function requestAccess() {
  if(authWaiting) return;
  const previous=deviceToken; session(false); banner();
  deviceToken=previous||Array.from(crypto.getRandomValues(new Uint8Array(16)),v=>v.toString(16).padStart(2,'0')).join('');
  rememberAuth(); authWaiting=true; const attempt=authAttempt; controls(); authMessage('Sending access request…');
  try { await authResult(await api('/api/auth',{method:'POST',body:'request'}),attempt); }
  catch(e) { if(attempt===authAttempt) { authWaiting=false; controls(); authMessage(e.message); } }
}
async function endAccess() {
  const token=deviceToken; session(false); banner(); authMessage('Request access to continue.');
  if(token) try { await api('/api/auth',{method:'POST',body:'cancel'},token); }
  catch(e) { authMessage('Access closed in this browser. Device cancellation failed; pending requests expire after 60 seconds.'); }
  $('request_access').focus();
}
$('request_access').onclick=requestAccess;
$('cancel_access').onclick=$('lock_device').onclick=endAccess;
if(typeof window!=='undefined') {
  const drawer=$('mobile_nav'), mobile=window.matchMedia('(max-width:650px)'), actions=$('refresh').parentElement;
  const layout=()=>{ if(drawer.matches(':popover-open')) drawer.hidePopover(); if(mobile.matches) { drawer.setAttribute('popover','auto'); drawer.append($('connection_pill'),$('lock_device')); } else { drawer.removeAttribute('popover'); actions.prepend($('connection_pill')); actions.append($('lock_device')); } };
  mobile.addEventListener('change', layout); layout();
  drawer.onclick=e=>{ if(e.target.closest('a')&&mobile.matches) drawer.hidePopover(); };
  window.addEventListener('hashchange', navigate);
  let saved=''; try { saved=sessionStorage.getItem('stick-auth')||''; } catch(e) {}
  navigate(); session(false);
  if(/^[a-f0-9]{32}$/.test(saved)) { deviceToken=saved; rememberAuth(); authWaiting=true; controls(); authMessage('Checking device approval…'); checkAccess(authAttempt); }
}
