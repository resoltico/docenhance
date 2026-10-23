"""One fixed paired plain workload diagnostic, only after actual quiet eligibility."""
from pathlib import Path
import argparse,datetime,hashlib,json,math,os,re,shutil,sys,time
import threading,subprocess,stat
ROOT=Path(os.environ['GITHUB_WORKSPACE'])/'sut-current';HERE=Path(__file__).resolve().parent/'measurement';OUT=HERE/'execution'
sys.path.insert(0,str(ROOT/'tools'))
import process_execution as execution
from cache_lock import exclusive_cache, WriterStateUnknownError
from check_performance_host import _power_causes, LOW_POWER_SCRIPT
from capture_reader import finish_capture_reader, capture_or_drain
from jsonschema import Draft202012Validator,ValidationError
parser=argparse.ArgumentParser();parser.add_argument('--heavy-work-finished-unix',type=float,required=True);parser.add_argument('--binding-sha256',required=True)
args=parser.parse_args();start=time.monotonic();deadline=start+524
binding=json.loads((HERE/'bindings.json').read_bytes());source=json.loads(Path(binding['source']['path']).read_bytes())
OLD=Path(binding['old_binary']['path']);NEW=Path(binding['new_binary']['path']);INPUT=Path(binding['input']['path'])
rows=[];trials=[{'name':n,'status':'not_started'} for n in ['old-process','old-verify','new-process','new-verify']];primary=None;eligible=False;child_cwd=ROOT;claim=None;claimed=False
def need(value, message):
    if not value:
        raise RuntimeError(message)


def receipt(path):
    path = Path(path)
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def stable():
    for field in ['source','producer','old_producer','protocol','baseline_identity','old_source']:
        entry=binding[field];need(receipt(Path(entry['path']))==entry,'Bound receipt changed: '+field)
    for entry in binding['immutable']:
        need(receipt(Path(entry['path']))==entry,'Bound immutable receipt changed')
    for field in ['old_binary','new_binary','input']:
        entry=binding[field];need(receipt(Path(entry['path']))['sha256']==entry['sha256'],'Bound bytes changed: '+field)
    old_source=json.loads(Path(binding['old_source']['path']).read_bytes())
    for name,digest in old_source['current_files'].items():
        path=Path(binding['old_checkout'])/name;need(path.is_file() and receipt(path)['sha256']==digest and oct(path.stat().st_mode&0o777)==old_source['current_modes'][name],'Frozenoldsourcechanged:'+name)
    for name,digest in source['current_files'].items():
        path=ROOT/name;need(path.is_file() and receipt(path)['sha256']==digest and oct(path.stat().st_mode&0o777)==source['current_modes'][name],'Frozen source changed: '+name)


def occupied():
    total=0
    for folder,dirs,names in os.walk(OUT,followlinks=False):
        for name in [*dirs,*names]:
            path=Path(folder)/name;need(not path.is_symlink(),'Unexpected evidence symlink')
            total+=path.stat().st_blocks*512
    return total


def monitor():
    need(time.monotonic()<deadline,'Original524s diagnostic deadline exhausted')
    need(occupied()<=192*1024**2,'Owned allocated evidence exceeds192MiB')
    need(shutil.disk_usage(OUT).free>=3*1024**3,'Free space below3GiB')

