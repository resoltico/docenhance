"""Bind restored qualified products, wait one quiet interval, then replace this interpreter."""
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


def make_binding(workspace,pins,restoration):
    """Map retained qualified bytes to new owners, never synthesize a fresh producer."""
    owner=workspace/'restored-products'
    inventory=json.loads(body(HERE/'artifact-manifest.json',2*MIB))
    need(restoration.get('complete') is True,'Product restoration incomplete')
    artifact=restoration['artifact']
    need((artifact['id'],artifact['run_id'],artifact['head_sha'])==
         (12529461168,40010346336,'5bc5b45b1ecd107740c85a0aadfa594af8e878d7'),
         'Wrong original artifact/run/capsule')
    need({k:artifact['zip'][k] for k in ('bytes','sha256')}==inventory['zip'] and
         inventory['zip']=={'bytes':17125546,'sha256':'0cdfedd1748d4cd25c02d061c2fe418f268ce8871cb18a734691864188d8f4a8'},
         'Original ZIP identity differs')
    checked(artifact['zip'],32*MIB)
    need(inventory['source_commits']==pins['source_commits'],'Original source roles differ')
    files={}
    expected={n for n in inventory['files'] if n.startswith('performance-work/')}
    for row in restoration['files']:
        member=row['member'];need(member in expected and member not in files,'Unexpected/duplicate restored member')
        need(Path(row['local']['path'])==owner/'originals'/member,'Restored member owner differs')
        checked(row['local'])
        need({k:row['local'][k] for k in ('bytes','sha256')}==inventory['files'][member],
             'Restored member bytes differ from original manifest')
        wanted=0o755 if member in {v['member'] for v in inventory['products'].values()} else 0o644
        need(row['mode']==wanted and Path(row['local']['path']).lstat().st_mode&0o777==wanted,
             'Restored regular file mode differs')
        files[member]=row['local']
    need(set(files)==expected,'Restored original member set incomplete')
    result_member=inventory['producer_results']
    need(restoration['producer_results']=={'member':result_member,'local':files[result_member],'mode':0o644},
         'Restored producer result mapping differs')
    result=json.loads(body(files[result_member]['path'],8*MIB))
    need(result.get('complete') is True and result.get('product_builds')==2 and
         result.get('SUT_invocations')==0 and result.get('measurement_eligibility') is False and
         result.get('full_R17_acceptance') is False and
         result.get('owned_children_closure_confirmed') is True and result.get('claim_absent') is True,
         'Original producer was incomplete/adverse or not this two-product scope')
    original_workspace=Path(result['old']['checkout']).parent
    need(Path(result['old']['checkout'])==original_workspace/'sut-old' and
         Path(result['current']['checkout'])==original_workspace/'sut-current',
         'Original checkout roles differ')
    aliases={}
    def direct(original):
        path=Path(original['path'])
        need(path.is_relative_to(original_workspace),'Original receipt escapes producer workspace')
        member=path.relative_to(original_workspace).as_posix()
        need(member in files and all(original[k]==files[member][k] for k in ('bytes','sha256')),
             'Original receipt differs from restored member')
        return files[member]
    def mapped(original):
        if original['path'] in aliases:
            source,local=aliases[original['path']]
            need(source==original,'Copied metadata original receipt differs')
            return local
        return direct(original)
    def text(original):return body(mapped(original)['path'],2*MIB).decode('utf-8','strict')
    def generic(value):
        need(not re.search(r'(?:-march|-mcpu|-mtune|--?target-cpu)(?:=|\s)',value),
             'Host-tuned CPU code generation is not admitted for restored comparison')
    sources={};quals={};locals_by_role={};contexts={};context_logs={};live=[];dependency_names={}
    for role in ('old','current'):
        checkout=workspace/('sut-old' if role=='old' else 'sut-current')
        manifest=HERE/('source-'+role+'.json');source=json.loads(body(manifest,MIB))
        need(source['head']==pins['source_commits'][role] and
             receipt(manifest)['sha256']==pins['payload_sha256'][manifest.name],
             'Exact restored role source manifest differs')
        for name,digest in source['current_files'].items():
            path=checkout/name
            need(receipt(path)['sha256']==digest and oct(path.stat().st_mode&0o777)==source['current_modes'][name],
                 'Current checkout bytes/modes differ from original qualified source')
        sources[role]=receipt(manifest)
        value=result[role];need(value.get('complete') is True,'Original role producer incomplete')
        need(value['source']==dict(sources[role],path=str(original_workspace/'packet'/manifest.name)),
             'Original qualification source role differs')
        for entry in json.loads(text(value['retained_metadata'])):
            src,kept=entry['source'],entry['retained'];local=direct(kept)
            need(src['path'] not in aliases and all(src[k]==kept[k] for k in ('bytes','sha256')),
                 'Original copied metadata mapping differs/duplicates')
            aliases[src['path']]=(src,local)
        q=json.loads(text(value['qualification']));quals[role]=q
        need(q.get('complete') is True and q.get('kind')=='FRESH_SAME_HOST_PRODUCER_INLINE_QUALIFICATION' and
             q['source']==value['source'] and q['binary']==value['binary'],
             'Original qualification does not own the role source/binary')
        rows=q['actual_completed_rows']
        need(len(rows)==22 and all(r.get('complete') is True and r.get('exit_code')==0 and
             r.get('reaped') is True and r.get('readers_closed') is True for r in rows),
             'Original 22 command rows incomplete or closure unknown')
        for row in rows:direct(row['log'])
        contexts[role]=json.loads(text(value['static_context']))
        need(contexts[role]=={'observations':[{'command':r['argv'],'exit':r['exit_code'],
                     'stdout':text(r['log'])} for r in rows[:8]]},'Original static context differs from raw rows')
        context_logs[role]={}
        for name in ('os-version','linker-path','linker-version'):
            original=value['context_logs'][name]
            need([r['log'] for r in rows if r['name']=='before-'+name]==[original],
                 'Original context log belongs to another command/role')
            context_logs[role][name]=direct(original)
        product=inventory['products'][role];restored=restoration['products'][role]
        need(product['original']==value['binary'] and restored==
             {'member':product['member'],'local':files[product['member']],'mode':0o755} and
             all(value['artifact_binary'][k]==value['binary'][k] for k in ('bytes','sha256')) and
             direct(value['artifact_binary'])==restored['local'],'Product role/binary mapping differs')
        locals_by_role[role]=restored['local']
        outer=json.loads(text(q['build_identity']))
        need(outer['CMAKE_OSX_ARCHITECTURES']=='x86_64' and outer['CMAKE_OSX_DEPLOYMENT_TARGET']=='15.0' and
             outer['CMAKE_BUILD_TYPE']=='Release' and outer['DE_BUILD_TESTS'].upper() in ('ON','TRUE','1') and
             outer['DE_ENABLE_IPO'].upper() in ('OFF','FALSE','0',''),'Original generic Release context differs')
        for prefix,key in [('C','C_compiler_sha256'),('CXX','CXX_compiler_sha256')]:
            fact=receipt(outer['CMAKE_'+prefix+'_COMPILER'],64*MIB)
            need(fact['sha256']==outer[key],'Current compiler bytes differ from original producer')
            live.append(fact)
        generic(text(q['database']));generic(text(q['actual_commands']))
        lock=json.loads(body(checkout/'deps/lock.json',MIB));names={d['name'] for d in lock['dependencies']}
        dependency_names[role]=names
        need(len(q['dependency_cache_receipts'])==len(names),'Original native cache closure incomplete')
        observed=set()
        for cache in q['dependency_cache_receipts']:
            path=Path(cache['path']);need(path.name=='CMakeCache.txt' and path.parent.name in names and
                 path.parent.parent==Path(value['build'])/'deps','Native cache belongs to another prefix')
            observed.add(path.parent.name);generic(text(cache))
        need(observed==names,'Original native cache role set differs')
        generic(json.dumps({k:v for k,v in outer.items() if 'FLAGS' in k}))
        feature=receipt(checkout/'deps/features.json')
        need(q['native_features']==dict(feature,path=str(original_workspace/('sut-old' if role=='old' else 'sut-current')/'deps/features.json')),
             'Original native features differ from exact source')
        from package_inspection import native_import_values
        loader=next(r['log'] for r in rows if r['name']=='loader-imports')
        need(native_import_values('Darwin',text(loader))==q['loader_imports'],
             'Original binary loader closure is not admitted system-only metadata')
        need(any(r['name']=='dependency-audit' for r in rows),'Original native audit is absent')
    need(contexts['old']==contexts['current'],'Original old/current build contexts differ')
    need(len(dependency_names['old']|dependency_names['current'])==11,'Original native source union differs')
    git=checked(result['git'],64*MIB);version=result['git_version']
    rows=[r for r in result['metadata_observations'] if r.get('argv')==[git['path'],'--version']]
    need(len(rows)==1 and all(rows[0].get(k) is True for k in ('accepted','reaped','readers_closed')) and
         rows[0].get('exit_code')==0 and rows[0].get('observed_bytes')==len(version.encode()) and
         rows[0].get('observed_sha256')==hashlib.sha256(version.encode()).hexdigest(),
         'Original selected Git provenance/current bytes differ')
    image=receipt(workspace/'sut-current/corpus/images/armenian-incipit.jpg',3*MIB)
    need((image['bytes'],image['sha256'])==(2450909,'a642b1a0ce2201ad1da772c644300286bedad62f7e5d7c4eaae52d8b6aed258a'),
         'Frozen plain input differs')
    protocol=receipt(HERE/'protocol.json',MIB)
    need(protocol['sha256']=='2a7332fc804149a862da10e508f8d664ba02b1a87c5aef956b25a15661f53192','Original protocol differs')
    immutable=[*files.values(),*live,git,artifact['zip'],receipt(owner/'restoration.json',2*MIB)]
    for name,digest in pins['payload_sha256'].items():
        fact=receipt(HERE/name);need(fact['sha256']==digest,'Frozen restore/controller payload changed')
        immutable.append(fact)
    immutable.append(receipt(HERE/'manifest.json',2*MIB))
    unique={}
    for fact in immutable:
        need(fact['path'] not in unique or unique[fact['path']]==fact,'Conflicting immutable receipt')
        unique[fact['path']]=fact
    linker=body(context_logs['old']['linker-path']['path'],MIB).decode().strip()
    need(Path(linker).is_absolute() and '\n' not in linker and '\r' not in linker,'Original selected linker unavailable')
    return {'source':sources['current'],'old_source':sources['old'],
            'producer':direct(result['current']['qualification']),'old_producer':direct(result['old']['qualification']),
            'protocol':protocol,'baseline_identity':direct(result['old']['static_context']),
            'old_binary':locals_by_role['old'],'new_binary':locals_by_role['current'],'input':image,
            'old_checkout':str(workspace/'sut-old'),'old_commit':pins['source_commits']['old'],
            'current_commit':pins['source_commits']['current'],'old_context_logs':context_logs['old'],
            'git':git,'git_version':version,'selected_linker':linker,'frozen_options':[],
            'owned_here':str(HERE/'measurement'),'immutable':list(unique.values()),
            'restoration':receipt(owner/'restoration.json',2*MIB),'new_product_builds':0,
            'original_producer_results':files[result_member],
            'scope':'Restored byte-identical qualified04 products; original producer paths/context unmodified, current host comparability still required.'},time.time()


