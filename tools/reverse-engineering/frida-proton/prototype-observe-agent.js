// Passive dependency/lifetime investigation; intentionally contains no NativeFunction.
const main=Process.getModuleByName('Minecraft.Windows.exe'),base=main.base;
if(Process.platform!=='windows'||Process.arch!=='x64')throw Error('Expected Windows x64');
let queue=[],seq=0,counts={},listeners=[],owner=null;
const relative=p=>({address:p.toString(),rva:p.compare(base)>=0&&p.compare(base.add(main.size))<0?p.sub(base).toString():null});
function object(p,slot){const result={pointer:p.toString()};try{const v=p.readPointer();result.table=relative(v);if(slot!==undefined)result.target=relative(v.add(slot).readPointer());}catch(e){result.error=String(e);}return result;}
function member(p,offset,slot){try{return object(p.add(offset).readPointer(),slot);}catch(e){return{error:String(e)};}}
function flush(){if(queue.length){send({kind:'events',events:queue});queue=[];}}
function emit(e){if(queue.length>=10000)throw Error('Safety cap');queue.push({seq:++seq,time:Date.now(),tid:Process.getCurrentThreadId(),...e});}
const specs=[['legacy_defs',0x83a190],['legacy_defs_completion',0x8f60b0],['legacy_ui_defs_property',0xdb3e0],['legacy_globals_init',0x12d4c0],['legacy_apply',0x193f2f0],['reload_parent',0x8c4f40]];
for(const [name,rva] of specs){listeners.push(Interceptor.attach(base.add(rva),{
 onEnter(args){counts[name]=(counts[name]??0)+1;const e={phase:'enter',name,rcx:args[0].toString(),rdx:args[1].toString(),returnAddress:relative(this.returnAddress)};
 if(name==='legacy_defs'){owner=args[0];e.owner=object(owner,0x60);e.resourceService=member(owner,0x818,8);e.resourceContext=member(owner,0x228);e.finalRefresh=member(owner,0x1df8,8);}
 if(name==='legacy_defs_completion'){e.callable=object(args[0]);e.capture=member(args[0],8);}
 if(name==='reload_parent')e.owner=object(args[0]);
 emit(e);this.entry=seq;
 },onLeave(){emit({phase:'leave',name,entry:this.entry});}
}));}
// Small sample of this owner's own virtual entries to find a live, repeated dispatch point.
const samples=[0x879030,0x837cb0,0x878dd0,0x8b5d50,0x8951f0,0x893e50,0x838730];
for(const rva of samples){const name='owner_'+rva.toString(16);listeners.push(Interceptor.attach(base.add(rva),{
 onEnter(args){try{if(!args[0].readPointer().equals(base.add(0xf091b70)))return;}catch{return;}
 counts[name]=(counts[name]??0)+1;
 if(counts[name]<=3)emit({phase:'enter',name,object:object(args[0]),returnAddress:relative(this.returnAddress)});
 }
}));}
const timer=setInterval(()=>{flush();send({kind:'heartbeat',time:Date.now(),counts});},1000);
rpc.exports={control(command){if(command.kind==='mark'){flush();send({kind:'mark',label:command.label,time:Date.now()});return{ok:true};}throw Error('Only observation marks are enabled');},stop(){clearInterval(timer);listeners.forEach(l=>l.detach());flush();send({kind:'stopped',counts});}};
Interceptor.flush();send({kind:'ready',pid:Process.id,base:base.toString(),hooks:[...specs,...samples.map(r=>['owner_'+r.toString(16),r])].map(([name,rva])=>({name,rva:'0x'+rva.toString(16),address:base.add(rva).toString()}))});