def capture(command, directory, seconds, row, split=False):
    """Own/reap the child group and join every bounded reader before parsing outputs."""
    pending = None
    readers = []
    failures = []
    streams = []
    row['reaped'] = row['readers_closed'] = False
    paths = [directory/'stdout.json', directory/'stderr.log'] if split else [directory/'diagnostics.log']
    try:
        for path in paths:
            streams.append(path.open('xb'))
        output = (subprocess.PIPE, subprocess.PIPE) if split else subprocess.PIPE
        try:
            with execution.owned_process(command, dict(os.environ), output, cwd=child_cwd,
                    graceful=command[0] == 'ctest' and '--tests-from-file' in command) as process:
                try:
                    pipes = [process.stdout, process.stderr] if split else [process.stdout]
                    for pipe, stream in zip(pipes, streams, strict=True):
                        need(pipe is not None, 'No owned capture pipe')
                        reader = threading.Thread(target=capture_or_drain,
                            args=(pipe, stream, (256 if row['name'] in {t['name'] for t in trials} else 64)*1024, failures), daemon=True)
                        readers.append((reader, pipe, stream))
                        reader.start()
                    def check():
                        if failures:
                            raise OSError(str(failures[0]))
                        monitor()
                    row['exit_code'] = execution._wait_monitored(process, command, seconds, check, 1)
                except BaseException as error:
                    pending = error
            row['reaped'] = True
        except BaseException as error:
            if pending is None:
                pending = error
            else:
                pending.add_note('Owned cleanup also failed: '+repr(error))
    except BaseException as error:
        if pending is None:
            pending = error
        elif error is not pending:
            pending.add_note('Capture setup also failed: '+repr(error))
    finally:
        closed = True
        retained_streams = set()
        owned_streams = {id(stream) for _, _, stream in readers}
        for reader, pipe, stream in readers:
            try:
                finish_capture_reader(reader, pipe, stream,
                    min(10, max(0, deadline-time.monotonic())), pending)
            except BaseException as error:
                closed = False
                retained_streams.add(id(stream))
                if pending is None:
                    pending = error
                elif error is not pending:
                    pending.add_note('Capture reader cleanup also failed: '+repr(error))
        row['readers_closed'] = closed and bool(readers)
        row['retained_unconfirmed_sink_count'] = len(retained_streams)
        for stream in streams:
            if id(stream) in owned_streams:
                # The shared helper consumed or retained both reader borrows; never close twice.
                continue
            try:
                stream.close()
            except BaseException as error:
                row['readers_closed'] = False
                if pending is None:
                    pending = error
                else:
                    pending.add_note('Capture stream close also failed: '+repr(error))
        try:
            row['logs'] = [receipt(path) for path in paths if path.exists()]
        except BaseException as error:
            if pending is None:
                pending = error
            else:
                pending.add_note('Capture identity also failed: '+repr(error))
    if failures and pending is None:
        pending = OSError(str(failures[0]))
    if pending is not None:
        raise pending
    need(row.get('exit_code') == 0, 'Actual child failed: '+str(row.get('exit_code')))
    need(all(b'[engine output omitted:' not in path.read_bytes() for path in paths),
         'Bounded output truncated; no qualification')


def invoke(name,argv,cap,cwd=None):
    global child_cwd
    directory=OUT/name;directory.mkdir();row={'name':name,'argv':argv,'complete':False,'cap_seconds':cap}
    rows.append(row);began=time.monotonic();previous=child_cwd;child_cwd=cwd or ROOT
    try:
        monitor();seconds=min(cap,deadline-time.monotonic()-30);need(seconds>0,'No child/cleanup room')
        capture(argv,directory,seconds,row,True);row['complete']=True
    except BaseException as error:
        row['error']=repr(error);row['notes']=getattr(error,'__notes__',[]);raise
    finally:
        child_cwd=previous;row['elapsed_seconds']=time.monotonic()-began
        (directory/'result.json').write_text(json.dumps(row,indent=2)+'\n')
    return directory,(directory/'stdout.json').read_text()