def compiler_readiness(binding,began):
    """Observe the cold compiler before quiet; this never executes either product."""
    from process_execution import bounded_capture, CapturePolicy
    original=json.loads(body(binding['baseline_identity']['path'],2*MIB))
    rows=[r for r in original['observations'] if r['command']==['/usr/bin/clang++','--version']]
    need(len(rows)==1 and rows[0]['exit']==0,'Original compiler version observation missing/ambiguous')
    expected=rows[0]['stdout'].encode('utf-8')
    need(len(expected)==209 and hashlib.sha256(expected).hexdigest()==
         'a5e70099f1ad266a05ca7076f2775f7c465aed305b3e51b182e5f8413abd8fe3',
         'Bound original compiler version differs')
    command=['/usr/bin/clang++','--version']
    facts=[r for r in binding['immutable'] if r['path']==command[0]]
    need(len(facts)==1 and receipt(command[0],64*MIB)==facts[0],
         'Current compiler bytes differ before readiness')
    log=HERE/'measurement/compiler-readiness.log';result_path=HERE/'measurement/compiler-readiness-result.json'
    need(not log.exists() and not log.is_symlink() and not result_path.exists(),
         'Compiler readiness scope already consumed')
    row={'argv':command,'cap_seconds':30,'retained_log_bytes':64*1024,
         'complete':False,'bounded_capture_returned':False,'child_group_reader_closure_confirmed':False,
         'expected_raw_bytes':209,'expected_raw_sha256':hashlib.sha256(expected).hexdigest(),
         'SUT_invocations':0,'scope':'One cold read-only compiler observation before fresh180quiet.'}
    failure=None
    def monitor():
        need(time.monotonic()-began<60,'No full180quiet room in original240prepair clock')
    try:
        monitor();timeout=min(30,60-(time.monotonic()-began));need(timeout>0,'No readiness work room')
        row['effective_timeout_seconds']=timeout
        row['exit_code']=bounded_capture(command,dict(os.environ),log,timeout,
                                        CapturePolicy(64*1024-1024,monitor,interval=1))
        row['bounded_capture_returned']=row['child_group_reader_closure_confirmed']=True
        need(row['exit_code']==0,'Cold compiler readiness command failed')
        need(body(log,64*1024)==expected,'Cold compiler readiness raw version differs')
        need(receipt(command[0],64*MIB)==facts[0],'Current compiler changed during readiness')
        monitor();row['complete']=True
    except BaseException as error:
        failure=error;row['error']=repr(error);row['notes']=getattr(error,'__notes__',[])
    finally:
        try:
            if log.exists():row['log']=receipt(log,64*1024)
        except BaseException as error:
            if failure is None:failure=error
            else:failure.add_note('Readiness log identity also failed: '+repr(error))
        if failure is not None:
            row.update(complete=False,error=repr(failure),notes=getattr(failure,'__notes__',[]))
        try:
            encoded=(json.dumps(row,indent=2)+'\n').encode();need(len(encoded)<=16*1024,'Readiness metadata exceeds16KiB')
            with result_path.open('xb')as stream:stream.write(encoded)
        except BaseException as error:
            if failure is None:failure=error
            else:failure.add_note('Readiness evidence retention also failed: '+repr(error))
    if failure is not None:raise failure
    binding['compiler_readiness']=receipt(result_path,16*1024)
    binding['immutable'].extend([receipt(log,64*1024),binding['compiler_readiness']])


