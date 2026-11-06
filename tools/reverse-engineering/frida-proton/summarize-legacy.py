"""Aggregate saved observation batches; no connection to the target."""
import collections,json,sys
from pathlib import Path
r=Path(sys.argv[1]);counts=collections.Counter();threads=collections.defaultdict(set);groups=[];objects={};status=None
with (r/'events.jsonl').open() as f:
 for line in f:
  try:batch=json.loads(line)
  except json.JSONDecodeError:continue
  status={k:batch.get(k) for k in ('kind','hostTime','counts')}
  for e in batch.get('events',[]):
   if e['phase']!='enter':continue
   name=e['name'];counts[name]+=1;threads[name].add(e['tid'])
   if not groups or e['time']-groups[-1]['last']>2000:groups.append({'first':e['time'],'last':e['time'],'counts':{},'order':[]})
   g=groups[-1];g['last']=e['time'];g['counts'][name]=g['counts'].get(name,0)+1
   if len(g['order'])<30:g['order'].append([e['seq'],name,e['tid'],e['rcx'],e['returnAddress']])
   for key in ('contextR8','context','owner'):
    if key in e:objects[json.dumps(e[key],sort_keys=True)]=e[key]
print(json.dumps({'counts':dict(counts),'threads':{k:sorted(v) for k,v in threads.items()},'groups':groups,'objects':list(objects.values()),'latestStatus':status},indent=2))