def context(label,cpu=False):
    state={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'loadavg':os.getloadavg(),'observations':{}}
    queries=[('power',['/usr/bin/pmset','-g','batt']),('thermal',['/usr/bin/pmset','-g','therm']),('power_modes',['/usr/bin/pmset','-g','custom']),('low_power',['/usr/bin/osascript','-l','JavaScript','-e',LOW_POWER_SCRIPT]),('processes',['/bin/ps','-wwaxco','pid=,pcpu=,comm='])]
    if cpu:queries.append(('cpu',['/usr/bin/top','-l','2','-s','1','-n','0']))
    for name,argv in queries:
        directory,text=invoke(label+'-'+name,argv,6 if name=='cpu' else 4);state['observations'][name]={'stdout':text,'stderr':(directory/'stderr.log').read_text(),'exit':rows[-1]['exit_code'],'row':label+'-'+name}
    if cpu:
        matches=re.findall(r'CPU usage:.*?([0-9.]+)% idle',state['observations']['cpu']['stdout'])
        state['idle_percent']=float(matches[-1]) if matches else None
    return state


def machine_eligible(state,require_idle=True):
    obs=state['observations'];causes=[]
    if any(v['exit']!=0 or v['stderr'].strip() for v in obs.values()):causes.append('Unavailableoradverseenvironmentobservation')
    if not obs['processes']['stdout'].strip():causes.append('Unknownprocessobservation')
    if require_idle and (state.get('idle_percent') is None or state['idle_percent']<90):causes.append('Idlebelow90orunknown')
    causes.extend(_power_causes({name:obs[name]['stdout'] for name in ('power','power_modes','low_power','thermal')}))
    process_rows=obs['processes']['stdout'].splitlines()
    if not process_rows or len(process_rows)>4096:causes.append('Unknownprocesscensus')
    pids=set();own_seen=False
    for line in process_rows:
        fields=line.split(None,2)
        if len(fields)!=3 or not fields[0].isdigit() or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?',fields[1]) or not fields[2].strip():
            causes.append('Malformedprocessidentity');continue
        pid=int(fields[0]);program=Path(fields[2].strip()).name
        if pid in pids:causes.append('Duplicateprocessidentity')
        pids.add(pid)
        if not program or program.startswith(('[','<')):causes.append('Unknownexecutableidentity')
        python=program.lower().startswith(('python','pypy'))
        heavy=re.fullmatch(r'(?:clang(?:\+\+|-tidy)?|gcc|g\+\+|cc|c\+\+)(?:-[0-9]+)?',program) is not None or program in {'ninja','ninja-build','cc1','cc1plus','ctest','cmake','docenhance','check_all.py','run_tooling_tests.py','check_reference_suite.py','run_fuzz_campaign.py'} or program.startswith('de_')
        if heavy:causes.append('Activeheavyworkflow:'+fields[2].strip())
        if pid==os.getpid():
            own_seen=True
            if not python:causes.append('Owninterpreteridentityunknown')
        elif python:causes.append('OtherPythoninterpreter:'+str(pid))
    if not own_seen:causes.append('Ownprocessabsentfromcensus')
    return sorted(set(causes))


def parse_response(directory,checkout):
    need(not (directory/'stderr.log').read_bytes(),'JSON product emittedstderr')
    response=json.loads((directory/'stdout.json').read_bytes())
    Draft202012Validator(json.loads((checkout/'schemas/command-response.schema.json').read_bytes())).validate(response)
    need(response['exit_code']==0,'Response exit mismatch');return response



def bundle_inventory(bundle):
    need(stat.S_ISDIR(bundle.lstat().st_mode),'Ownbundlemustberealdirectory')
    entries=sorted(bundle.iterdir())
    need({entry.name for entry in entries}=={'run.json','result.png'},'Ownflatbundlenamesdiffer')
    inventory=[]
    for entry in entries:
        mode=entry.lstat().st_mode
        need(stat.S_ISREG(mode),'Ownflatbundleentrymustberegularfile')
        inventory.append({'name':entry.name,'type':stat.S_IFMT(mode),'file':receipt(entry)})
    return inventory

