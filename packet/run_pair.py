"""Seal actual fresh products, wait one quiet interval, then replace this interpreter."""
from pathlib import Path
import hashlib,json,math,os,re,stat,sys,time

MIB=1024**2
HERE=Path(__file__).resolve().parent


def need(value,message):
    if not value:
        raise RuntimeError(message)


def epoch(info):
    return (info.st_dev,info.st_ino,info.st_size,info.st_mtime_ns,info.st_ctime_ns)


def body(path,limit):
    path=Path(path);before=path.lstat()
    need(stat.S_ISREG(before.st_mode),'Bound file must be regular: '+str(path))
    descriptor=os.open(path,os.O_RDONLY|os.O_NONBLOCK|getattr(os,'O_NOFOLLOW',0))
    with os.fdopen(descriptor,'rb') as stream:
        opened=os.fstat(stream.fileno())
        need(stat.S_ISREG(opened.st_mode) and epoch(opened)==epoch(before),
             'Bound file changed before read: '+str(path))
        data=stream.read(limit+1)
        closed=os.fstat(stream.fileno())
    after=path.lstat()
    need(len(data)<=limit and len(data)==before.st_size,'Bound file exceeds allowance: '+str(path))
    need(epoch(before)==epoch(closed)==epoch(after),'Bound file epoch changed: '+str(path))
    return data


def receipt(path,limit=16*MIB):
    data=body(path,limit)
    return {'path':str(path),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}


def checked(entry,limit=16*MIB):
    need(isinstance(entry,dict) and set(entry)=={'path','bytes','sha256'},'Malformed receipt')
    need(isinstance(entry['path'],str) and Path(entry['path']).is_absolute(),'Receipt path not absolute')
    need(type(entry['bytes']) is int and 0<=entry['bytes']<=limit and
         isinstance(entry['sha256'],str) and re.fullmatch(r'[0-9a-f]{64}',entry['sha256']),
         'Malformed receipt identity')
    need(receipt(entry['path'],limit)==entry,'Actual receipt differs: '+entry['path'])
    return entry


def collect_evidence(root):
    need(root.is_dir() and not root.is_symlink(),'Producer evidence owner missing/redirected')
    pending=[root];files=[];entries=total=0
    while pending:
        with os.scandir(pending.pop()) as children:
            for child in children:
                entries+=1;need(entries<=4096,'Producer evidence exceeds4096 entries')
                mode=child.stat(follow_symlinks=False).st_mode
                if stat.S_ISDIR(mode):
                    pending.append(Path(child.path))
                else:
                    need(stat.S_ISREG(mode),'Producer evidence must be closed regular files')
                    item=receipt(child.path);files.append(item);total+=item['bytes']
                    need(total<=192*MIB,'Producer evidence exceeds192MiB')
    return sorted(files,key=lambda item:item['path'])


