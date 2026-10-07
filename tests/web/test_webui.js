const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
class Element {
  constructor(tag = 'div') { this.tag = tag; this.children = []; this.value = ''; this.checked = true; }
  set innerHTML(value) { assert.equal(value, '', 'dynamic markup must not be parsed'); this.children = []; }
  appendChild(child) { this.children.push(child); }
  replaceChildren() { this.children = []; }
}
const nodes = new Map();
const document = {
  getElementById(id) { if (!nodes.has(id)) nodes.set(id, new Element()); return nodes.get(id); },
  createElement(tag) { return new Element(tag); },
  querySelectorAll() { return []; },
};
const context = vm.createContext({document, fetch: () => new Promise(() => {})});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../../webui/app.js'), 'utf8'), context);
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
console.log('webui rendering and credential tests passed');

const html = fs.readFileSync(path.join(__dirname, "../../webui/index.html"), "utf8");
assert(html.includes("Test first action"));
assert(html.includes("Runs the saved first action; ignores trigger and timing"));
assert(!html.includes("Test First Rule"));
