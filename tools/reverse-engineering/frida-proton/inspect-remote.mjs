import fs from 'node:fs';
const [endpoint, target, output] = process.argv.slice(2);
if (!process.env.CONDUIT_FRIDA_MODULE) throw Error('Set CONDUIT_FRIDA_MODULE to the absolute Frida Node entrypoint (17.23.2)');
const frida = await import(process.env.CONDUIT_FRIDA_MODULE);
const result = {endpoint,target,started:new Date().toISOString()};
let device,session,script;
try {
 device=await frida.getDeviceManager().addRemoteDevice(endpoint);
 let selected;
 for(let i=0;i<30;i++) {
  const processes=await device.enumerateProcesses();
  result.processes=processes.map(p=>({pid:p.pid,name:p.name}));
  if(target==='--list')break;
  const matches=processes.filter(p=>p.name.toLowerCase()===target.toLowerCase());
  if(matches.length>1)throw Error('Ambiguous target name');
  if(matches.length===1){selected=matches[0];break;}
  await new Promise(r=>setTimeout(r,250));
 }
 if(target!=='--list') {
  if(!selected)throw Error('Target not enumerated');
  result.windowsPid=selected.pid;
  session=await device.attach(selected.pid);
  session.detached.connect((reason,crash)=>{result.detachReason=reason;result.crash=crash?.summary??null;});
  script=await session.createScript(`
   const modules=Process.enumerateModules();
   const main=modules.find(m=>m.name.toLowerCase()===${JSON.stringify(target.toLowerCase())});
   const isProbe=${JSON.stringify(target.toLowerCase()==='conduitfridaprobe.exe')};
   const marker=isProbe ? main?.findExportByName('conduitFridaMarker') : null;
   send({platform:Process.platform,arch:Process.arch,pid:Process.id,
    modules:modules.map(m=>({name:m.name,base:m.base.toString(),size:m.size,path:m.path})),
    mainMagic:isProbe ? main?.base.readU16() : null,probeMarker:marker?.readU32()});
  `);
  let resolveMessage,rejectMessage;
  const received=new Promise((resolve,reject)=>{resolveMessage=resolve;rejectMessage=reject;});
  script.message.connect(message=>{if(message.type==='send'){result.inspection=message.payload;resolveMessage();}else if(message.type==='error'){result.scriptError=message;rejectMessage(Error(message.description));}});
  await script.load();
  await Promise.race([received,new Promise((_,reject)=>setTimeout(()=>reject(Error('Inspection timeout')),8000))]);
  await script.unload();script=null;
  await session.detach();session=null;
  result.targetAliveAfterDetach=(await device.enumerateProcesses()).some(p=>p.pid===selected.pid);
  if(result.inspection.platform!=='windows'||result.inspection.arch!=='x64'||!result.targetAliveAfterDetach)throw Error('Windows x64 or survival check failed');
 }
 result.success=true;
} catch(e){result.success=false;result.error=String(e);process.exitCode=1;}
finally {
 if(script)await script.unload().catch(()=>{});
 if(session)await session.detach().catch(()=>{});
 if(device)await frida.getDeviceManager().removeRemoteDevice(endpoint).catch(()=>{});
 fs.writeFileSync(output,JSON.stringify(result,null,2));
 console.log(JSON.stringify({success:result.success,error:result.error,pid:result.windowsPid,platform:result.inspection?.platform,arch:result.inspection?.arch,modules:result.inspection?.modules?.length,mainMagic:result.inspection?.mainMagic,probeMarker:result.inspection?.probeMarker,targetAlive:result.targetAliveAfterDetach}));
}
