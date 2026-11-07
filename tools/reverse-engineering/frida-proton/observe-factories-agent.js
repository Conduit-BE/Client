// Temporary exact-build observer; bounded UI-thread snapshots, no native calls or game writes.
// Evidence and limitations: docs/reverse-engineering/1.26.60-ui-factories.md.
const image = Process.getModuleByName('Minecraft.Windows.exe');
const base = image.base;
const hooks = [];
let stopped = false, events = 0, visibleCalls = 0, nameCalls = 0;
const relative = p => p.compare(base) >= 0 && p.compare(base.add(image.size)) < 0 ? p.sub(base).toString() : null;
function readPointer(p) {
    if (p.isNull()) throw Error('Null pointer slot');
    return p.readPointer(); // Frida guarded read throws on access failure; no whole-range lookup per edge.
}
function msvcString(p) {
    if (p.isNull()) throw Error('Null string object');
    const size = p.add(16).readU64(), capacity = p.add(24).readU64();
    if (size.compare(192) > 0 || capacity.compare(size) < 0 || capacity.compare(1048576) > 0) throw Error('Invalid/bounded string');
    const count = size.toNumber();
    if (!count) return '';
    const text = capacity.compare(16) < 0 ? p : readPointer(p);
    if (text.add(count).readU8() !== 0) throw Error('Unreadable string storage');
    return text.readUtf8String(count);
}
function emit(kind, fields) {
    if (events >= 64 || stopped) return;
    ++events;
    send({kind, time: Date.now(), tid: Process.getCurrentThreadId(), ...fields});
}
function stop() {
    if (stopped) return;
    stopped = true;
    for (const hook of hooks) hook.detach();
    send({kind: 'screen-observation-ended', visibleCalls, nameCalls, events});
}
rpc.exports = {stop() {stop(); return true;}};
function attach(rva, expectedPrefix, callbacks) {
    const address = base.add(rva);
    const actual = [...new Uint8Array(address.readByteArray(expectedPrefix.length))];
    if (actual.some((value, index) => value !== expectedPrefix[index])) throw Error('Entry already hooked/incompatible: '+rva.toString(16));
    hooks.push(Interceptor.attach(address, callbacks));
}

let lastSample=0;const seen=new Set();
try {
 attach(0x4cd2860,[0x48,0x8b,0x49,0x40,0xe9],{onEnter(args){
  if(stopped||Date.now()-lastSample<2000)return;
  try{
   if(!readPointer(args[0]).equals(base.add(0xf20ec60)))return;
   const tree=readPointer(readPointer(args[0].add(0x40)).add(0x50));
   const root=readPointer(tree.add(8)),name=msvcString(root.add(0x20));
   if(!['hud_screen','inventory_screen','small_chest_screen','pause_screen'].includes(name)||seen.has(name))return;lastSample=Date.now();const start=Date.now();
   const queue=[root],nodes=new Map([[root.toString(),[]]]),factories=[];let edgeCount=0;
   for(let i=0;i<queue.length;i++){
    if(queue.length>8192||edgeCount>32768||Date.now()-start>150)throw Error('Traversal limit');
    const node=queue[i],children=nodes.get(node.toString());
    const begin=readPointer(node.add(0x98)),end=readPointer(node.add(0xa0));
    const bytes=end.sub(begin).toUInt32();if(end.compare(begin)<0||bytes%16||bytes>16*8192)throw Error('Children invalid');
    for(let n=0;n<bytes;n+=16){const child=readPointer(begin.add(n));++edgeCount;children.push(child.toString());if(!nodes.has(child.toString())){nodes.set(child.toString(),[]);queue.push(child);}}
    const cb=readPointer(node.add(0xb8)),ce=readPointer(node.add(0xc0));
    const span=ce.sub(cb).toUInt32();if(ce.compare(cb)<0||span%8||span>8*128)throw Error('Components invalid');
    for(let n=0;n<span;n+=8){const c=readPointer(cb.add(n));if(c.isNull()||!readPointer(c).equals(base.add(0xf0fcaf0)))continue;
     const wb=readPointer(c.add(0xa8)),we=readPointer(c.add(0xb0));const size=we.sub(wb).toUInt32();if(we.compare(wb)<0||size%16||size>16*8192)throw Error('Weak vector invalid');
     const refs=[];for(let o=0;o<size;o+=16){const object=readPointer(wb.add(o)),owner=readPointer(wb.add(o+8));const strong=owner.isNull()?0:owner.add(8).readS32();refs.push({object:object.toString(),strong});}
     factories.push({control:msvcString(node.add(0x20)),name:msvcString(c.add(0x20)),ownerMatches:readPointer(c.add(8)).equals(node),mappings:c.add(0x50).readU64().toString(),refs});
    }
   }
   const generated=new Set();for(const f of factories){f.liveRoots=f.refs.filter(x=>x.strong>0&&nodes.has(x.object)).length;f.expired=f.refs.filter(x=>x.strong===0).length;f.outside=f.refs.filter(x=>x.strong>0&&!nodes.has(x.object)).length;
    const roots=f.refs.filter(x=>x.strong>0&&nodes.has(x.object)).map(x=>x.object);const unique=new Set(roots);for(let i=0;i<roots.length;i++){generated.add(roots[i]);for(const ch of nodes.get(roots[i]))if(!unique.has(ch)){unique.add(ch);roots.push(ch);}}f.subtree=unique.size;delete f.refs;
   }
   seen.add(name);emit('factory-snapshot',{name,controls:nodes.size,factories,generatedSubtree:generated.size,durationMs:Date.now()-start});
   if(seen.size>=4)setImmediate(stop);
  }catch(e){emit('factory-unavailable',{error:String(e)});setImmediate(stop);}
 }});
 send({kind:'ready',observation:'bounded-factory-components',durationMs:60000});setTimeout(stop,60000);
}catch(e){stop();throw e;}
