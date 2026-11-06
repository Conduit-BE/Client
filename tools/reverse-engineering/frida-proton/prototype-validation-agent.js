// Observe only the established legacy chain; calls below are our DLL's documented C exports.
const main=Process.getModuleByName('Minecraft.Windows.exe'),base=main.base;
if(Process.platform!=='windows'||Process.arch!=='x64')throw Error('Expected Windows x64');
let queue=[],seq=0,counts={},listeners=[],prototype=null,api=null,controlActive=false;
const files=new Map();
const relative=p=>({address:p.toString(),rva:p.compare(base)>=0&&p.compare(base.add(main.size))<0?p.sub(base).toString():null});
function flush(){if(queue.length){send({kind:'events',events:queue});queue=[];}}
const specs=[['resource_parent',0x8c4f40],['legacy_resource_request',0xda940],['legacy_ui_defs_property',0xdb3e0],['legacy_globals_init',0x12d4c0],['legacy_apply',0x193f2f0],['legacy_final_queue',0x564a40]];
for(const [name,rva] of specs)listeners.push(Interceptor.attach(base.add(rva),{
 onEnter(args){counts[name]=(counts[name]??0)+1;this.id=++seq;queue.push({seq:this.id,time:Date.now(),tid:Process.getCurrentThreadId(),phase:'enter',name,rcx:args[0].toString(),rdx:args[1].toString(),r8:args[2].toString(),returnAddress:relative(this.returnAddress)});},
 onLeave(){queue.push({seq:++seq,time:Date.now(),tid:Process.getCurrentThreadId(),phase:'leave',name,entry:this.id});}
}));
// Scoped Windows file I/O: retain paths/hashes only for legacy UI JSON files.
function uiPath(value){const text=value.toLowerCase().replaceAll('\\','/');return text.endsWith('.json') && ((text.includes('/ui/') || text.includes('/interfaces/')) || /(^|\/)(_ui_defs|_global_variables|hud_screen|server_form|chest_screen)\.json$/.test(text));}
const ioSeen=new Map();
function io(e){if(e.tid===undefined)e.tid=Process.getCurrentThreadId();const key=[e.tid,e.kind,e.path,e.status,e.sha256].join('|');const now=Date.now();if(now-(ioSeen.get(key)??0)<1000)return;ioSeen.set(key,now);queue.push({seq:++seq,time:Date.now(),tid:Process.getCurrentThreadId(),...e});}
// File I/O evidence was collected separately. No global NT hooks during native setup.
function snapshot(){if(!api)return null;const output=Memory.alloc(2048);api.snapshot(output,2048);return output.readUtf8String();}
const timer=setInterval(()=>{if(controlActive)return;flush();send({kind:'heartbeat',time:Date.now(),counts,nativeState:snapshot()});},1000);
rpc.exports={
 control(command){
  controlActive=true;
  try {
  if(command.kind==='mark'){flush();send({kind:'mark',label:command.label,time:Date.now()});return{ok:true};}
  if(command.kind==='loadPrototype'){
   if(prototype && !snapshot().includes('\"enabled\":0'))throw Error('Disable current prototype before switching');prototype=Module.load(command.dll);
   api={start:new NativeFunction(prototype.findExportByName('ConduitPrototypeStartAsync') || prototype.getExportByName('ConduitPrototypeStart'),'int',['pointer','pointer']),disable:new NativeFunction(prototype.getExportByName('ConduitPrototypeDisable'),'void',[]),snapshot:new NativeFunction(prototype.getExportByName('ConduitPrototypeSnapshot'),'uint',['pointer','uint'])};
   return{module:prototype.path,state:snapshot(),existingConduitModules:Process.enumerateModules().filter(m=>m.name.toLowerCase().includes('conduit')).map(m=>m.name)};
  }
  if(command.kind==='startPrototype'){
   if(!api)throw Error('Load own prototype first');
   const result=api.start(Memory.allocUtf8String(command.watchDirectory),Memory.allocUtf8String(command.logFile));return{result,state:snapshot()};
  }
  if(command.kind==='retireV1Prototype'){
   if(!api||!prototype||prototype.name.toLowerCase()!=='conduitreloadprototype.dll')throw Error('Expected existing v1 own DLL');
   const before=snapshot();if(!before.includes('\"busy\":0'))throw Error('Wait for native completion');
   const address=prototype.base.add(0xefc0);
   const actual=Array.from(new Uint8Array(address.readByteArray(command.expectedPrefix.length/2))).map(b=>b.toString(16).padStart(2,'0')).join('');
   if(actual!==command.expectedPrefix)throw Error('Own MinHook symbol bytes mismatch');
   api.disable();
   const disableHooks=new NativeFunction(address,'int',['pointer']);const status=disableHooks(ptr(0));
   if(status!==0)throw Error('Own MH_DisableHook failed: '+status);
   return{state:snapshot(),retired:true,note:'Own verified MinHook API; v1 remains pinned and disabled'};
  }
  if(command.kind==='disablePrototype'){if(api)api.disable();return{state:snapshot()};}
  if(command.kind==='configureFallback'){const configure=new NativeFunction(prototype.getExportByName('ConduitPrototypeSetResourceFallback'),'int',['uint']);const arm=new NativeFunction(prototype.getExportByName('ConduitPrototypeArmFromObservation'),'int',['uint','uint']);return{configured:configure(command.observedFlag),armed:arm(command.windowsPid,command.observedThread),state:snapshot()};}
  if(command.kind==='snapshot')return{state:snapshot()};
  throw Error('Unknown prototype control');
  } finally {controlActive=false;}
 },
 stop(){clearInterval(timer);listeners.forEach(l=>l.detach());flush();send({kind:'stopped',counts,nativeState:snapshot(),note:'Prototype DLL remains resident; use disablePrototype before stopping to disable hotkeys.'});}
};
Interceptor.flush();send({kind:'ready',pid:Process.id,base:base.toString(),hooks:specs.map(([name,rva])=>({name,rva:'0x'+rva.toString(16),address:base.add(rva).toString()}))});
