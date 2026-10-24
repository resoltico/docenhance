"""One finite MSVC microcomparison plus original196 two-case timing-only observation."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,sys,time
from support import identity,need,scan,source_check,index_modes,capture_adapter,write_results
PACKET=Path(__file__).resolve().parent

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.source.resolve();out=a.output.resolve();work=out.with_name(out.name+'-work');need(not out.exists() and not work.exists() and not(root/'out/dev').exists(),'Fresh owners required');out.mkdir();work.mkdir();os.chdir(root)
 sys.path.insert(0,str(root/'tools'))
 from process_execution import CapturePolicy,bounded_capture
 import windows_resource_execution as execution
 import windows_resource_records as records
 from native_history import owned_native_history
 from native_journal import snapshot_ctest_output
 from native_output import archive_ctest_output
 from restoration_resource_cases import RESOURCE_PATTERN,resource_cases
 parent=json.loads((PACKET/'source-manifest.json').read_bytes());overlay=json.loads((PACKET/'overlay.json').read_bytes());start=time.monotonic();deadline=start+2820
 result={'kind':'INSTRUMENTED_ORIGINAL196_DIAGNOSTIC_NOT_QUALIFICATION','base_head':parent['head'],'lookup_profile':{},'commands':[],'cases_to_observe':['fft-native-resources','restoration-native-resources-0'],'unstarted_other_resource_cases':16,'native_validation_passed':False,'observation_complete':False,'caps':{'profile_total_seconds':20,'Debug_owner_seconds':2820,'per_case_seconds':90,'parallel':2,'native_history_MiB':384,'run_reserve_MiB':136,'log_bytes':2*1024**2,'build_sampled_stop_MiB':2048}}
 def monitor():
  need(time.monotonic()<deadline,'Original2820s work clock exhausted');scan(root/'out/dev',2*1024**3,100000);scan(work,128*1024**2,512);scan(out,32*1024**2,256)
 def run(name,argv,seconds,limit=2*1024**2):
  need(seconds>0,'Original profile/command clock exhausted before launch');monitor();row={'name':name,'argv':argv,'cap':min(seconds,deadline-time.monotonic())};result['commands'].append(row);log=out/(name+'.log');entered=time.monotonic()
  try:row['exit_code']=bounded_capture(argv,dict(os.environ),log,row['cap'],CapturePolicy(limit,monitor,1))
  finally:
   row['elapsed_seconds']=time.monotonic()-entered;row['error']=repr(sys.exception()) if sys.exception() else None
   if log.exists():row['log']=identity(log,limit)
  need(row['exit_code']==0 and b'[engine output omitted:'not in log.read_bytes(),'Actual diagnostic command failed or truncated: '+name);return log.read_text(encoding='utf-8').rstrip('\r\n')
 try:
  need(identity(PACKET/'source-manifest.json')['sha256']=='cece18a1a40cc70f8e5421b62172734b24ff5394227f0ecace404bda057fcf74' and parent['head']=='a12a0fdd5ba5a508c8d604685546d34f6e0f876d','Base source pin differs');source_check(root,parent)
  git=shutil.which('git');need(git is not None,'Git absent');need(run('head-before',[git,'rev-parse','HEAD'],10)==parent['head'],'Checkout head differs');need(not run('status-before',[git,'status','--porcelain','--untracked-files=all'],10),'Checkout dirty');index_modes(run('index-before',[git,'ls-files','--stage','-z'],10),parent)
  run('runtime',[sys.executable,'tools/check_runtime_platform.py','--platform','windows-x86_64'],10)
  compiler=shutil.which('cl.exe');need(compiler is not None,'Selected cl absent');result['compiler']=identity(Path(compiler));result['MSVC_include_root']=os.environ.get('VCToolsInstallDir');result['benchmark_sources']=json.loads((PACKET/'benchmark-sources.json').read_bytes());need(identity(PACKET/'original/allocation_observer.hpp')['sha256']==parent['current_files']['tests/support/allocation_observer.hpp'] and result['benchmark_sources']['original']['source_cpp_sha256']==parent['current_files']['tests/support/allocation_observer.cpp'],'Micro original source is not actual196');need(identity(PACKET/'candidate/allocation_observer.hpp')['sha256']==result['benchmark_sources']['candidate']['header_sha256'],'Micro candidate header binding differs')
  for variant in ('original','candidate'):
   program=work/(variant+'.exe');argv=[compiler,'/nologo','/std:c++latest','/permissive-','/fp:precise','/W4','/WX','/Od','/Ob0','/MTd','/RTC1','/Z7','/EHsc','/GS','/guard:cf','/Zc:inline','/Gy','/DDE_ALLOCATION_C_OBSERVATION=1','/DDE_ALLOCATION_SANITIZER_OBSERVATION=0','/I'+str(root/'tests/support'),'/Fe'+str(program),'/Fo'+str(work/(variant+'.obj')),str(PACKET/(variant+'.cpp')),'/link','/OPT:REF']
   run('compile-'+variant,argv,60);result['lookup_profile'][variant]={'program':identity(program)}
  profile_deadline=min(deadline,time.monotonic()+20)
  for variant in ('original','candidate'):
   body=run('profile-'+variant,[str(work/(variant+'.exe'))],profile_deadline-time.monotonic(),65536);rows=[json.loads(line) for line in body.splitlines()];need(len(rows)==14 and rows[0]['iterator_debug_level']==2 and rows[-1]['complete'] is True and rows[-1]['valid']==1,'Actual microprofile incomplete or Debug iterator policy differs');need(all(r.get('valid',1)==1 for r in rows),'Microprofile semantic refusal');result['lookup_profile'][variant]['rows']=rows
  need(result['lookup_profile']['original']['rows'][-1]['checksum']==result['lookup_profile']['candidate']['rows'][-1]['checksum'],'Original/candidate checksum differs');result['lookup_profile']['scope']='Fixed order,64 operations each12cases, actualMSVC/Od/MTd/checkediterators2. Diagnostic only, no whole-test/R17 speed claim.'
  need(set(overlay)=={'tests/support/allocation_observer.hpp','tests/support/allocation_observer.cpp','tests/support/fft_resources.cpp','tests/support/allocation_calibration.hpp','tests/support/restoration_resource_observation.hpp'},'Closed timing-only overlay ownership differs')
  changed={}
  for name,item in overlay.items():
   target=root/name;need(identity(target)['sha256']==item['before_sha256'],'Overlay base differs');data=(PACKET/'overlay'/name).read_bytes();need(hashlib.sha256(data).hexdigest()==item['diagnostic_sha256'],'Overlay payload differs');target.write_bytes(data);changed[name]=item['diagnostic_sha256']
  expected=dict(parent);expected['current_files']={**parent['current_files'],**changed};before_fs=source_check(root,expected);(out/'instrumented-source-before.json').write_text(json.dumps(before_fs,sort_keys=True)+'\n');result['explicit_overlay']=overlay;result['source_scope']='Base196 plus ONLY declared timing/logging overlay, ORIGINALlinear ledger and numerical code retained; NOT196qualification.'
  need(execution.ROOT==root and not execution.OUT.exists(),'Actual Debug helper owner differs');(execution.OUT/'debug').mkdir(parents=True);result['debug']={};original_capture=execution.bounded_capture;execution.bounded_capture=capture_adapter(original_capture,monitor)
  try:execution.debug_producer(deadline,result['debug'])
  finally:execution.bounded_capture=original_capture
  need(result['debug']['producer']['compiler']['sha256']==result['compiler']['sha256'],'Actual resource compiler differs from microcompiler')
  run('audit',[sys.executable,'tools/audit_build.py','--build','out/dev'],60)
  build=root/'out/dev/app';all_tests=execution.resource_discovery(build,RESOURCE_PATTERN);names=resource_cases(all_tests,records.executable_paths(result['debug']['producer']));need(len(names)==18,'Registered resource set differs')
  chosen=execution.resource_discovery(build,r'^(fft-native-resources|restoration-native-resources-0)$');need({t['name']for t in chosen}==set(result['cases_to_observe']),'Observed subset differs');result['actual_selected_cases']=chosen
  with owned_native_history(build,deadline) as history:
   directory=execution.OUT/'debug';history.directory=directory;history.admit();before=snapshot_ctest_output(build);command=['ctest','--test-dir',str(build),'-R',r'^(fft-native-resources|restoration-native-resources-0)$','--no-tests=error','--stop-on-failure','--parallel','2','--timeout','90','--output-junit',str(directory/'ctest.xml')];result['runtime_argv']=command
   try:result['runtime_exit_code']=capture_adapter(bounded_capture,monitor)(command,dict(os.environ),execution.OUT/'debug-tests.log',min(180,deadline-time.monotonic()-30),CapturePolicy(2*1024**2,history.check,1))
   finally:result['runtime_child_error']=repr(sys.exception()) if sys.exception() else None;result['archived']=archive_ctest_output(build,directory,before,deadline,2)
  records.stable_producer(result['debug']);after_fs=source_check(root,expected);need(before_fs==after_fs,'Instrumented source FS changed during observation');(out/'instrumented-source-after.json').write_text(json.dumps(after_fs,sort_keys=True)+'\n');index_modes(run('index-after',[git,'ls-files','--stage','-z'],10),parent);status=run('status-after',[git,'status','--porcelain','--untracked-files=all'],10);need({line[3:]for line in status.splitlines()}==set(overlay),'Unexpected source changes');result['actual_dirty_source_status']=status;result['observation_complete']=True;result['native_validation_passed']=False;monitor()
 except BaseException as error:result['error']=repr(error);result['notes']=getattr(error,'__notes__',[]);raise
 finally:
  result['elapsed_seconds']=time.monotonic()-start;write_results(out,result,deadline,sys.exception())
 if result.get('runtime_exit_code')!=0:raise SystemExit(1)
if __name__=='__main__':main()
