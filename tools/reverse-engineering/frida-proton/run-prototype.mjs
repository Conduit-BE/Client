// Own-DLL lifecycle only; native game calls remain gated inside the backend.
import fs from 'node:fs';import path from 'node:path';
const [endpoint,configPath,out]=process.argv.slice(2),config=JSON.parse(fs.readFileSync(configPath,'utf8'));
const frida=await import(process.env.CONDUIT_FRIDA_MODULE);const device=await frida.getDeviceManager().addRemoteDevice(endpoint);let session,script,done=false;
const fd=fs.openSync(path.join(out,'prototype-control.jsonl'),'a');
const record=x=>{fs.writeSync(fd,JSON.stringify({hostTime:new Date().toISOString(),...x})+'\n');fs.fsyncSync(fd);};
const attachCancel=new frida.Cancellable(),attachTimer=setTimeout(()=>attachCancel.cancel(),20000);
try {
 const matches=(await device.enumerateProcesses()).filter(p=>p.name.toLowerCase()==='minecraft.windows.exe');if(matches.length!==1||matches[0].pid!==config.windowsPid)throw Error('Observed process identity changed');
 session=await device.attach(matches[0].pid,{},attachCancel);clearTimeout(attachTimer);
 script=await session.createScript(`
 const config=${JSON.stringify(config)};
 if(config.retireModule){const prior=Process.findModuleByName(config.retireModule);if(prior){const before=Memory.alloc(2048);new NativeFunction(prior.getExportByName('ConduitPrototypeSnapshot'),'uint',['pointer','uint'])(before,2048);if(before.readUtf8String().includes('\"busy\":1'))throw Error('Previous own prototype still busy');new NativeFunction(prior.getExportByName('ConduitPrototypeDisable'),'void',[])();}}
 const module=Module.load(config.dll);
 const api={start:new NativeFunction(module.getExportByName('ConduitPrototypeStartAsync'),'int',['pointer','pointer']),state:new NativeFunction(module.getExportByName('ConduitPrototypeSnapshot'),'uint',['pointer','uint']),configure:new NativeFunction(module.getExportByName('ConduitPrototypeSetResourceFallback'),'int',['uint']),arm:new NativeFunction(module.getExportByName('ConduitPrototypeArmFromObservation'),'int',['uint','uint']),disable:new NativeFunction(module.getExportByName('ConduitPrototypeDisable'),'void',[])};
 const started=api.start(Memory.allocUtf8String(config.watchDirectory),Memory.allocUtf8String(config.logFile));send({kind:'start-queued',started,module:module.path});
 let configured=false;
 function state(){const buffer=Memory.alloc(2048);api.state(buffer,2048);return buffer.readUtf8String();}
 const timer=setInterval(()=>{const value=state();send({kind:'state',value});if(value.startsWith('ERROR:'))send({kind:'startup-error',value});
 if(!configured&&value.includes('"enabled":1')){const result=api.configure(config.observedFlag),armed=api.arm(config.windowsPid,config.observedThread);configured=result===1&&armed===1;send({kind:'configured',result,armed,state:state()});}},1000);
 rpc.exports={disable(){api.disable();return state();},stop(){clearInterval(timer);return state();}};
 `);
 script.message.connect(m=>{record(m.type==='send'?m.payload:{kind:'script-error',message:m});if(m.type==='error'||m.payload?.kind==='startup-error')done=true;});
 session.detached.connect((reason,crash)=>{record({kind:'detached',reason,crash:crash?.summary??null});done=true;});
 await script.load();const start=Date.now();
 while(!done && Date.now()-start<7200000 && !fs.existsSync(path.join(out,'CONTROL_STOP'))){
  if(fs.existsSync(path.join(out,'DISABLE'))){record({kind:'disabled',state:await script.exports.disable()});done=true;break;}
  await new Promise(r=>setTimeout(r,500));
 }
 if(!done)record({kind:'ended',state:await script.exports.stop()});
}finally{clearTimeout(attachTimer);if(script)await script.unload();if(session)await session.detach();fs.closeSync(fd);await frida.getDeviceManager().removeRemoteDevice(endpoint);}