def make_binding(workspace,pins,result):
    need(result.get('complete') is True and result.get('owned_children_closure_confirmed') is True and
         result.get('claim_absent') is True,'Actual fresh producer is incomplete or ownership unknown')
    need(result.get('product_builds')==2 and result.get('SUT_invocations')==0 and
         result.get('measurement_eligibility') is False and result.get('full_R17_acceptance') is False,
         'Unexpected producer scope')
    heavy=result['heavy_work_finished_unix']
    need(type(heavy) in (int,float) and math.isfinite(heavy) and heavy<=time.time(),
         'Actual heavy-work closure clock missing/invalid')
    selected_git=checked(result['git'],64*MIB)
    version=result.get('git_version')
    need(isinstance(version,str) and re.fullmatch(r'git version [^\r\n \t]+[^\r\n]*(?:\r?\n)?',version),
         'Actual selected Git version unavailable')
    git_rows=[row for row in result.get('metadata_observations',[])
              if row.get('argv')==[selected_git['path'],'--version']]
    need(len(git_rows)==1 and git_rows[0].get('accepted') is True and
         git_rows[0].get('reaped') is True and git_rows[0].get('readers_closed') is True and
         git_rows[0].get('exit_code')==0 and
         git_rows[0].get('observed_bytes')==len(version.encode('utf-8')) and
         git_rows[0].get('observed_sha256')==hashlib.sha256(version.encode('utf-8')).hexdigest(),
         'Selected Git identity/version differs from its actual closed observation')
    work=workspace/'performance-work';sources={}
    for role in ('old','current'):
        path=HERE/('source-'+role+'.json');item=receipt(path,MIB)
        need(item['sha256']==pins['payload_sha256'][path.name],'Frozen source manifest differs')
        source=json.loads(body(path,MIB));need(source['head']==pins['source_commits'][role],'Wrong source commit')
        sources[role]=item
        value=result[role];need(value.get('complete') is True,'Actual role incomplete: '+role)
        for field in ('qualification','binary','artifact_binary','static_context'):
            checked(value[field])
        need(value.get('source')==item and value.get('checkout')==str(workspace/('sut-old' if role=='old' else 'sut-current')),
             'Actual producer role source/checkout differs')
        qualification=json.loads(body(value['qualification']['path'],2*MIB))
        need(qualification.get('complete') is True and
             qualification.get('kind')=='FRESH_SAME_HOST_PRODUCER_INLINE_QUALIFICATION' and
             qualification.get('source')==item and qualification.get('binary')==value['binary'],
             'Actual qualification does not own this exact source/binary role')
        qualified_rows=qualification.get('actual_completed_rows')
        need(type(qualified_rows) is list and len(qualified_rows)==22 and all(
            row.get('complete') is True and row.get('exit_code')==0 and
            row.get('reaped') is True and row.get('readers_closed') is True
            for row in qualified_rows),'Actual qualified command rows incomplete')
        for row in qualified_rows:
            checked(row['log'],2*MIB)
        expected_context={'observations':[{'command':row['argv'],'exit':row['exit_code'],
                          'stdout':body(row['log']['path'],2*MIB).decode('utf-8','strict')}
                          for row in qualified_rows[:8]]}
        need(json.loads(body(value['static_context']['path'],2*MIB))==expected_context,
             'Static producer context differs from its actual qualified command rows')
        need(value['binary']['path']==str(work/role/'bin/docenhance'),'Actual binary outside exact fresh owner')
        need(value['artifact_binary']['sha256']==value['binary']['sha256'] and
             value['artifact_binary']['bytes']==value['binary']['bytes'],'Retained CLI copy differs')
        for field in ('os-version','linker-path','linker-version'):
            checked(value['context_logs'][field],2*MIB)
            matches=[row['log'] for row in qualified_rows if row.get('name')=='before-'+field]
            need(matches==[value['context_logs'][field]],'Context log differs from qualified role')
    old=result['old'];current=result['current']
    old_context=json.loads(body(old['static_context']['path'],2*MIB))
    current_context=json.loads(body(current['static_context']['path'],2*MIB))
    need(old_context==current_context,'Actual fresh producer static contexts differ')
    observations=old_context['observations']
    need(type(observations) is list and len(observations)==8 and all(
        row.get('exit')==0 and isinstance(row.get('stdout'),str) and row['stdout'].strip()
        for row in observations),'Incomplete actual static context')
    linker=body(old['context_logs']['linker-path']['path'],2*MIB).decode('utf-8','strict').strip()
    need(Path(linker).is_absolute() and '\n' not in linker and '\r' not in linker,'Unknown selected linker')
    protocol=receipt(HERE/'protocol.json',MIB)
    need(protocol['sha256']=='2a7332fc804149a862da10e508f8d664ba02b1a87c5aef956b25a15661f53192',
         'Original frozen performance protocol differs')
    input_file=workspace/'sut-current/corpus/images/armenian-incipit.jpg';image=receipt(input_file,3*MIB)
    need(image['sha256']=='a642b1a0ce2201ad1da772c644300286bedad62f7e5d7c4eaae52d8b6aed258a' and
         image['bytes']==2450909,'Original input differs')
    immutable=collect_evidence(work/'evidence')
    immutable.append(selected_git)
    immutable.extend(receipt(HERE/name) for name in pins['payload_sha256'])
    immutable.append(receipt(HERE/'manifest.json',2*MIB))
    binding={'source':sources['current'],'old_source':sources['old'],
             'producer':current['qualification'],'old_producer':old['qualification'],
             'protocol':protocol,'baseline_identity':old['static_context'],
             'old_binary':old['binary'],'new_binary':current['binary'],'input':image,
             'old_checkout':str(workspace/'sut-old'),'old_commit':pins['source_commits']['old'],
             'current_commit':pins['source_commits']['current'],'old_context_logs':old['context_logs'],
             'git':selected_git,'git_version':result['git_version'],
             'selected_linker':linker,'frozen_options':[], 'owned_here':str(HERE/'measurement'),
             'immutable':immutable}
    return binding,heavy


def main():
    began=time.monotonic();workspace=Path(os.environ['GITHUB_WORKSPACE'])
    need(workspace.is_absolute(),'Unknown workspace')
    need(os.environ['GITHUB_RUN_ATTEMPT']=='1','One-shot scope refuses reruns')
    pins=json.loads(body(HERE/'manifest.json',2*MIB))
    for name,expected in pins['payload_sha256'].items():
        need(receipt(HERE/name)['sha256']==expected,'Frozen capsule payload differs: '+name)
    result_path=workspace/'performance-work/results.json'
    result_receipt=receipt(result_path,8*MIB)
    result=json.loads(body(result_path,8*MIB))
    binding,heavy=make_binding(workspace,pins,result)
    need(receipt(result_path,8*MIB)==result_receipt,'Actual producer result changed')
    binding['immutable'].append(result_receipt)
    encoded=(json.dumps(binding,indent=2)+'\n').encode()
    need(len(encoded)<=2*MIB,'Binding exceeds2MiB')
    destination=HERE/'measurement/bindings.json'
    with destination.open('xb') as stream:stream.write(encoded)
    binding_hash=hashlib.sha256(encoded).hexdigest()
    # Start quiet after binding verification as well as actual producer process return.
    quiet_started=time.monotonic()
    while True:
        now=time.monotonic()
        need(now-began<240,'Original pre-pair240s deadline exhausted')
        quiet_elapsed=now-quiet_started
        if quiet_elapsed>=180:
            break
        time.sleep(min(30,180-quiet_elapsed))
    need(time.monotonic()-began<240,'Original pre-pair240s deadline exhausted')
    need(receipt(destination,2*MIB)['sha256']==binding_hash,'Fresh binding changed')
    need(receipt(HERE/'measure.py')['sha256']==pins['payload_sha256']['measure.py'],'Driver changed')
    args=[sys.executable,'-B',str(HERE/'measure.py'),'--heavy-work-finished-unix',str(heavy),
          '--binding-sha256',binding_hash]
    # No controller Python process remains to defeat the independent process census.
    os.execve(sys.executable,args,dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))


if __name__=='__main__':
    main()
