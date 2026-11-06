// Recovery of a pinned *own* build via its verified COFF symbols; never a game call.
import fs from 'node:fs';
const [endpoint,manifestPath,output]=process.argv.slice(2),manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
const frida=await import(process.env.CONDUIT_FRIDA_MODULE);const device=await frida.getDeviceManager().addRemoteDevice(endpoint);let session,script;
const record=x=>fs.appendFileSync(output,JSON.stringify({time:new Date().toISOString(),...x})+'\n');
const cancel=new frida.Cancellable(),timer=setTimeout(()=>cancel.cancel(),20000);
try{
 const matches=(await device.enumerateProcesses()).filter(p=>p.name.toLowerCase()==='minecraft.windows.exe');if(matches.length!==1)throw Error('Ambiguous target');
 session=await device.attach(matches[0].pid,{},cancel);clearTimeout(timer);
 script=await session.createScript(`
 const spec=${JSON.stringify(manifest)};
 const module=Process.getModuleByName(spec.module);
 function check(item){const address=module.base.add(item.rva);const bytes=Array.from(new Uint8Array(address.readByteArray(16))).map(b=>b.toString(16).padStart(2,'0')).join('');if(bytes!==item.prefix)throw Error('Own symbol bytes mismatch');return address;}
 const disable=check(spec.disable),retire=check(spec.retire);
 send({kind:'verified-own-symbols',module:module.path});
 new NativeFunction(disable,'void',[])();
 const status=new NativeFunction(retire,'int',['pointer'])(ptr(0));
 const main=Process.getModuleByName('Minecraft.Windows.exe');
 send({kind:'retired',status,entries:[0x83a190,0x879030,0x8f60b0].map(r=>({rva:r.toString(16),bytes:Array.from(new Uint8Array(main.base.add(r).readByteArray(7))).map(x=>x.toString(16).padStart(2,'0')).join('')}))});
 `);
 let finish;const done=new Promise(r=>finish=r);script.message.connect(m=>{record(m.type==='send'?m.payload:{kind:'error',message:m});if(m.type==='error'||m.payload?.kind==='retired')finish();});await script.load();await Promise.race([done,new Promise((_,reject)=>setTimeout(()=>reject(Error('Recovery timeout')),10000))]);
}finally{clearTimeout(timer);if(script)await script.unload();if(session)await session.detach();await frida.getDeviceManager().removeRemoteDevice(endpoint);}
