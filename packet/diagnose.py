"""One finite real-MSVC constructor/exception control, no product or dependency build."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,sys,time
from support import identity,need,source_check,scan,write_results
PACKET=Path(__file__).resolve().parent
CASES=['error-default','vector-default','vector-move','vector-count0','vector-copy','cfftp-length1','rfftp-length1','arr-info-moves']

def selected_source(compiler):
 vc=Path(os.environ.get('VCToolsInstallDir','')).resolve();need(vc.is_dir() and compiler.resolve().is_relative_to(vc),'Selected cl outside VC tools owner');result={'compiler':identity(compiler),'VC_tools_owner':str(vc),'files':{}}
 for name in ['vector','xstring','xmemory']:
  path=vc/'include'/name;before=identity(path,2*1024**2);data=path.read_bytes();need(len(data)<=2*1024**2 and identity(path,2*1024**2)==before,'Selected STL source changed');lines=data.decode('utf-8-sig').splitlines();indices=set()
  for index,line in enumerate(lines):
   if any(token in line for token in ['vector() noexcept','vector(vector&&','basic_string() noexcept','basic_string(basic_string&&','_Alloc_proxy','_Container_proxy_ptr','explicit vector(','vector(const vector&']):indices.update(range(max(0,index-2),min(len(lines),index+8)))
  text='\n'.join(str(index+1)+': '+lines[index] for index in sorted(indices));need(len(text.encode())<=65536,'Selected constructor source snippets exceed64KiB');result['files'][name]={'identity':before,'constructor_proxy_lines':text}
 return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.source.resolve();out=a.output.resolve();need(not out.exists(),'Fresh finite probe owner required');out.mkdir();os.chdir(out);sys.path.insert(0,str(root/'tools'))
 from process_execution import CapturePolicy,bounded_capture
 start=time.monotonic();deadline=start+90;result={'kind':'FINITE_MSVC_CONSTRUCTOR_EXCEPTION_DIAGNOSTIC','product_builds':0,'SUT_invocations':0,'commands':[],'complete':False,'child_work_clock_seconds':20,'case_seconds':2,'no_retry':True,'qualification':False}
 def monitor():
  need(time.monotonic()<deadline,'Finite controller90s clock exhausted');scan(out,128*1024**2,128)
 try:
  parent=json.loads((PACKET/'source-manifest.json').read_bytes());need(parent['head']=='a12a0fdd5ba5a508c8d604685546d34f6e0f876d','Actual SUT pin differs');before=source_check(root,parent);git=shutil.which('git');need(git is not None,'Git absent')
  compiler=shutil.which('cl.exe');need(compiler is not None,'Selected MSVC absent');selected=selected_source(Path(compiler));(out/'selected-stl-source.json').write_text(json.dumps(selected,indent=2)+'\n');result['selected_STL']=identity(out/'selected-stl-source.json',512*1024)
  receipt=json.loads((PACKET/'pocketfft-receipt.json').read_bytes());lock=json.loads((root/'deps/lock.json').read_bytes());need(receipt['dependency'] in lock['dependencies'] and receipt['resolved_commit']=='c90e55b3d529f8efa40ed01a20de22405f45fc65','Bundled header source not current immutable lock');need(identity(PACKET/'vendor/pocketfft_hdronly.h')['sha256']==receipt['files']['pocketfft_hdronly.h'] and identity(PACKET/'vendor/LICENSE.md')['sha256']==receipt['files']['LICENSE.md'],'Bundled upstream header/license differs')
  work_deadline=time.monotonic()+20
  def run(name,argv,cap,allowed):
   monitor();remaining=work_deadline-time.monotonic();need(remaining>0,'Fixed20s compiler/child aggregate exhausted');row={'name':name,'argv':argv,'cap':min(cap,remaining)};result['commands'].append(row);path=out/(name+'.log');entered=time.monotonic()
   try:row['exit_code']=bounded_capture(argv,dict(os.environ),path,row['cap'],CapturePolicy(65536,monitor,.1))
   finally:
    row['elapsed_seconds']=time.monotonic()-entered;row['error']=repr(sys.exception()) if sys.exception() else None
    if path.exists():row['log']=identity(path,65536)
   need(row['exit_code'] in allowed and b'[engine output omitted:' not in path.read_bytes(),'Actual finite compile/constructor child failed or truncated: '+name);return path
  program=out/'probe.exe';run('compile',[compiler,'/nologo','/std:c++latest','/EHsc','/MTd','/Od','/RTC1','/Zi','/D_ITERATOR_DEBUG_LEVEL=2','/W4','/WX','/external:I'+str(PACKET/'vendor'),'/external:W0','/I'+str(root/'include'),str(PACKET/'probe.cpp'),'/Fe'+str(program),'/Fo'+str(out/'probe.obj'),'/Fd'+str(out/'probe.pdb')],20,{0});result['program']=identity(program)
  def rows(path):return [json.loads(line)for line in path.read_text().splitlines()if line.startswith('{')]
  healthy=rows(run('healthy',[str(program)],2,{0}));need(len(healthy)==9 and healthy[0]['iterator_debug_level']==2 and healthy[0]['DEBUG']==1,'Actual MSVC Debug context or healthy row count differs');result['healthy']=healthy;need([r['case']for r in healthy[1:]]==CASES and all(r['outcome']=='returned' and r['attempts']>0 for r in healthy[1:]),'Each healthy constructor must perform actual observed allocations')
  result['failure_controls']=[]
  for name in CASES:
   path=run('fail1-'+name,[str(program),name],2,{0,86});observations=rows(path);need(len(observations)==2 and observations[0]==healthy[0],'Failure child context differs');row=observations[1];need(row['case']==name and row['failure']==1 and row['attempts']==1 and row['outcome'] in {'terminate','caught_bad_alloc'},'One-shot constructor refusal not observed');result['failure_controls'].append(row)
  result['child_work_elapsed_seconds']=20-(work_deadline-time.monotonic());need(result['child_work_elapsed_seconds']<=20,'Original20s child work exceeded');need(source_check(root,parent)==before,'Frozen source changed');result['complete']=True;monitor()
 except BaseException as error:result['error']=repr(error);result['notes']=getattr(error,'__notes__',[]);raise
 finally:result['elapsed_seconds']=time.monotonic()-start;write_results(out,result,deadline,sys.exception())
if __name__=='__main__':main()
