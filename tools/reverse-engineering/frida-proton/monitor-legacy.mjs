import fs from 'node:fs';
import path from 'node:path';
const [endpoint,out]=process.argv.slice(2);
const frida=await import(process.env.CONDUIT_FRIDA_MODULE);
const log=fs.openSync(path.join(out,'events.jsonl'),'a');
let device,session,script,done=false;
async function bounded(action,ms=20000){const cancel=new frida.Cancellable();const timer=setTimeout(()=>cancel.cancel(),ms);try{return await action(cancel);}finally{clearTimeout(timer);}}
const record=x=>{fs.writeSync(log,JSON.stringify({hostTime:new Date().toISOString(),...x})+'\n');fs.fsyncSync(log);};
const finish=()=>{done=true;};process.on('SIGTERM',finish);process.on('SIGINT',finish);
try {
 device=await frida.getDeviceManager().addRemoteDevice(endpoint);
 const matches=(await device.enumerateProcesses()).filter(p=>p.name.toLowerCase()==='minecraft.windows.exe');
 if(matches.length!==1)throw Error('Expected exactly one Minecraft');
 record({kind:'attach',windowsPid:matches[0].pid});
 session=await bounded(c=>device.attach(matches[0].pid,{},c));
 session.detached.connect((reason,crash)=>{record({kind:'detached',reason,crash:crash?.summary??null});done=true;});
 script=await session.createScript(fs.readFileSync(process.env.CONDUIT_AGENT_PATH || new URL('./monitor-legacy-agent.js',import.meta.url),'utf8'));
 script.message.connect(m=>{if(m.type==='send'){record(m.payload);if(m.payload.kind==='ready')fs.writeFileSync(path.join(out,'READY'),JSON.stringify(m.payload,null,2));if(m.payload.kind==='fatal'||m.payload.kind==='screen-observation-ended')done=true;}else{record({kind:'script-error',message:m});done=true;}});
 await script.load();
 let controlMtime=0;
 const start=Date.now();
 while(!done && !fs.existsSync(path.join(out,'STOP')) && Date.now()-start<7200000){
  const control=path.join(out,'CONTROL.json');
  if(fs.existsSync(control)){const mtime=fs.statSync(control).mtimeMs;if(mtime!==controlMtime){controlMtime=mtime;const command=JSON.parse(fs.readFileSync(control,'utf8'));record({kind:'control',command});record({kind:'control-result',result:await script.exports.control(command)});}}
  await new Promise(r=>setTimeout(r,500));
 }
 if(!done)await script.exports.stop();
} catch(e){record({kind:'error',error:String(e)});process.exitCode=1;}
finally {
 if(script)await bounded(c=>script.unload(c),8000).catch(e=>record({kind:"cleanup-error",error:String(e)}));
 if(session)await bounded(c=>session.detach(c),8000).catch(e=>record({kind:"cleanup-error",error:String(e)}));
 if(device)await bounded(c=>frida.getDeviceManager().removeRemoteDevice(endpoint,c),8000).catch(()=>{});
 record({kind:'monitor-ended'});fs.closeSync(log);
}
