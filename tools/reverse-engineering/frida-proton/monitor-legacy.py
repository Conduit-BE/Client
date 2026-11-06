import argparse,os,subprocess,time,socket,json,hashlib
from pathlib import Path
parser=argparse.ArgumentParser(description='Monitor only known legacy UI entries; probe validation gates one Minecraft attach. Create OUTPUT/STOP to detach.')
parser.add_argument('--game-linux-pid',type=int,required=True)
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--server-exe',type=Path,required=True)
parser.add_argument('--probe-exe',type=Path,required=True)
parser.add_argument('--game-exe',type=Path,required=True)
# Monitoring is explicitly requested; probe remains the gate.
options=parser.parse_args()
r=options.output_dir.resolve();r.mkdir(parents=True,exist_ok=True);pid=options.game_linux_pid
client=Path(__file__).with_name('inspect-remote.mjs').resolve()
if not os.environ.get('CONDUIT_FRIDA_MODULE'):raise RuntimeError('Set CONDUIT_FRIDA_MODULE to the matching Frida Node entrypoint')
if any((r/name).exists() for name in ['game-prefix-context.json','game-prefix-probe-result.json','minecraft-result.json']):raise RuntimeError('Choose a fresh output directory; previous evidence will not be overwritten')
if options.probe_exe.name!='ConduitFridaProbe.exe':raise RuntimeError('Expected the owned ConduitFridaProbe.exe fixture')
if hashlib.file_digest(options.server_exe.open('rb'),'sha256').hexdigest()!='a7b0e82c4efdd12f579914369b83b955dd49dbccb0170ec7bd4730c78cc19749':raise RuntimeError('Server does not match the verified Windows x64 Frida 17.23.2 executable')
cmd=Path(f'/proc/{pid}/cmdline').read_bytes();assert b'Minecraft.Windows.exe' in cmd
keys={}
for value in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0'):
 k,_,v=value.partition(b'=')
 if k in [b'WINEPREFIX',b'PROTONPATH',b'WINEESYNC',b'WINEFSYNC',b'LD_LIBRARY_PATH',b'WINEDLLPATH']:keys[k.decode()]=v.decode()
wine=Path(keys['PROTONPATH'])/'files/bin/wine'
original=options.game_exe.resolve()
assert hashlib.file_digest(original.open('rb'),'sha256').hexdigest()=='1d6ad5c217e9305442c353ba3e9bca623d5719134e1cc8a84fb1233f7145e85c'
assert b'1.26.6005.0' in (original.parent/'MicrosoftGame.Config').read_bytes()
env=os.environ.copy();env.update(keys);env.update(WINEDEBUG='-all',WINEDLLOVERRIDES='mscoree,mshtml=')
temp=r/'frida-temp';temp.mkdir(exist_ok=True)
# Isolate owned Frida extraction from caches left by a prior interrupted server.
windowsTemp='Z:'+str(temp).replace('/',chr(92))
env.update(TEMP=windowsTemp,TMP=windowsTemp)
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
state={'linuxGamePid':pid,'wine':str(wine),'context':keys,'helperNamespaceTarget':pid,'endpoint':f'127.0.0.1:{port}'}
children=[]
try:
 for name,exe,args in [('server',options.server_exe.resolve(),['--directory',windowsTemp,'--listen',state['endpoint']]),('probe',options.probe_exe.resolve(),[])]:
  log=(r/f'game-prefix-{name}.log').open('w');p=subprocess.Popen(['nsenter','--target',str(pid),'--user','--mount','--preserve-credentials','--',str(wine),str(exe),*args],env=env,stdout=log,stderr=subprocess.STDOUT);children.append(p);state[name+'LinuxPid']=p.pid
 (r/'game-prefix-context.json').write_text(json.dumps(state,indent=2))
 deadline=time.monotonic()+20
 while time.monotonic()<deadline:
  if children[0].poll() is not None:raise RuntimeError('Server exited unexpectedly')
  try:
   with socket.create_connection(('127.0.0.1',port),timeout=.5):break
  except OSError:time.sleep(.25)
 else:raise RuntimeError('Server readiness timeout')
 def inspect(name,output):
  result=subprocess.run(['node',str(client),state['endpoint'],name,str(r/output)],timeout=35)
  if result.returncode:raise RuntimeError(f'{name} inspection failed; stop')
 inspect('ConduitFridaProbe.exe','game-prefix-probe-result.json')
 probe=json.loads((r/'game-prefix-probe-result.json').read_text());matches=[p for p in probe['processes'] if p['name'].lower()=='minecraft.windows.exe']
 if len(matches)!=1:raise RuntimeError('Expected exactly one Minecraft instance on remote server; do not attach')
 if not Path(f'/proc/{pid}').exists():raise RuntimeError('Original game is no longer alive; do not attach')
 state['remoteMinecraftPid']=matches[0]['pid'];(r/'game-prefix-context.json').write_text(json.dumps(state,indent=2))
 subprocess.run(['node',str(Path(__file__).with_name('monitor-legacy.mjs').resolve()),state['endpoint'],str(r)],check=True)
 print('Minecraft Linux PID still present:',Path(f'/proc/{pid}').exists())
finally:
 # Never kill the shared wineserver or signal Minecraft. Only stop owned helpers.
 for p in reversed(children):
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=5)
   except subprocess.TimeoutExpired:p.kill();p.wait(timeout=5)
 print('Owned test helpers cleaned; original game PID present:',Path(f'/proc/{pid}').exists())
