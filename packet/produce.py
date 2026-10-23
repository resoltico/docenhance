"""One fresh exact-source pair of Intel Mac CLI producers, never a measurement runner."""
from pathlib import Path
import hashlib
import ast
import importlib.util
import json
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import time

OLD = 'e72a00f58303c01f31eea18c64745c93efbdabec'
CURRENT = 'f577bf67cad6c433ea2a53cfaa4bc3294a2f8cb5'
MIB = 1024 * 1024
TOTAL_SECONDS = 6000
CLEANUP_SECONDS = 150
HERE = Path(__file__).resolve().parent


def need(value, message):
    if not value:
        raise RuntimeError(message)


def receipt(path):
    path = Path(path)
    before = path.stat()
    need(stat.S_ISREG(before.st_mode),'Receipt file must be regular')
    with path.open('rb') as stream:
        value = hashlib.file_digest(stream, 'sha256').hexdigest()
    after = path.stat()
    need((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
         (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns),
         'Receipt epoch changed: ' + str(path))
    return {'path': str(path), 'bytes': after.st_size, 'sha256': value}


def tree_bytes(root, deadline, observations=None):
    """Charge max(logical, allocated) per entry; finite sampling is never a disk quota."""
    total = logical = allocated = entries = 0
    pending = [root] if root.exists() else []
    def charge(info):
        blocks=getattr(info,'st_blocks',None)
        need(isinstance(blocks,int) and blocks>=0,'Allocated blocks unavailable on required Mac host')
        return info.st_size,blocks*512,max(info.st_size,blocks*512)
    while pending:
        need(time.monotonic() < deadline, 'Original producer inventory clock exhausted')
        directory = pending.pop()
        try:
            info=directory.lstat()
            need(stat.S_ISDIR(info.st_mode),'Redirected/non-directory producer tree: '+str(directory))
            scan=os.scandir(directory)
        except FileNotFoundError:
            continue
        one,two,three=charge(info);logical+=one;allocated+=two;total+=three
        with scan as children:
            for child in children:
                need(time.monotonic()<deadline,'Original inventory clock exhausted')
                entries+=1;need(entries<=200000,'Producer component exceeds200000 entries')
                try:info=child.stat(follow_symlinks=False)
                except FileNotFoundError:continue
                if stat.S_ISDIR(info.st_mode):pending.append(Path(child.path))
                else:
                    need(stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode),'Special producer entry')
                    one,two,three=charge(info);logical+=one;allocated+=two;total+=three
    if observations is not None:
        observations.update(logical_bytes=logical,allocated_block_bytes=allocated,
                            max_per_entry_charged_bytes=total,entries=entries)
    return total


def source_stable(root, source, deadline, git, capture=None):
    actual=set();entries=0
    for folder,directories,files in os.walk(root,followlinks=False):
        need(time.monotonic()<deadline,'Original source membership deadline exhausted')
        directory=Path(folder)
        if directory==root:
            directories[:]=[name for name in directories if name not in {'.git','.cache'}]
            cache=root/'.cache'
            if cache.exists():
                need(not cache.is_symlink() and {p.name for p in cache.iterdir()}<={'linux-gate'},
                     'Unexpected ignored source cache owner')
                gate=cache/'linux-gate'
                if gate.exists():
                    need(not gate.is_symlink() and {p.name for p in gate.iterdir()}<={'active'},
                         'Unexpected ignored source gate payload')
        for name in directories:
            need(not(directory/name).is_symlink(),'Redirected source directory')
        for name in files:
            entries+=1;need(entries<=10000,'Source membership exceeds10000 files')
            actual.add((directory/name).relative_to(root).as_posix())
    need(actual==set(source['current_files']),'Exact source file membership changed')
    for name, digest in source['current_files'].items():
        need(time.monotonic() < deadline, 'Original source guard clock exhausted')
        path = root / name
        need(not path.is_symlink() and receipt(path)['sha256'] == digest and
             oct(path.stat().st_mode & 0o777) == source['current_modes'][name],
             'Exact source bytes/modes changed: ' + str(path))
    if capture is not None:
        for argv, expected in [
            (['rev-parse', 'HEAD'], source['head']), (['status', '--porcelain=v1'], '')
        ]:
            capture([str(git), '-C', str(root), *argv],5,
                    lambda text:need(text.strip()==expected,'Source HEAD/clean ownership changed: '+str(root)))



