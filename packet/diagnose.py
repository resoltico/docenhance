"""Only original196 two-case phase/timing observation; closed microprofile is historical."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,sys,time
from support import identity,need,scan,source_check,index_modes,capture_adapter,write_results
PACKET=Path(__file__).resolve().parent

def clock_source_audit(compiler):
 vc=Path(os.environ.get('VCToolsInstallDir','')).resolve();need(vc.is_dir() and compiler.resolve().is_relative_to(vc),'Selected compiler outside observed VC tools owner')
 paths={'chrono':vc/'include/__msvc_chrono.hpp','declarations':vc/'include/xtimec.h','provider':vc/'crt/src/stl/xtime.cpp'};audit={'compiler':identity(compiler),'VC_tools_owner':str(vc),'files':{},'snippets':{},'allocation_free_assumption':'Actual selected MSVC steady_clock provider uses native performance-counter queries with no allocation expressions in the retained function bodies; diagnostic observation, not general library proof.'}
 text={}
 for role,path in paths.items():
  before=identity(path,2*1024**2);data=path.read_bytes();need(len(data)<=2*1024**2 and identity(path,2*1024**2)==before,'MSVC clock source changed during audit');text[role]=data.decode('utf-8-sig');audit['files'][role]=before
 def snippet(source,name):
  position=source.find(name);need(position>=0,'Actual selected MSVC clock signature missing: '+name);opening=source.find('{',position);need(opening>=0,'Clock source body missing');depth=0
  for end in range(opening,min(len(source),opening+16384)):
   depth+=(source[end]=='{')-(source[end]=='}')
   if depth==0:return source[position:end+1]
  raise ValueError('Clock source body exceeds16KiB')
 audit['snippets']['steady_clock']=snippet(text['chrono'],'struct steady_clock')
 for name in ['_Query_perf_counter','_Query_perf_frequency']:
  need(name in text['declarations'] and name in audit['snippets']['steady_clock'],'Steady clock/provider declarations not coupled');body=snippet(text['provider'],name);need(not any(token in body for token in ['operator new','malloc(', 'calloc(', 'realloc(']),'Clock provider allocation expression refused');audit['snippets'][name]=body
 need('QueryPerformanceCounter' in audit['snippets']['_Query_perf_counter'] and 'QueryPerformanceFrequency' in audit['snippets']['_Query_perf_frequency'],'MSVC clock provider is not actual native performance counter');return audit

def crt_source_observations():
 vc=Path(os.environ.get('VCToolsInstallDir','')).resolve();sdk=os.environ.get('UniversalCRTSdkDir');version=os.environ.get('UCRTVersion');paths={'scalar_delete':vc/'crt/src/vcruntime/delete_scalar.cpp','array_delete':vc/'crt/src/vcruntime/delete_array.cpp'}
 if sdk and version:paths['debug_heap']=Path(sdk)/'Source'/version/'ucrt/heap/debug_heap.cpp'
 need(sdk and version,'Selected UCRT SDK provenance absent for fatalcallback audit');api_root=Path(sdk)/'Include'/version/'ucrt';paths.update(report_api=api_root/'crtdbg.h',descriptor_api=api_root/'corecrt_io.h')
 observations={'scope':'Optional actual selected CRT source observation; missing sources remain unknown, no fallback or provider behavior fix.','VC_tools_owner':str(vc),'SDK_owner':sdk,'SDK_version':version,'files':{}}
 for role,path in paths.items():
  row={'path':str(path),'available':path.exists()};observations['files'][role]=row
  if not path.exists():
   need(role not in {'report_api','descriptor_api'},'Selected fatalcallback API header absent');continue
  before=identity(path,2*1024**2);data=path.read_bytes();need(len(data)<=2*1024**2 and identity(path,2*1024**2)==before,'Actual CRT source changed during bounded read');lines=data.decode('utf-8-sig').splitlines();selected=set()
  for index,line in enumerate(lines):
   if any(token in line for token in ['operator delete','_free_dbg','_UNKNOWN_BLOCK','_NORMAL_BLOCK','block_use ==','expected_block_use','_CRT_REPORT_HOOK','_CrtSetReportHook','_CRT_ASSERT','_CRT_ERROR','_write(']):selected.update(range(max(0,index-2),min(len(lines),index+4)))
  text='\n'.join(str(index+1)+': '+lines[index] for index in sorted(selected));need(len(text.encode())<=32768,'CRT source evidence snippet exceeds32KiB');row.update(identity=before,selected_lines=text)
  if role=='report_api':need('_CRT_REPORT_HOOK' in text and '_CrtSetReportHook' in text and '_CRT_ASSERT' in text and '_CRT_ERROR' in text,'Selected assertion callback signature absent')
  if role=='descriptor_api':need('_write(' in text,'Selected raw descriptor API signature absent')
 return observations

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.source.resolve();out=a.output.resolve();need(not out.exists() and not(root/'out/dev').exists(),'Fresh owners required');out.mkdir();os.chdir(root)
 sys.path.insert(0,str(root/'tools'))
 from process_execution import CapturePolicy,bounded_capture
 import windows_resource_execution as execution
 import windows_resource_records as records
 from native_history import owned_native_history
 from native_journal import snapshot_ctest_output
 from native_output import archive_ctest_output
 from restoration_resource_cases import RESOURCE_PATTERN,resource_cases
 parent=json.loads((PACKET/'source-manifest.json').read_bytes());overlay=json.loads((PACKET/'overlay.json').read_bytes());start=time.monotonic();deadline=start+2820
 result={'kind':'ORIGINAL196_FATAL_CAUSE_DIAGNOSTIC_NOT_QUALIFICATION','base_head':parent['head'],'prior_microprofile':json.loads((PACKET/'prior-microprofile.json').read_bytes()),'commands':[],'cases_to_observe':['fft-native-resources','restoration-native-resources-0'],'unstarted_other_resource_cases':16,'native_validation_passed':False,'observation_complete':False,'caps':{'microbenchmark_invocations':0,'Debug_owner_seconds':2820,'per_case_seconds':90,'parallel':2,'native_history_MiB':384,'run_reserve_MiB':136,'log_bytes':2*1024**2,'build_sampled_stop_MiB':2048}}
 def monitor():
  need(time.monotonic()<deadline,'Original2820s work clock exhausted');scan(root/'out/dev',2*1024**3,100000);scan(out,32*1024**2,256)
 def run(name,argv,seconds,limit=2*1024**2):
  need(seconds>0,'Original command clock exhausted before launch');monitor();row={'name':name,'argv':argv,'cap':min(seconds,deadline-time.monotonic())};result['commands'].append(row);log=out/(name+'.log');entered=time.monotonic()
  try:row['exit_code']=bounded_capture(argv,dict(os.environ),log,row['cap'],CapturePolicy(limit,monitor,1))
  finally:
   row['elapsed_seconds']=time.monotonic()-entered;row['error']=repr(sys.exception()) if sys.exception() else None
   if log.exists():row['log']=identity(log,limit)
  need(row['exit_code']==0 and b'[engine output omitted:'not in log.read_bytes(),'Actual diagnostic command failed or truncated: '+name);return log.read_text(encoding='utf-8').rstrip('\r\n')
 try:
  need(identity(PACKET/'source-manifest.json')['sha256']=='cece18a1a40cc70f8e5421b62172734b24ff5394227f0ecace404bda057fcf74' and parent['head']=='a12a0fdd5ba5a508c8d604685546d34f6e0f876d','Base source pin differs');source_check(root,parent)
  git=shutil.which('git');need(git is not None,'Git absent');need(run('head-before',[git,'rev-parse','HEAD'],10)==parent['head'],'Checkout head differs');need(not run('status-before',[git,'status','--porcelain','--untracked-files=all'],10),'Checkout dirty');index_modes(run('index-before',[git,'ls-files','--stage','-z'],10),parent)
  run('runtime',[sys.executable,'tools/check_runtime_platform.py','--platform','windows-x86_64'],10)
  compiler=shutil.which('cl.exe');need(compiler is not None,'Selected cl absent');result['compiler']=identity(Path(compiler));result['MSVC_include_root']=os.environ.get('VCToolsInstallDir');result['clock_source_audit']=clock_source_audit(Path(compiler));(out/'clock-source-audit.json').write_text(json.dumps(result['clock_source_audit'],indent=2)+'\n');result['CRT_source_observations']=crt_source_observations();(out/'crt-source-observations.json').write_text(json.dumps(result['CRT_source_observations'],indent=2)+'\n')
  need(set(overlay)=={'tests/support/allocation_observer.hpp','tests/support/allocation_observer.cpp','tests/support/fft_resources.cpp','tests/support/allocation_calibration.hpp','tests/support/restoration_resource_observation.hpp'},'Closed timing-only overlay ownership differs')
  changed={}
  for name,item in overlay.items():
   target=root/name;need(identity(target)['sha256']==item['before_sha256'],'Overlay base differs');data=(PACKET/'overlay'/name).read_bytes();need(hashlib.sha256(data).hexdigest()==item['diagnostic_sha256'],'Overlay payload differs');target.write_bytes(data);changed[name]=item['diagnostic_sha256']
  expected=dict(parent);expected['current_files']={**parent['current_files'],**changed};before_fs=source_check(root,expected);(out/'instrumented-source-before.json').write_text(json.dumps(before_fs,sort_keys=True)+'\n');result['explicit_overlay']=overlay;result['source_scope']='Base196 plus ONLY declared timing/logging overlay, ORIGINALlinear ledger and numerical code retained; NOT196qualification.'
  need(execution.ROOT==root and not execution.OUT.exists(),'Actual Debug helper owner differs');(execution.OUT/'debug').mkdir(parents=True);result['debug']={};original_capture=execution.bounded_capture;execution.bounded_capture=capture_adapter(original_capture,monitor)
  try:execution.debug_producer(deadline,result['debug'])
  finally:execution.bounded_capture=original_capture
  need(result['debug']['producer']['compiler']['sha256']==result['compiler']['sha256'],'Actual resource compiler differs from selected MSVC')
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
