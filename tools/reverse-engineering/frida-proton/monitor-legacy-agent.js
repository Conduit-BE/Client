// Build-specific observational entry hooks only. No target calls or argument changes.
const main=Process.getModuleByName('Minecraft.Windows.exe');
if(Process.platform!=='windows'||Process.arch!=='x64')throw Error('Expected Windows x64');
const specs=[['reload_callback',0xf203e0],['reload_action',0x6583170],['legacy_defs',0x83a190],['legacy_defs_completion',0x8f60b0],['legacy_defs_alternate',0x26a6aa0],['legacy_ui_defs_property',0xdb3e0],['legacy_globals_init',0x12d4c0]];
let sequence=0,queue=[],counts={},listeners=[],failed=false;
const relative=p=>({address:p.toString(),rva:p.compare(main.base)>=0&&p.compare(main.base.add(main.size))<0?p.sub(main.base).toString():null});
function object(p,slot){
 const result={pointer:p.toString()};
 try {const v=p.readPointer();result.table=relative(v);if(slot!==undefined)result.target=relative(v.add(slot).readPointer());}catch(e){result.readError=String(e);}
 return result;
}
function flush(){if(queue.length){send({kind:'events',events:queue});queue=[];}}
for(const [name,rva] of specs){
 const address=main.base.add(rva);
 // Parsing checks readability/decodability; image SHA is checked by the host launcher.
 Instruction.parse(address);
 listeners.push(Interceptor.attach(address,{
  onEnter(args){
   const count=counts[name]=(counts[name]??0)+1;
   if(queue.length>10000){if(!failed){failed=true;send({kind:'fatal',error:'Event safety cap exceeded'});}return;}
   const event={seq:++sequence,phase:'enter',name,rva:'0x'+rva.toString(16),time:Date.now(),tid:Process.getCurrentThreadId(),count,rcx:args[0].toString(),returnAddress:relative(this.returnAddress)};
   this.eventId=event.seq;
   if(name==='reload_callback')event.contextR8=object(args[2],0x270);
   if(name==='reload_action'){try{event.context=object(args[0].add(0x10).readPointer(),0x270);}catch(e){event.readError=String(e);}}
   if(name==='legacy_defs'||name==='legacy_defs_alternate')event.owner=object(args[0]);
   queue.push(event);
  },
  onLeave(){if(this.eventId)queue.push({seq:++sequence,phase:'leave',entry:this.eventId,name,time:Date.now(),tid:Process.getCurrentThreadId()});}
 }));
}
Interceptor.flush();
const timer=setInterval(()=>{flush();send({kind:'heartbeat',time:Date.now(),counts});},1000);
rpc.exports={stop(){clearInterval(timer);listeners.forEach(l=>l.detach());flush();send({kind:'stopped',counts});}};
send({kind:'ready',platform:Process.platform,arch:Process.arch,pid:Process.id,base:main.base.toString(),moduleSize:main.size,hooks:specs.map(([name,rva])=>({name,rva:'0x'+rva.toString(16),address:main.base.add(rva).toString()}))});