def trial(label,binary,checkout,verify=False):
    stable();before=context(label+'-before')
    need(not machine_eligible(before,False),'Per-trialpower/heavyworkconditionsrefused')
    directory=OUT/label;bundle=(OUT/label.replace('-verify','-process')/'bundle') if verify else directory/'bundle'
    args0=[str(binary),'verify',str(bundle),'--json'] if verify else [str(binary),'process',str(INPUT),'--out-dir',str(bundle),'--json',*binding['frozen_options']]
    timer=['/usr/bin/time','-l','-o',str(directory/'time.raw'),*args0]
    fact=next(t for t in trials if t['name']==label)
    fact.update(argv=args0,status='not_started',conditions_before=before,publication=None if verify else 'unknown')
    failure=None
    before_bundle=bundle_inventory(bundle) if verify else None
    try:
        fact['status']='running';got,_=invoke(label,timer,45,checkout)
        response=parse_response(got,checkout);fact['response_exit']=response['exit_code']
        need(response['command']==('verify' if verify else 'process'),'Owncommandresponsekinddiffers')
        if not verify:
            fact['publication']=response.get('publication','unknown');need(fact['publication']=='completed','Process publication notcompleted')
            need((bundle/'run.json').stat().st_size<=2*1024**2,'Ownedrecordexceeds2MiB')
            need((bundle/'result.png').stat().st_size<=51*1024**2,'OwnedPNGexceeds51MiBfixedinputbound')
            record=json.loads((bundle/'run.json').read_bytes())
            Draft202012Validator(json.loads((checkout/'schemas/run-record.schema.json').read_bytes())).validate(record)
            record_receipt=receipt(bundle/'run.json')
            need(record['source']['sha256']==binding['input']['sha256'],'Exacttrialinputidentitydiffers')
            if binary==OLD:
                decoding=record['source']['decoding']
                need((decoding['width'],decoding['height'],decoding['precision'])==(2179,3000,8),'Baseline14inputgeometrydiffers')
                reference=response['record']
                need(reference['path']=='run.json' and reference['run']==record['record']['run'],
                     'Baseline14recordreferencepath/run differs')
                need(reference['sha256']==record_receipt['sha256'] and reference['bytes']==record_receipt['bytes'],
                     'Baseline14recordreferencedigest/bytes differs')
                fact['record_binding']='Baseline14schema-owned digestreference'
            else:
                need(binary==NEW,'Unexpectedcomparatorbinary')
                container=record['source']['container']
                need((container['width'],container['height'],container['precision'])==(2179,3000,8),'Current15inputgeometrydiffers')
                need(response['record']==record and response['record_sha256']==record_receipt['sha256'],
                     'Current15fullrecord/digest differs')
                fact['record_binding']='Current15schema-owned fullrecordanddigest'
            fact['record_receipt']=record_receipt
        else:
            need(response.get('establishes')=='artifacts_agree_with_record','Ownverifydidnotestablishagreement')
            need(response.get('run')==json.loads((bundle/'run.json').read_bytes())['record']['run'],'Ownverifyresponserundiffers')
            need(response['directory']==str(bundle),'Ownverifyresponsebundlediffers')
            need(before_bundle==bundle_inventory(bundle),'Read-onlyverify changedbundle')
        raw=(directory/'time.raw').read_text();timing=re.search(r'([0-9.]+)\s+real\s+([0-9.]+)\s+user\s+([0-9.]+)\s+sys',raw);rss=re.search(r'(\d+)\s+maximum resident set size',raw)
        need(timing and rss,'Native timing/RSS absent');fact.update(wall_seconds=float(timing[1]),user_seconds=float(timing[2]),sys_seconds=float(timing[3]),max_rss_bytes=int(rss[1]),status='completed')
    except BaseException as error:
        failure=error;fact['status']='failed';fact['error']=repr(error)
        # RetainedcompleteJSON mayconfirmcommit even whenanotherindependentcheckfailed.
        try:
            raw=json.loads((directory/'stdout.json').read_bytes())
            Draft202012Validator(json.loads((checkout/'schemas/command-response.schema.json').read_bytes())).validate(raw)
            fact['observed_response']=raw
            if not verify and isinstance(raw,dict) and raw.get('publication')=='completed':fact['publication']='completed'
        except (OSError,ValueError,ValidationError):pass
        raise
    finally:
        try:
            fact['retained_files']=[receipt(p) for p in sorted(directory.rglob('*')) if p.is_file()]
            fact['conditions_after']=context(label+'-after');fact['owned_allocated_bytes']=occupied()
            fact['conditions_after_causes']=machine_eligible(fact['conditions_after'],False)
            need(not fact['conditions_after_causes'],'Post-trialenvironmentadverse;stopremainingpair')
            stable()
        except BaseException as cleanup_error:
            fact['finalization_error']=repr(cleanup_error)
            if failure is None:raise
            if cleanup_error is not failure:failure.add_note('Trial finalization also failed: '+repr(cleanup_error))