class Producer:
    """One finite capsule clock owns both fresh recipes, diagnostics and all child groups."""
    def __init__(self, workspace):
        self.workspace = workspace
        self.base = workspace / 'performance-work'
        need(not self.base.exists() and not self.base.is_symlink(), 'Fresh producer owner already exists')
        self.base.mkdir()
        self.start = time.monotonic()
        self.deadline = self.start + TOTAL_SECONDS
        self.env = dict(os.environ, DE_BUILD_JOBS='2', PYTHONDONTWRITEBYTECODE='1', CC='/usr/bin/clang', CXX='/usr/bin/clang++')
        for key in ('TMPDIR', 'TMP', 'TEMP'):
            self.env[key] = str(self.base / 'tmp')
        self.env.pop('PYTHONPYCACHEPREFIX', None)
        for key in ('CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS'):
            need(not self.env.get(key),'Disallowed inherited native flag injection: '+key)
            self.env.pop(key,None)
        (self.base / 'tmp').mkdir()
        (self.base / 'evidence').mkdir()
        self.sources = {}
        self.rows = []
        self.metadata_rows = []
        self.results = {'complete': False, 'rows': self.rows, 'product_builds': 0,
                        'SUT_invocations': 0, 'measurement_eligibility': False,
                        'full_R17_acceptance': False, 'sampled_resources': {}, 'metadata_observations':self.metadata_rows}
        self.last_source_check = 0
        self.components = {
            self.base / 'cache': 768*MIB, self.base / 'old': 512*MIB,
            self.base / 'current': 256*MIB, self.base / 'tmp': 512*MIB,
            self.base / 'evidence': 192*MIB,
        }
        self.frozen = {}
        self.tool_context = None
        self.git=None
        self.git_spelling=None
        self.git_path_environment=self.env.get('PATH','')
        self.cm = shutil.which('cmake')
        self.ninja = shutil.which('ninja')
        need(self.cm and self.ninja, 'Pinned build tools unavailable')

    def select_git(self):
        need(self.git is None,'Selected Git role already consumed')
        need(self.git_path_environment and all(part and Path(part).is_absolute()
             for part in self.git_path_environment.split(os.pathsep)), 'Git PATH must use absolute entries')
        spelling=shutil.which('git',path=self.git_path_environment)
        need(spelling is not None and Path(spelling).is_absolute(),'Actual checkout PATH Git unavailable')
        program=Path(spelling).resolve(strict=True)
        info=program.lstat()
        need(stat.S_ISREG(info.st_mode) and info.st_size<=64*MIB,'Selected Git is not a bounded regular program')
        self.git,self.git_spelling=program,spelling
        item=receipt(program);self.frozen[program]=item
        self.results.update(git=item,git_observed_spelling=spelling,git_path_environment=self.git_path_environment)
        def version(text):
            need(bool(text.strip()),'Actual selected Git version unavailable')
            return text
        self.results['git_version']=self.metadata([str(program),'--version'],5,version)

    def check_git(self):
        if self.git is None:return
        need(self.env.get('PATH')==self.git_path_environment and
             os.environ.get('PATH','')==self.git_path_environment,'Selected Git PATH changed')
        spelling=shutil.which('git',path=self.git_path_environment)
        need(spelling==self.git_spelling and Path(spelling).resolve(strict=True)==self.git,
             'Selected Git program/spelling changed')

    def remaining(self):
        return self.deadline - time.monotonic() - CLEANUP_SECONDS

    def monitor(self, final=False):
        self.check_git()
        need(self.remaining() > 0, 'Original6000s capsule work clock exhausted')
        values = {};details={}
        for root, cap in self.components.items():
            detail={};values[str(root)]=tree_bytes(root,self.deadline-CLEANUP_SECONDS,detail);details[str(root)]=detail
            need(values[str(root)] <= cap, 'Sampled producer component overrun: ' + str(root))
        source_bytes=0
        for root in self.sources:
            detail={};source_bytes+=tree_bytes(root,self.deadline-CLEANUP_SECONDS,detail);details[str(root)]=detail
        need(source_bytes <= 256*MIB, 'Sampled source checkout/git extents exceed256MiB')
        extra = 0
        for path in self.base.iterdir():
            if path not in self.components:
                need(path.name in {'progress.json','results.json'} and path.is_file(),
                     'Unexpected producer root extent: ' + str(path))
                info=path.lstat();need(isinstance(getattr(info,'st_blocks',None),int),'Root allocated blocks unavailable')
                extra += max(info.st_size,info.st_blocks*512)
        owner=self.base.lstat();extra+=max(owner.st_size,owner.st_blocks*512)
        occupied = sum(values.values()) + source_bytes + extra
        need(occupied <= 3072*MIB, 'Sampled aggregate producer extents exceed3GiB')
        free = shutil.disk_usage(self.workspace).free
        need(free >= 5*1024**3, 'Actual host free disk below5GiB')
        rss = 0
        if not final:
            def census(text):
                processes={}
                for line in text.splitlines():
                    fields=line.split()
                    need(len(fields)==3 and all(re.fullmatch(r'[0-9]+',field) for field in fields),
                         'Malformed/negative process RSS census')
                    pid,parent,rss=map(int,fields)
                    need(pid not in processes,'Duplicate process RSS identity')
                    processes[pid]=(parent,rss)
                need(os.getpid() in processes,'Own producer absent from RSS census')
                owned = {os.getpid()}
                while True:
                    added = {pid for pid,(parent,_) in processes.items() if parent in owned}
                    if added <= owned:
                        break
                    owned |= added
                return sum(processes[p][1] for p in owned if p in processes)*1024
            rss = self.metadata(['/bin/ps','-axo','pid=,ppid=,rss='],3,census)
        need(rss <= 4*1024**3, 'Sampled owned process tree RSS exceeds4GiB')
        resources = self.results['sampled_resources']
        resources['latest_component_extents']=details
        resources.update(peak_owned_bytes=max(occupied, resources.get('peak_owned_bytes',0)),
                         peak_process_tree_RSS_bytes=max(rss, resources.get('peak_process_tree_RSS_bytes',0)),
                         minimum_free_bytes=min(free, resources.get('minimum_free_bytes',free)))
        if not final and time.monotonic()-self.last_source_check >= 15:
            for root, source in self.sources.items():
                source_stable(root, source, self.deadline-CLEANUP_SECONDS,self.git,self.metadata)
            for path, expected in self.frozen.items():
                need(receipt(path) == expected, 'Bound tool/harness drift: ' + str(path))
            self.last_source_check = time.monotonic()

    def progress(self, original=None):
        try:
            data=(json.dumps(self.results,indent=2)+'\n').encode()
            need(len(data)<=8*MIB,'Producer progress exceeds8MiB')
            (self.base/'progress.json').write_bytes(data)
        except BaseException as error:
            if original is None:raise
            original.add_note('Producer progress retention also failed: '+repr(error))

    def metadata(self, argv, seconds, parse):
        """Bound discarded-success metadata with the existing joined capture owner."""
        from process_execution import bounded_capture, CapturePolicy
        self.check_git()
        need(not self.metadata_rows or self.metadata_rows[-1].get('accepted'),
             'Prior adverse metadata raw must remain untouched')
        need(len(self.metadata_rows)<10000,'Finite metadata observation count exceeded')
        log=self.base/'tmp/metadata-current.log'
        if log.exists():
            need(not log.is_symlink(),'Transient metadata owner redirected')
            before=receipt(log);last=self.metadata_rows[-1]
            need(before['bytes']==last['observed_bytes'] and before['sha256']==last['observed_sha256'],
                 'Accepted transient metadata changed before retirement')
            log.unlink()
        row={'argv':argv,'reaped':False,'readers_closed':False,'accepted':False}
        self.metadata_rows.append(row)
        check=lambda:need(self.remaining()>0,'Original metadata work clock exhausted')
        pending=None;value=None
        try:
            code=bounded_capture(argv,self.env,log,min(seconds,self.remaining()),
                                 CapturePolicy(2*MIB-1024,check,interval=3))
            row.update(reaped=True,readers_closed=True,exit_code=code)
            need(code==0,'Read-only metadata command failed')
            text=log.read_bytes().decode('utf-8','strict');need('[engine output omitted:' not in text,'Read-only metadata overflow')
            value=parse(text)
            row['accepted']=True
        except BaseException as error:
            pending=error
        finally:
            try:
                if log.exists():
                    item=receipt(log);row['observed_bytes']=item['bytes'];row['observed_sha256']=item['sha256']
                    self.results['metadata_raw_retention']='Only most recent metadata raw retained; previous accepted transient samples discarded as before.'
            except BaseException as error:
                if pending is None:pending=error
                else:pending.add_note('Metadata receipt also failed: '+repr(error))
        if pending is not None:raise pending
        return value

    def run(self, directory, name, argv, seconds):
        from process_execution import bounded_capture, CapturePolicy
        self.check_git()
        need(self.remaining() > 5 and seconds>0, 'No original work/cleanup interval remains')
        row = {'name': name, 'argv': argv, 'complete': False, 'reaped': False,
               'readers_closed': False, 'started_unix': time.time()}
        self.rows.append(row)
        log = directory / (name+'.log')
        need(not log.exists(), 'Refuse replacing prior diagnostic')
        pending=None;text=None
        try:
            row['exit_code'] = bounded_capture(argv, self.env, log, min(seconds,self.remaining()-5),
                                              CapturePolicy(2*MIB-1024, self.monitor, interval=3))
            row['reaped'] = row['readers_closed'] = True
            need(row['exit_code'] == 0, 'Actual producer child failed: ' + name)
            text = log.read_text()
            need('[engine output omitted:' not in text, 'Actual producer diagnostic truncated')
            row['complete'] = True
        except BaseException as error:
            pending=error
        finally:
            try:
                if log.exists():row['log']=receipt(log)
                row['ended_unix']=time.time()
                self.progress()
            except BaseException as error:
                if pending is None:pending=error
                else:pending.add_note('Producer row retention also failed: '+repr(error))
        if pending is not None:raise pending
        return text

    def context(self, directory, prefix):
        commands = [
            ('clang-version',['/usr/bin/clang','--version']),
            ('clangxx-version',['/usr/bin/clang++','--version']),
            ('sdk-path',['/usr/bin/xcrun','--show-sdk-path']),
            ('sdk-version',['/usr/bin/xcrun','--show-sdk-version']),
            ('os-version',['/usr/bin/sw_vers']),
            ('machine',['/usr/sbin/sysctl','-n','hw.memsize','hw.ncpu','machdep.cpu.brand_string']),
            ('linker-path',['/usr/bin/xcrun','--find','ld']),
        ]
        observed = {}
        for name, command in commands:
            observed[name] = self.run(directory,prefix+name,command,10)
        linker = Path(observed['linker-path'].strip())
        observed['linker-version'] = self.run(directory,prefix+'linker-version',[str(linker),'-version_details'],10)
        need('Apple clang version ' in observed['clangxx-version'], 'Selected compiler is not AppleClang')
        sdk = Path(observed['sdk-path'].strip())
        need(sdk.is_dir() and sdk.is_relative_to(Path(self.env['DEVELOPER_DIR'])), 'Selected SDK escapes Xcode')
        for path in [Path('/usr/bin/clang'), Path('/usr/bin/clang++'), linker, sdk/'SDKSettings.json']:
            current = receipt(path)
            need(path not in self.frozen or self.frozen[path] == current, 'Toolchain bytes changed')
            self.frozen[path] = current
        if self.tool_context is None:
            self.tool_context = observed
        else:
            need(observed == self.tool_context, 'Fresh old/current before/after tool contexts differ')
        return observed

    def qualify(self, observed, context):
        target = HERE/'qualify_products.py'
        need(receipt(target) == self.frozen[target], 'Bound qualifier drift before import')
        spec = importlib.util.spec_from_file_location('_paired_qualifier', target)
        module = importlib.util.module_from_spec(spec)
        data = target.read_bytes()
        need(hashlib.sha256(data).hexdigest() == self.frozen[target]['sha256'],
             'Bound qualifier source buffer changed')
        exec(compile(data,str(target),'exec',dont_inherit=True),module.__dict__)
        need(Path(module.__file__).resolve() == target and Path(module.__spec__.origin).resolve() == target and receipt(target) == self.frozen[target],
             'Bound qualifier owner drift')
        return module.qualify(observed, min(self.deadline-CLEANUP_SECONDS,time.monotonic()+60),context)

    def build(self, label):
        root = self.workspace / ('sut-old' if label=='old' else 'sut-current')
        build = self.base/label
        directory = self.base/'evidence'/label
        directory.mkdir()
        source_path = HERE/('source-'+label+'.json')
        observed = {'source':receipt(source_path), 'rows':[], 'resources':self.results['sampled_resources']}
        start_row = len(self.rows)
        before = self.context(directory,'before-')
        sdk = before['sdk-path'].strip()
        args = [self.cm,'--preset','release','-S',str(root),'-B',str(build),
                '-DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang','-DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++',
                '-DCMAKE_MAKE_PROGRAM:FILEPATH='+self.ninja,'-DCMAKE_OSX_SYSROOT:PATH='+sdk,
                '-DCMAKE_OSX_ARCHITECTURES:STRING=x86_64','-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=15.0',
                '-DDE_SOURCE_CACHE:PATH='+str(self.base/'cache'),'-DDE_BUILD_JOBS:STRING=2',
                '-DDE_ENABLE_IPO:BOOL=OFF','-DPython3_EXECUTABLE:FILEPATH='+sys.executable]
        self.run(directory,'configure',args,30)
        stamp = 'ep/application/src/de_native-stamp/de_native-configure'
        need(any(line.startswith('build ') and stamp in line.split(':',1)[0].split()
                 for line in (build/'build.ninja').read_text().splitlines()), 'Actual inner configure stamp absent')
        self.run(directory,'native-closure',[self.cm,'--build',str(build),'--parallel','2','--target',stamp],900)
        self.run(directory,'dependency-audit',[sys.executable,'-B',str(root/'tools/audit_build.py'),'--build',str(build)],15)
        self.run(directory,'cli-only',[self.cm,'--build',str(build/'app'),'--parallel','2','--target','docenhance'],1500)
        self.results['product_builds'] += 1
        self.context(directory,'after-')
        self.run(directory,'actual-cli-commands',[self.ninja,'-C',str(build/'app'),'-t','commands','docenhance'],15)
        self.run(directory,'loader-imports',['/usr/bin/otool','-L',str(build/'bin/docenhance')],10)
        observed['rows'] = self.rows[start_row:]
        match = re.search(r'Apple clang version ([0-9.]+)',before['clangxx-version'])
        context = dict(root=str(root), build=str(build), out=str(directory), source=str(source_path),
                       dependency_count=10 if label=='old' else 9,
                       sdk_encoding=sdk_encoding(before['sdk-version']), clang_semantic_version=match[1],
                       compiler_hashes={'C_compiler_sha256':receipt('/usr/bin/clang')['sha256'],
                                        'CXX_compiler_sha256':receipt('/usr/bin/clang++')['sha256']})
        qualification = self.qualify(observed,context)
        static = {'observations':[{'command':row['argv'],'exit':row['exit_code'],
                                  'stdout':Path(row['log']['path']).read_text()} for row in observed['rows'][:8]]}
        encoded = (json.dumps(static,indent=2)+'\n').encode()
        need(len(encoded)<=2*MIB, 'Static producer context exceeds2MiB')
        with (directory/'static-context.json').open('xb') as stream:
            stream.write(encoded)
        metadata = [
            ('compile_commands.json',build/'app/compile_commands.json'),
            ('architecture-targets.json',build/'app/architecture-targets.json'),
            ('build.ninja',build/'app/build.ninja'),
            ('outer-build-identity.json',build/'build-identity.json'),
            ('inner-build-identity.json',build/'app/build-identity.json'),
            ('outer-cache.txt',build/'CMakeCache.txt'),
            ('inner-cache.txt',build/'app/CMakeCache.txt'),
        ]
        metadata += [('native-'+dep['name']+'-cache.txt',build/'deps'/dep['name']/'CMakeCache.txt')
                     for dep in json.loads((root/'deps/lock.json').read_bytes())['dependencies']]
        copied = []
        total = 0
        for name,source_file in metadata:
            prior = receipt(source_file)
            need(prior['bytes']<=2*MIB, 'Producer metadata file exceeds2MiB')
            data = source_file.read_bytes(); total += len(data)
            need(total<=8*MIB and receipt(source_file)==prior, 'Producer metadata epoch/8MiB changed')
            with (directory/name).open('xb') as stream:
                stream.write(data)
            need(receipt(directory/name)['sha256']==prior['sha256'],'Retained metadata differs')
            copied.append(dict(source=prior,retained=receipt(directory/name)))
        binary = build/'bin/docenhance'; original_binary = receipt(binary)
        need(original_binary['bytes']<=16*MIB,'Actual CLI exceeds16MiB retention allowance')
        shutil.copy2(binary,directory/'docenhance')
        need(receipt(binary)==original_binary and receipt(directory/'docenhance')['sha256']==original_binary['sha256'],
             'Retained CLI copy differs')
        with (directory/'retained-metadata.json').open('x') as stream:
            stream.write(json.dumps(copied,indent=2)+'\n')
        return {'complete':True,'qualification':qualification,'binary':receipt(build/'bin/docenhance'),
                'artifact_binary':receipt(directory/'docenhance'), 'build':str(build),
                'source':receipt(source_path),'checkout':str(root),'selected_linker':before['linker-path'].strip(),
                'retained_metadata':receipt(directory/'retained-metadata.json'),
                'static_context':receipt(directory/'static-context.json'),
                'context_logs':{name:receipt(directory/('before-'+name+'.log'))
                                for name in ['os-version','linker-path','linker-version']}}


