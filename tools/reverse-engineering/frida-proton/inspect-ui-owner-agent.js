// Bounded read-only owner discovery. No native calls, writes, global file hooks or reloads.
const module = Process.getModuleByName('Minecraft.Windows.exe'), base = module.base;
const relative = p => ({ address: p.toString(), rva: p.compare(base) >= 0 && p.compare(base.add(module.size)) < 0 ? p.sub(base).toString() : null });
const read = (p, offset = 0) => p.add(offset).readPointer();
let hook = null, finished = false;
function stop() { if (hook) { hook.detach(); hook = null; } }
rpc.exports = { stop() { stop(); return true; } };
const prefix = base.add(0x879030).readU8();
if (prefix !== 0x55) throw Error('Frame entry is already hooked or incompatible; refusing duplicate instrumentation');
hook = Interceptor.attach(base.add(0x879030), {
 onEnter(args) {
  if (finished) return;
  try {
   const owner = args[0];
   if (!read(owner).equals(base.add(0xf091b70))) return;
   const service = read(owner, 0x818), root = read(owner, 0x9d0), node = read(root);
   if (node.equals(root)) return;
   const client = read(node, 0x38), serviceTable = read(service), clientTable = read(client);
   const data = { kind: 'ui-owner', time: Date.now(), tid: Process.getCurrentThreadId(), caller: relative(this.returnAddress), owner: relative(owner), service: relative(service), serviceTable: relative(serviceTable), operation: relative(read(serviceTable, 0x28)), client: relative(client), clientTable: relative(clientTable), clientOwner: relative(read(client, 0x1c8)), clientOwnerGetter: relative(read(clientTable, 0x270)), clientCacheGetter: relative(read(clientTable, 0x790)), cache: relative(read(client, 0x230)), owner68: relative(read(read(owner), 0x68)) };
   finished = true; send(data); setImmediate(stop);
  } catch (e) { finished = true; send({kind:'fatal',error:String(e)}); setImmediate(stop); }
 }
});
setTimeout(() => { stop(); if (!finished) send({kind:'fatal',error:'No validated owner within 20 seconds'}); }, 20000);
send({kind:'ready',base:base.toString(),scope:'one owner identity capture; no native calls'});