def main():
    began=time.monotonic();workspace=Path(os.environ['GITHUB_WORKSPACE'])
    need(workspace.is_absolute(),'Unknown workspace')
    need(os.environ['GITHUB_RUN_ATTEMPT']=='1','One-shot scope refuses reruns')
    sys.path.insert(0,str(workspace/'sut-current/tools'))
    pins=json.loads(body(HERE/'manifest.json',2*MIB))
    for name,expected in pins['payload_sha256'].items():
        need(receipt(HERE/name)['sha256']==expected,'Frozen capsule payload differs: '+name)
    result_path=workspace/'restored-products/restoration.json'
    result_receipt=receipt(result_path,2*MIB)
    result=json.loads(body(result_path,2*MIB))
    binding,heavy=make_binding(workspace,pins,result)
    need(receipt(result_path,2*MIB)==result_receipt,'Restoration manifest changed')
    compiler_readiness(binding,began)
    heavy=time.time()
    encoded=(json.dumps(binding,indent=2)+'\n').encode()
    need(len(encoded)<=2*MIB,'Binding exceeds2MiB')
    destination=HERE/'measurement/bindings.json'
    with destination.open('xb') as stream:stream.write(encoded)
    binding_hash=hashlib.sha256(encoded).hexdigest()
    # Start quiet after binding verification as well as actual producer process return.
    quiet_started=time.monotonic()
    need(began+240-quiet_started>180,'No full quiet interval remains after restoration verification')
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