try:
    need(not OUT.exists() and not OUT.is_symlink(),'Once-onlyscopeconsumed')
    need(Path(binding['owned_here'])==HERE,'Exact owned measurement path differs')
    OUT.mkdir()
    claim=exclusive_cache(ROOT/'.cache/linux-gate/active');claim.__enter__();claimed=True
    need(re.fullmatch(r'[0-9a-f]{64}',args.binding_sha256) is not None and receipt(HERE/'bindings.json')['sha256']==args.binding_sha256,'Frozen bindingfile differs')
    need(math.isfinite(args.heavy_work_finished_unix) and time.time()-args.heavy_work_finished_unix>=180,'Actual180squietcutoffnotyetattained')
    stable();old_checkout=Path(binding['old_checkout'])
    _,current_head=invoke('current-head',[binding['git']['path'],'-C',str(ROOT),'rev-parse','HEAD'],5);need(current_head.strip()==binding['current_commit'],'CurrentsourceHEADdiffers')
    _,current_clean=invoke('current-clean',[binding['git']['path'],'-C',str(ROOT),'status','--porcelain=v1'],5);need(not current_clean.strip(),'Currentcheckoutnotclean')
    _,head=invoke('old-head',[binding['git']['path'],'-C',str(old_checkout),'rev-parse','HEAD'],5);need(head.strip()==binding['old_commit'],'Baselinecommitchanged')
    _,clean=invoke('old-clean',[binding['git']['path'],'-C',str(old_checkout),'status','--porcelain=v1'],5);need(not clean.strip(),'Baselinecheckoutnotclean')
    identity=json.loads(Path(binding['baseline_identity']['path']).read_bytes())
    observations={tuple(v['command']):v for v in identity['observations']}
    platform_observations={}
    for name,argv in [('OS',['/usr/bin/sw_vers']),('compiler',['/usr/bin/clang++','--version']),('SDK',['/usr/bin/xcrun','--show-sdk-version']),('machine',['/usr/sbin/sysctl','-n','hw.memsize','hw.ncpu','machdep.cpu.brand_string'])]:
        _,text=invoke('comparability-'+name,argv,4);platform_observations[name]=text
        if name=='OS':
            need(text.strip()==Path(binding['old_context_logs']['os-version']['path']).read_text().strip(),'ActualOSdiffersfromfreshproducers')
        else:
            old=observations.get(tuple(argv));need(old is not None and old['exit']==0 and old['stdout'].strip()==text.strip(),'CompilerSDKmachinecomparabilitydiffersorunknown:'+name)
    for name,argv in [('linker-path',['/usr/bin/xcrun','--find','ld']),('linker-version',[binding['selected_linker'],'-version_details'])]:
        _,text=invoke('comparability-'+name,argv,4);need(text.strip()==Path(binding['old_context_logs'][name]['path']).read_text().strip(),'Actualselectedlinkerdiffersfromfreshproducers')
    eligibility=[context('eligibility-'+str(i),True) for i in range(3)]
    causes=[machine_eligible(s) for s in eligibility]
    eligibility_text=json.dumps({'samples':eligibility,'causes':causes,'quiet_cutoff_unix':args.heavy_work_finished_unix,'platform_observations':platform_observations,'build_context':'Bothfreshsame-hostpinnedXcodeproducers/compiler/SDK/selectedlinkerbeforeaftermatch; singleplainpaironly,nofullR17acceptance.'},indent=2)+'\n'
    need(len(eligibility_text.encode())<=12*1024**2,'Eligibilitymetadataexceeds12MiB');(OUT/'eligibility.json').write_text(eligibility_text);monitor()
    need(not any(causes),'Eligibilityrefused;ZEROpairedSUTinvocations')
    eligible=True
    for label,binary,checkout,verify in [('old-process',OLD,old_checkout,False),('old-verify',OLD,old_checkout,True),('new-process',NEW,ROOT,False),('new-verify',NEW,ROOT,True)]:trial(label,binary,checkout,verify)