def tool_version_matches(name, text, expected):
    if name == 'ninja':
        return text == expected + '.git.kitware.jobserver-pipe-1\n'
    return bool(re.search(r'(?<![0-9.])'+re.escape(expected)+r'(?![0-9.])',text))


def sdk_encoding(text):
    value=text.strip()
    need(re.fullmatch(r'[0-9]+(?:\.[0-9]+){0,2}',value) is not None,'Malformed actual SDK version')
    parts=[int(field) for field in value.split('.')]
    parts += [0]*(3-len(parts))
    need(0<=parts[0]<=65535 and all(0<=field<=255 for field in parts[1:]),
         'Actual SDK components exceed Mach-O16/8/8 bit fields')
    return (parts[0]<<16)|(parts[1]<<8)|parts[2]


def admit_sources(producer):
    details={};total=0
    for name in ['sut-old','sut-current']:
        root=producer.workspace/name;observed={}
        need(root.is_dir() and not root.is_symlink(),'Fresh source root redirected/missing')
        total+=tree_bytes(root,producer.deadline-CLEANUP_SECONDS,observed)
        details[str(root)]=observed
        need(total<=256*MIB,'Actual source checkout/git extents exceed256MiB before content hashing')
    producer.results['source_admission']=details


def screen_prebuild(producer, capability, directory):
    """Reuse the frozen measurement classifier before spending time on acquisition/builds."""
    from check_performance_host import capture, _power_causes
    manifest=json.loads((HERE/'manifest.json').read_bytes())
    path=HERE/'measure.py';expected=manifest['payload_sha256']['measure.py']
    data=path.read_bytes();need(hashlib.sha256(data).hexdigest()==expected,'Frozen measurement classifier differs')
    producer.frozen[path]=receipt(path)
    functions=[node for node in ast.parse(data).body if isinstance(node,ast.FunctionDef) and node.name=='machine_eligible']
    need(len(functions)==1,'Exact measurement classifier is unavailable')
    namespace=dict(Path=Path,re=re,os=os,_power_causes=_power_causes)
    exec(compile(ast.Module(body=functions,type_ignores=[]),str(path),'exec'),namespace)
    state={'observations':{}}
    for name in ['power','power_modes','low_power','thermal']:
        observation=capability['observations'][name];streams={}
        for stream in ['stdout','stderr']:
            target=directory/'host-capability'/name/stream;record=observation['streams'][stream]
            need(record['path']==str(target) and receipt(target)=={key:record[key] for key in ['path','bytes','sha256']},
                 'Fresh capability raw stream owner differs')
            streams[stream]=target.read_text()
        state['observations'][name]=dict(streams,exit=observation['exit_code'])
    argv=['/bin/ps','-wwaxco','pid=,pcpu=,comm=']
    row={'name':'prebuild-processes','argv':argv,'complete':False,'reaped':False,'readers_closed':False}
    producer.rows.append(row)
    try:
        observed=capture(argv,directory/'prebuild-processes',min(4,producer.remaining()),producer.env)
        row.update(exit_code=observed['exit_code'],reaped=True,readers_closed=observed['children_readers_closed'])
        row['log']=receipt(directory/'prebuild-processes/stdout')
        row['stderr']=receipt(directory/'prebuild-processes/stderr')
        state['observations']['processes']={'stdout':observed['streams']['stdout']['text'],
                                          'stderr':observed['streams']['stderr']['text'],'exit':observed['exit_code']}
        causes=namespace['machine_eligible'](state,False)
        record=dict(state=state,causes=causes,requires_idle=False,measurement_eligibility=False)
        encoded=(json.dumps(record,indent=2)+'\n').encode();need(len(encoded)<=2*MIB,'Prebuild classifier evidence exceeds2MiB')
        with (directory/'prebuild-eligibility.json').open('xb') as stream:stream.write(encoded)
        producer.results['prebuild_screen']=receipt(directory/'prebuild-eligibility.json')
        need(not causes,'Actual prebuild machine classifier refused: '+repr(causes))
        row['complete']=True
    finally:
        producer.progress(sys.exception())


