import fs from 'node:fs';
const [endpoint,output]=process.argv.slice(2);const frida=await import(process.env.CONDUIT_FRIDA_MODULE);
const device=await frida.getDeviceManager().addRemoteDevice(endpoint);let session,script;
const log=x=>{fs.appendFileSync(output,JSON.stringify({time:new Date().toISOString(),...x})+'\n');};
try{
 const matches=(await device.enumerateProcesses()).filter(p=>p.name.toLowerCase()==='minecraft.windows.exe');if(matches.length!==1)throw Error('Ambiguous target');
 session=await device.attach(matches[0].pid);
 script=await session.createScript(`
 const main=Process.getModuleByName('Minecraft.Windows.exe');
 const modules=Process.enumerateModules().filter(m=>m.name.toLowerCase().includes('conduit'));
 send({kind:'metadata',modules:modules.map(m=>({name:m.name,base:m.base.toString()})),entries:[0x83a190,0x879030,0x8f60b0].map(r=>({rva:r.toString(16),bytes:Array.from(new Uint8Array(main.base.add(r).readByteArray(8))).map(x=>x.toString(16).padStart(2,'0')).join('')}))});
 for(const m of modules){const fn=m.findExportByName('ConduitPrototypeSnapshot');if(fn){const buffer=Memory.alloc(2048);new NativeFunction(fn,'uint',['pointer','uint'])(buffer,2048);send({kind:'snapshot',module:m.name,state:buffer.readUtf8String()});}}
 send({kind:'done'});
 `);
 let finish;const done=new Promise(r=>finish=r);script.message.connect(m=>{log(m.type==='send'?m.payload:{kind:'error',message:m});if(m.type==='send'&&m.payload.kind==='done')finish();});await script.load();await Promise.race([done,new Promise((_,reject)=>setTimeout(()=>reject(Error('Snapshot timeout')),10000))]);
}finally{if(script)await script.unload();if(session)await session.detach();await frida.getDeviceManager().removeRemoteDevice(endpoint);}
