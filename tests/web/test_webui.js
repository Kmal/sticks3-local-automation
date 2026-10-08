const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
class Element {
  constructor(tag = 'div') { this.tag = tag; this.children = []; this.value = ''; this.checked = true; }
  set innerHTML(value) { assert.equal(value, '', 'dynamic markup must not be parsed'); this.children = []; }
  appendChild(child) { this.children.push(child); }
  replaceChildren() { this.children = []; }
  focus() {}
  showModal() { this.open = true; }
  close() { this.open = false; }
  get options() { return this.children; }
  setAttribute(name, value) { this[name] = value; }
  removeAttribute(name) { delete this[name]; }
}
const nodes = new Map();
const document = {
  getElementById(id) { if (!nodes.has(id)) nodes.set(id, new Element()); return nodes.get(id); },
  createElement(tag) { return new Element(tag); },
  querySelectorAll() { return []; },
};
const context = vm.createContext({document, AbortSignal, fetch: () => new Promise(() => {})});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../../webui/app.js'), 'utf8'), context);
document.getElementById('threshold').value = 'true';
const hostile = '<img src=x onerror=alert(1)>';
context.inputStatus = {mode:'apsta', sta_connected:true, sta_ssid:hostile, sta_ip:'1.2.3.4', ap_started:true, ap_ssid:hostile, ap_ip:'5.6.7.8', web_url:'http://5.6.7.8'};
vm.runInContext('netSummary(inputStatus)', context);
const summary = document.getElementById('net_summary');
assert.equal(summary.children[3].textContent, hostile + ' @ 1.2.3.4');
assert.equal(summary.children[5].textContent, hostile + ' @ 5.6.7.8');
assert(summary.children.every(child => child.tag === 'b' || child.tag === 'span'));
context.networks = [{ssid:hostile, rssi:-50, channel:6, secure:true}];
vm.runInContext('renderNetworks(networks)', context);
const row = document.getElementById('wifi_networks').children[0];
assert(row.textContent.startsWith(hostile));
row.onclick();
assert.equal(document.getElementById('wifi_ssid').value, hostile);
assert(!Object.hasOwn(JSON.parse(vm.runInContext('body()', context)), 'http_bearer_token'));
document.getElementById('http_bearer_token').value = 'replacement';
assert.equal(JSON.parse(vm.runInContext('body()', context)).http_bearer_token, 'replacement');
document.getElementById('threshold').value = '12junk';
assert.throws(() => vm.runInContext('body()', context), /32-bit whole number/);
document.getElementById('threshold').value = 'true';
console.log('webui rendering and credential tests passed');

const html = fs.readFileSync(path.join(__dirname, "../../webui/index.html"), "utf8");
assert(html.includes("Test first action"));
assert(html.includes("Runs the saved first action; ignores trigger and timing"));
assert(!html.includes("Test First Rule"));

(async () => {
  let requests = [];
  context.fetch = async (url, options) => {
    requests.push({url, options});
    return {ok:true, text:async()=>'{"ok":true}'};
  };
  await assert.rejects(vm.runInContext("api('/api/status')", context), /Device code required/);
  assert.equal(requests.length, 0);
  vm.runInContext("deviceToken='0123456789abcdef'", context);
  await vm.runInContext("api('/api/status')", context);
  assert.equal(requests[0].options.headers['X-Device-Token'], '0123456789abcdef');
  requests = [];
  context.fetch = async (url, options) => {
    requests.push({url, options});
    const result = options.method === 'POST' ? {ok:true} : {rules:[]};
    return {ok:true, text:async()=>JSON.stringify(result)};
  };
  vm.runInContext('configSnapshot={schema_version:1,rules:[]}; editIndex=-1', context);
  document.getElementById('rule_name').value = 'New automation';
  document.getElementById('source').value = 'key1.short';
  document.getElementById('action').value = 'local_ui';
  await document.getElementById('save_config').onclick();
  assert.equal(requests[0].url, '/api/config');
  assert.equal(requests[0].options.method, 'POST');
  assert.equal(requests[1].url, '/api/config');
  assert.notEqual(requests[1].options.method, 'POST');
  requests = [];
  await vm.runInContext('activeView="automations"; refresh()', context);
  assert.deepEqual(requests.map(r => r.url), ['/api/config?view=list']);
  requests = [];
  await vm.runInContext('activeView="settings"; refresh()', context);
  assert.deepEqual(requests.map(r => r.url), ['/api/wifi/status']);
  assert.equal(requests[0].options.signal instanceof AbortSignal, true);
  context.fetch = async () => ({ok:false, status:401, text:async()=>'{"error":"Expired code"}'});
  await assert.rejects(vm.runInContext("api('/api/status')", context), /Expired code/);
  assert.equal(vm.runInContext('deviceToken', context), '');
  assert.equal(document.getElementById('unlock_panel').hidden, false);
  const fixture = {schema_version:1,rules:[
    {id:4,name:hostile,source:'key1.short',enabled:false,actions:[{action:'local_ui'}]},
    {id:8,name:'Second',source:'key2.short',enabled:true,source_key:'key2',cooldown_ms:4321,actions:[{action:'http_post',http_url:'https://example.com',http_bearer_token:'masked',action_timeout_ms:3000},{action:'local_ui'}]}
  ]};
  context.fixture = fixture;
  vm.runInContext('applyConfig(fixture); editIndex=1',context);
  assert.equal(document.getElementById('rule_list').children[0].children[0].textContent,hostile);
  document.getElementById('rule_name').value = 'Second edited';
  document.getElementById('source').value = 'key2.short';
  document.getElementById('action').value = 'http_post';
  document.getElementById('http_url').value = 'https://example.com/new';
  document.getElementById('http_bearer_token').value = '';
  let candidate = JSON.parse(vm.runInContext('JSON.stringify(editedConfig())',context));
  assert.deepEqual(candidate.rules[0],fixture.rules[0]);
  assert.equal(candidate.rules[1].cooldown_ms,4321);
  assert.equal(candidate.rules[1].actions[0].http_bearer_token,'masked');
  assert.equal(candidate.rules[1].actions[0].action_timeout_ms,3000);
  assert.deepEqual(candidate.rules[1].actions[1],fixture.rules[1].actions[1]);
  document.getElementById('action').value = 'local_ui';
  candidate = JSON.parse(vm.runInContext('JSON.stringify(editedConfig())',context));
  assert(!Object.hasOwn(candidate.rules[1].actions[0],'http_bearer_token'));
  vm.runInContext('editIndex=-1',context);
  candidate = JSON.parse(vm.runInContext('JSON.stringify(editedConfig())',context));
  assert.equal(candidate.rules.length,3);
  assert.equal(candidate.rules[2].id,1);
  assert.deepEqual(candidate.rules.slice(0,2),fixture.rules);
  assert.equal(fixture.rules.length,2,'editing must not mutate the loaded configuration');
  console.log('multi-rule creation, isolated editing, and token preservation tests passed');
  console.log('webui session header and save acknowledgement tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