def main():
    workspace = Path(os.environ['GITHUB_WORKSPACE']).resolve()
    root = workspace/'sut-current'
    sys.path.insert(0,str(root/'tools'))
    from cache_lock import exclusive_cache, WriterStateUnknownError
    from check_runtime_platform import runtime_errors
    from llvm_cache import validate_source_tools
    need(os.environ.get('DE_NATIVE_DIAGNOSTIC') in [None,'','false'], 'Ordinary stop-first producer environment required')
    need(not runtime_errors('macos-15-x86_64',platform.system(),platform.machine(),platform.mac_ver()[0]),
         'Producer requires actual macOS15 Intel')
    producer = Producer(workspace)
    primary = None
    try:
        with exclusive_cache(root/'.cache/linux-gate/active'):
            try:
                admit_sources(producer)
                producer.select_git()
                for label,head in [('old',OLD),('current',CURRENT)]:
                    checkout = workspace/('sut-old' if label=='old' else 'sut-current')
                    source = json.loads((HERE/('source-'+label+'.json')).read_bytes())
                    need(source['head']==head,'Wrong exact source manifest')
                    need((checkout/'.git').is_dir() and not(checkout/'.git').is_symlink(), 'Fresh checkout requires owned git directory')
                    source_stable(checkout,source,producer.deadline-CLEANUP_SECONDS,producer.git,producer.metadata)
                    producer.sources[checkout] = source
                need(receipt(workspace/'sut-old/deps/tools.json')['sha256']==receipt(root/'deps/tools.json')['sha256'], 'Old/current pinned tool authority differs')
                producer.last_source_check=time.monotonic()
                pins = json.loads((root/'deps/tools.json').read_bytes())
                from select_xcode import developer_directory, confirm_version
                selected = developer_directory(Path('/Applications'), pins['macos']['minimum_runner_xcode'])
                need(producer.env.get('DEVELOPER_DIR') == str(selected), 'Actual declared Xcode is not selected')
                common = producer.base/'evidence'
                confirm_version(producer.run(common,'Xcode',[str(selected/'usr/bin/xcodebuild'),'-version'],10),
                                pins['macos']['minimum_runner_xcode'])
                capability_dir=common/'host-capability'
                producer.run(common,'fresh-host-capability',
                             ['/usr/bin/env','GITHUB_SHA='+CURRENT,sys.executable,'-B',
                              str(root/'tools/check_performance_host.py'),'--output',str(capability_dir)],80)
                capability=json.loads((capability_dir/'result.json').read_bytes())
                need(capability['capability_admitted'] and capability['source_sha']==CURRENT and
                     capability['product_builds']==capability['SUT_invocations']==0 and
                     not capability['measurement_eligibility'] and not capability['full_R17_acceptance'],
                     'Fresh actual producer host capability did not admit')
                producer.results['host_capability']=receipt(capability_dir/'result.json')
                llvm = pins['clang_tidy']; pin = llvm['install']['macos']
                identity = dict(version=llvm['version'],source_url=pin['source_url'],source_sha256=pin['source_sha256'],
                                architecture='x86_64',host_os_version=platform.mac_ver()[0],
                                developer_directory=os.environ.get('DEVELOPER_DIR','system-default'),
                                deployment_target=pins['macos']['deployment_target'],
                                recipe_sha256=receipt(root/'tools/install_llvm.py')['sha256'],
                                cache_recipe_sha256=receipt(root/'tools/llvm_cache.py')['sha256'])
                identifier = hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()[:16]
                target = Path.home()/'.cache/docenhance/llvm'/f"{llvm['version']}-x86_64-{identifier}"
                parent=target.parent
                need(parent.is_dir() and not parent.is_symlink(), 'LLVM cache parent redirected/missing')
                observed=[]
                with os.scandir(parent) as children:
                    for child in children:
                        need(len(observed)<1,'Unexpected restored LLVM generations/markers')
                        observed.append(child.name)
                need(observed==[target.name], 'Unselected restored LLVM footprint is not admitted')
                need(tree_bytes(parent,producer.deadline-CLEANUP_SECONDS)<=512*MIB,
                     'Restored LLVM exceeds512MiB before payload hashing')
                validate_source_tools(target,identity)
                producer.components[parent] = 512*MIB
                producer.env['DE_CLANG_TIDY_DIR'] = str(target/'bin')
                for name in ['produce.py','qualify_products.py','source-old.json','source-current.json','manifest.json']:
                    producer.frozen[HERE/name] = receipt(HERE/name)
                common = producer.base/'evidence'
                for name,command,expected in [
                    ('cmake',[producer.cm,'--version'],pins['cmake']['version']),
                    ('ninja',[producer.ninja,'--version'],pins['ninja']['version']),
                    ('clang-tidy',[str(target/'bin/clang-tidy'),'--version'],llvm['version']),
                    ('clang-query',[str(target/'bin/clang-query'),'--version'],llvm['version']),
                ]:
                    text = producer.run(common,name,command,10)
                    need(tool_version_matches(name,text,expected), 'Pinned tool version differs: '+name)
                screen_prebuild(producer,capability,common)
                producer.run(common,'compiler-readiness',['/usr/bin/clang++','--version'],30)
                locks = {label:json.loads((workspace/('sut-old' if label=='old' else 'sut-current')/'deps/lock.json').read_bytes())['dependencies']
                         for label in ['old','current']}
                by_role = {label:{dep['name']:dep for dep in deps} for label,deps in locks.items()}
                need(len(by_role['old'])==10 and len(by_role['current'])==9 and
                     len(by_role['old'].keys() | by_role['current'].keys())==11 and
                     all(by_role['old'][name] == by_role['current'][name]
                         for name in by_role['old'].keys() & by_role['current'].keys()),
                     'Exact finite source union differs')
                acquisition_end = min(producer.deadline-CLEANUP_SECONDS,time.monotonic()+300)
                for label in ['old','current']:
                    need(time.monotonic() < acquisition_end, 'Original300s acquisition clock exhausted')
                    checkout = workspace/('sut-old' if label=='old' else 'sut-current')
                    producer.run(common,'fetch-'+label,[sys.executable,'-B',str(checkout/'tools/deps.py'),'fetch','--cache',str(producer.base/'cache')],min(150,acquisition_end-time.monotonic()))
                    producer.run(common,'verify-'+label,[sys.executable,'-B',str(checkout/'tools/deps.py'),'verify','--cache',str(producer.base/'cache')],min(30,acquisition_end-time.monotonic()))
                need(time.monotonic() <= acquisition_end, 'Original300s acquisition exceeded')
                for label in ['old','current']:
                    producer.results[label] = producer.build(label)
                producer.monitor()
                for checkout,source in producer.sources.items():
                    source_stable(checkout,source,producer.deadline-CLEANUP_SECONDS,producer.git,producer.metadata)
                    head=checkout/'.git/HEAD'
                    need(head.read_text().strip()==source['head'],'Final source checkout is not exact detached HEAD')
                    for path in [head,checkout/'.git/index']:
                        need(not path.is_symlink(),'Final source git metadata redirected')
                        producer.frozen[path]=receipt(path)
                producer.results['complete'] = True
            except BaseException as error:
                if any(not row['reaped'] or not row['readers_closed'] for row in producer.rows+producer.metadata_rows):
                    producer.retained_primary=error
                    raise WriterStateUnknownError('Producer child/capture closure unconfirmed; no measurement') from error
                raise
    except BaseException as error:
        primary = getattr(producer,'retained_primary',error)
        if primary is not error:primary.add_note('Producer writer claim retained: child/capture closure unconfirmed')
        producer.results.update(complete=False,error=repr(primary),notes=getattr(primary,'__notes__',[]))
    finally:
        producer.results.update(elapsed_seconds=time.monotonic()-producer.start,
                                owned_children_closure_confirmed=all(row['reaped'] and row['readers_closed'] for row in producer.rows+producer.metadata_rows),
                                claim_absent=not(root/'.cache/linux-gate/active').exists() and not(root/'.cache/linux-gate/active').is_symlink(),
                                heavy_work_finished_unix=time.time(),
                                bounds={'total_seconds':TOTAL_SECONDS,'cleanup_reserve':CLEANUP_SECONDS,
                                        'sampled_owned_aggregate_bytes':3072*MIB,'hard_quota':False,
                                        'charge':'max(st_size,st_blocks*512) per file/link/directory; st_blocks required',
                                        'sample_interval_seconds':3,
                                        'overshoot':'Cooperative operational sampling, not OS reservations or hard quotas; actual overrun observations retained'})
        if time.monotonic()>producer.deadline:
            producer.results['complete'] = False
            primary = primary or TimeoutError('Original producer capsule clock exceeded')
        def final_guard():
            producer.results['claim_absent']=False
            try:(root/'.cache/linux-gate/active').lstat()
            except FileNotFoundError:producer.results['claim_absent']=True
            for checkout,source in producer.sources.items():
                source_stable(checkout,source,producer.deadline-CLEANUP_SECONDS,producer.git)
            for path,expected in producer.frozen.items():
                need(receipt(path)==expected,'Frozen producer/tool drift after result write')
            producer.monitor(final=True)
            need(producer.results['owned_children_closure_confirmed'],'Producer child closure unknown')
            need(producer.results['claim_absent'],'Producer claim not released or newly occupied')
        def retain():
            producer.results['notes']=getattr(primary,'__notes__',[])
            encoded=(json.dumps(producer.results,indent=2)+'\n').encode()
            need(len(encoded)<=8*MIB,'Producer result exceeds8MiB')
            (producer.base/'results.json').write_bytes(encoded)
        try:
            retain();final_guard()
        except BaseException as error:
            producer.results.update(complete=False,post_write_guard_error=repr(error))
            if primary is None:primary=error
            else:primary.add_note('Producer post-write guard also failed: '+repr(error))
        producer.results.update(error=repr(primary) if primary else None,
                                elapsed_seconds=time.monotonic()-producer.start,
                                heavy_work_finished_unix=time.time())
        retain()
        try:final_guard()
        except BaseException as error:
            producer.results.update(complete=False,post_write_budget_error=repr(error),
                                    elapsed_seconds=time.monotonic()-producer.start)
            if primary is None:primary=error
            else:primary.add_note('Final producer metadata observation also failed: '+repr(error))
            producer.results['error']=repr(primary)
            retain()
    if primary is not None:
        raise primary
    print('PASS: both exact fresh CLI producers; no SUT or timing')


if __name__ == '__main__':
    main()