except BaseException as error:
    primary=error
finally:
    if OUT.exists():
        try:stable();monitor()
        except BaseException as final_error:
            if primary is None:primary=final_error
            elif final_error is not primary:primary.add_note('Final guard also failed: '+repr(final_error))
        result={'complete':primary is None and len(trials)==4 and all(t['status']=='completed' for t in trials),'eligibility_admitted':eligible,'error':repr(primary) if primary is not None else None,'notes':getattr(primary,'__notes__',[]) if primary is not None else [],'trials':trials,'actual_rows':rows,'diagnostic_only':True,'full_R17_acceptance':False,'scope':'Onefrozenplainold/newpair+eachownverifyonly. Preservealloutcomes;novariance/noise/all10rowsacceptance;nonaturalqualityclaim.','elapsed_seconds':time.monotonic()-start}
        result['owned_children_closure_confirmed']=all(row.get('reaped') and row.get('readers_closed') for row in rows)
        if claimed:
            try:
                marker=None if result['owned_children_closure_confirmed'] else WriterStateUnknownError('Singlepairownedchildclosureunknown')
                claim.__exit__(type(marker) if marker else None,marker,None)
                result['claim_retained_for_inspection']=marker is not None
            except BaseException as error:
                result['complete']=False;result['claim_release_error']=repr(error)
                if primary is None:primary=error
                else:primary.add_note('Claimreleasealsofailed:'+repr(error))
        result['claim_absent']=not (ROOT/'.cache/linux-gate/active').exists()
        result_text=json.dumps(result,indent=2)+'\n'
        need(len(result_text.encode())<=36*1024**2,'Resultsmetadataexceeds36MiB')
        (OUT/'results.json').write_text(result_text)
        try:
            stable();monitor()
            need(result['owned_children_closure_confirmed'],'Ownedchildclosureunconfirmed')
            need(result['claim_absent'],'Ownedclaimnotreleased')
        except BaseException as error:
            result['complete']=False;result['post_write_guard_error']=repr(error)
            if primary is None:primary=error
            else:primary.add_note('Postwriteguardalsofailed:'+repr(error))
        result['elapsed_seconds']=time.monotonic()-start
        result['error']=repr(primary) if primary is not None else None
        result_text=json.dumps(result,indent=2)+'\n'
        need(len(result_text.encode())<=36*1024**2,'Resultsmetadataexceeds36MiB')
        (OUT/'results.json').write_text(result_text)
        try:monitor()
        except BaseException as error:
            result['complete']=False;result['post_write_budget_error']=repr(error)
            if primary is None:primary=error
            else:primary.add_note('Finalmetadataobservationfailed:'+repr(error))
            result['error']=repr(primary);result['elapsed_seconds']=time.monotonic()-start
            (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
if primary is not None:raise primary
print('COMPLETE: one diagnostic pair only; fullR17 remains open')
