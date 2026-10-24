"""One isolated actual Windows Debug resource scope; no Release or product CLI qualification."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, stat, sys, time

LOG_BYTES = 2 * 1024**2
ARTIFACT_BYTES = 64 * 1024**2
CONTROL_BYTES = 32 * 1024**2
BUILD_BYTES = 2 * 1024**3
BUILD_ENTRIES = 100_000

def need(ok, message):
    if not ok:
        raise ValueError(message)

def regular(path):
    info = path.lstat()
    need(stat.S_ISREG(info.st_mode) and not (getattr(info, 'st_file_attributes', 0) or 0) & 1024, f'Nonregular identity: {path}')
    return info

def identity(path, limit=64 * 1024**2):
    before = regular(path)
    need(before.st_size <= limit, f'Identity exceeds bound: {path}')
    with path.open('rb') as stream:
        opened = os.fstat(stream.fileno())
        need((opened.st_dev, opened.st_ino, opened.st_size, opened.st_mtime_ns) == (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns), 'Identity acquisition changed')
        hasher = hashlib.sha256()
        consumed = 0
        while block := stream.read(min(65536, limit + 1 - consumed)):
            consumed += len(block)
            need(consumed <= limit, 'Identity grew beyond bound')
            hasher.update(block)
        digest = hasher.hexdigest()
        after = os.fstat(stream.fileno())
    end = regular(path)
    need((after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) == (opened.st_dev, opened.st_ino, opened.st_size, opened.st_mtime_ns, opened.st_ctime_ns), 'Identity FD changed')
    need((end.st_dev, end.st_ino, end.st_size, end.st_mtime_ns) == (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns), 'Identity pathname changed')
    return {'path': str(path), 'bytes': end.st_size, 'sha256': digest}

def scan(directory, byte_limit, entry_limit):
    total = count = 0
    if not directory.exists():
        return {'charged_bytes': 0, 'entries': 0}
    pending = [directory]
    while pending:
        current = pending.pop()
        try:
            owner = current.lstat()
        except FileNotFoundError:
            continue
        need(stat.S_ISDIR(owner.st_mode) and not (getattr(owner, 'st_file_attributes', 0) or 0) & 1024, 'Owned workspace directory redirected')
        with os.scandir(current) as entries:
            for entry in entries:
                count += 1
                need(count <= entry_limit, 'Owned workspace entry stop reached')
                try:
                    info = entry.stat(follow_symlinks=False)
                except FileNotFoundError:
                    continue
                need(not stat.S_ISLNK(info.st_mode) and not (getattr(info, 'st_file_attributes', 0) or 0) & 1024, 'Owned workspace redirect refused')
                if stat.S_ISDIR(info.st_mode):
                    pending.append(Path(entry.path))
                else:
                    need(stat.S_ISREG(info.st_mode), 'Owned workspace special entry refused')
                    total += max(info.st_size, getattr(info, 'st_blocks', 0) * 512)
                    need(total <= byte_limit, 'Owned workspace byte stop reached')
        try:
            after = current.lstat()
        except FileNotFoundError:
            continue
        need((after.st_dev, after.st_ino) == (owner.st_dev, owner.st_ino) and stat.S_ISDIR(after.st_mode) and not (getattr(after, 'st_file_attributes', 0) or 0) & 1024, 'Owned workspace directory changed')
    return {'charged_bytes': total, 'entries': count}

def source_check(root, frozen):
    expected = frozen['current_files']
    observed = {}
    need(len(expected) <= 4096, 'Source inventory too large')
    for name, digest in expected.items():
        path = root / name
        need(identity(path, 16 * 1024**2)['sha256'] == digest, f'Frozen source changed: {name}')
        info = path.lstat()
        observed[name] = {'filesystem_mode': oct(info.st_mode & 0o777), 'file_attributes': getattr(info, 'st_file_attributes', 0) or 0}
    return observed

def index_modes(text, frozen):
    rows = [row.split('\t', 1) for row in text.split('\0') if row]
    actual = {name: '0o' + header.split()[0][-3:] for header, name in rows if header.split()[2] == '0'}
    need(len(actual) == len(rows) and actual == frozen['current_modes'], 'Checkout Git index source modes/membership differ')


def capture_adapter(capture, guard):
    """Explicit one-off composition: preserve the actual monitor, log limit and interval."""
    def observed(command, env, log, timeout, policy):
        def both():
            policy.monitor()
            guard()
        return capture(command, env, log, timeout, type(policy)(policy.log_limit_bytes, both, policy.interval))
    return observed

def write_results(output, record, deadline, primary):
    """Final metadata is bounded and cannot turn a late/refused observation into success."""
    path = output / 'results.json'
    try:
        body = (json.dumps(record, indent=2) + '\n').encode('utf-8')
        need(len(body) <= 4 * 1024**2, 'Final metadata exceeds original4MiB metadata reservation')
        scan(output, CONTROL_BYTES - len(body), 256)
        need(time.monotonic() < deadline or primary is not None, 'Final observation exceeded original clock')
        path.write_bytes(body)
        scan(output, CONTROL_BYTES, 256)
        need(time.monotonic() < deadline or primary is not None, 'Final metadata write exceeded original clock')
    except BaseException as error:
        fallback = {'kind': 'DEBUG_SUBSCOPE_ONLY', 'complete': False, 'primary_error': repr(primary), 'metadata_error': repr(error), 'original_logs_retained': True}
        path.write_text(json.dumps(fallback, indent=2) + '\n')
        if primary is not None:
            primary.add_note(f'Debug metadata finalization also failed: {error!r}')
            raise primary from error
        raise

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--head', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    root, output = args.source.resolve(), args.output.resolve()
    need(not output.exists() and not (root / 'out/dev').exists(), 'Fresh private owners required')
    output.mkdir(parents=True)
    sys.path.insert(0, str(root / 'tools'))
    from process_execution import CapturePolicy, bounded_capture
    from audit_build import read_cache
    from windows_resource_producer import dependency_crt, native_context
    import windows_resource_execution as execution
    import windows_resource_records as records
    start = time.monotonic()
    deadline = start + 2820
    record = {'kind': 'DEBUG_SUBSCOPE_ONLY', 'complete': False, 'product_CLI_invocations': 0, 'application_CLI_builds': 0, 'resource_helper_calls': 0, 'commands': [], 'head': args.head, 'caps': {'work_seconds': 2820, 'cleanup_reserve_seconds': 120, 'configure_seconds': 180, 'dependency_seconds': 1800, 'parallel': 2, 'log_bytes': LOG_BYTES, 'artifact_bytes': ARTIFACT_BYTES, 'build_bytes': BUILD_BYTES, 'build_entries': BUILD_ENTRIES}}
    os.chdir(root)
    frozen = json.loads(args.manifest.read_bytes())
    def monitor():
        need(time.monotonic() < deadline, 'Original preflight work deadline exhausted')
        build = scan(root / 'out/dev', BUILD_BYTES, BUILD_ENTRIES)
        evidence = scan(output, CONTROL_BYTES, 256)
        if execution.OUT.exists():
            scan(execution.OUT, 384 * 1024**2, 4096)
        record['high_water_build_bytes'] = max(record.get('high_water_build_bytes', 0), build['charged_bytes'])
        record['high_water_artifact_bytes'] = max(record.get('high_water_artifact_bytes', 0), evidence['charged_bytes'])
    def run(name, argv, cap):
        row = {'name': name, 'argv': argv, 'cap_seconds': min(cap, deadline - time.monotonic()), 'capture_returned': False}
        need(row['cap_seconds'] > 0, 'No command work room')
        log = output / (name + '.log')
        record['commands'].append(row)
        entered = time.monotonic()
        try:
            row['exit_code'] = bounded_capture(argv, dict(os.environ), log, row['cap_seconds'], CapturePolicy(LOG_BYTES, monitor, 5))
            row['capture_returned'] = True
            row['log'] = identity(log, LOG_BYTES)
            need(row['exit_code'] == 0 and b'[engine output omitted:' not in log.read_bytes(), f'Actual {name} failed or diagnostics omitted')
        finally:
            row['elapsed_seconds'] = time.monotonic() - entered
            row['error'] = repr(sys.exception()) if sys.exception() is not None else None
            if log.exists():
                row['log'] = identity(log, LOG_BYTES)
        monitor()
        return log.read_text(encoding='utf-8').strip()
    try:
        need(frozen['head'] == args.head, 'Manifest head differs')
        before_modes = source_check(root, frozen)
        (output / 'source-filesystem-before.json').write_text(json.dumps(before_modes, sort_keys=True) + '\n')
        record['source_filesystem_before'] = identity(output / 'source-filesystem-before.json', 2 * 1024**2)
        record['source_manifest'] = identity(args.manifest, 2 * 1024**2)
        git = shutil.which('git')
        need(git is not None, 'Selected Git missing')
        need(run('source-head-before', [git, 'rev-parse', 'HEAD'], 10) == args.head, 'Checkout HEAD differs')
        need(not run('source-status-before', [git, 'status', '--porcelain', '--untracked-files=normal'], 10), 'Checkout dirty')
        index_modes(run('source-index-before', [git, 'ls-files', '--stage', '-z'], 10), frozen)
        record['selected_cl'] = identity(Path(shutil.which('cl.exe') or 'missing-cl'))
        run('runtime-platform', [sys.executable, 'tools/check_runtime_platform.py', '--platform', 'windows-x86_64'], 10)
        need(execution.ROOT == root and execution.OUT == root / 'out/windows-resource-proof', 'Actual Debug helper source owner differs')
        need(not execution.OUT.exists(), 'Fresh actual Debug evidence owner required')
        (execution.OUT / 'debug').mkdir(parents=True)
        record['debug'] = {}
        original_capture = execution.bounded_capture
        record['capture_adapter'] = {'source': identity(Path(__file__), 2 * 1024**2), 'actual_capture_source': identity(root / 'tools/process_execution.py', 2 * 1024**2), 'composition': 'original policy monitor first, then additional sampled workspace guard; same log byte limit, interval, timeout and command'}
        execution.bounded_capture = capture_adapter(original_capture, monitor)
        try:
            execution.debug_producer(deadline, record['debug'])
            monitor()
            run('debug-audit', [sys.executable, 'tools/audit_build.py', '--build', 'out/dev'], 60)
            record['debug']['execution'] = {}
            record['resource_helper_calls'] = 1
            execution.debug_cases(deadline, record['debug']['execution'], records.executable_paths(record['debug']['producer']))
            records.stable_producer(record['debug'])
            need(len(record['debug']['execution']['selected_cases']) == 18, 'Debug scope did not retain all18 actual resource cases')
            record['debug_evidence_size'] = scan(execution.OUT, 384 * 1024**2, 4096)
            monitor()
        finally:
            execution.bounded_capture = original_capture
        build = root / 'out/dev'
        config = json.loads((build / 'build-identity.json').read_bytes())
        need(config['CMAKE_SYSTEM_NAME'] == 'Windows' and config['CMAKE_CXX_COMPILER_ID'] == 'MSVC' and config['CMAKE_BUILD_TYPE'] == 'Debug' and config['CMAKE_MSVC_RUNTIME_LIBRARY'] == 'MultiThreadedDebug', 'Actual Debug MSVC/CRT identity differs')
        record['build_identity'] = identity(build / 'build-identity.json', 2 * 1024**2)
        record['dependency_crt'] = {}
        dependency_crt(build, 'MultiThreadedDebug', record['dependency_crt'])
        for directory in os.environ['LIB'].split(';'):
            if directory:
                for name in ('libcmtd.lib', 'libucrtd.lib', 'libvcruntimed.lib', 'libcpmtd.lib'):
                    candidate = Path(directory) / name
                    if candidate.is_file():
                        need(candidate.stat().st_size <= 64 * 1024**2, 'CRT identity exceeds bound')
        record['native_context'] = native_context(debug=True)
        record['zsd_archive'] = identity(build / 'prefix/lib/zsd.lib')
        candidates = [name + '.lib' for name in ('zsd', 'zs', 'zlibstaticd', 'zlibstatic', 'zlibstat', 'zd', 'z') if (build / 'prefix/lib' / (name + '.lib')).exists()]
        need(candidates == ['zsd.lib'], 'Fresh Debug zlib candidates differ from locked zsd owner')
        record['zlib_named_candidates'] = candidates
        record['actual_zlib_cache'] = identity(build / 'deps/zlib/CMakeCache.txt', 2 * 1024**2)
        record['configured_app_cache'] = identity(build / 'app/CMakeCache.txt', 2 * 1024**2)
        record['selected_compiler'] = identity(Path(config['CMAKE_CXX_COMPILER']))
        record['selected_linker'] = identity(Path(read_cache(build / 'app/CMakeCache.txt')['CMAKE_LINKER']))
        need(not (build / 'bin/docenhance.exe').exists(), 'Unexpected product CLI build')
        after_modes = source_check(root, frozen)
        (output / 'source-filesystem-after.json').write_text(json.dumps(after_modes, sort_keys=True) + '\n')
        record['source_filesystem_after'] = identity(output / 'source-filesystem-after.json', 2 * 1024**2)
        need(before_modes == after_modes, 'Actual source filesystem modes/attributes changed')
        need(run('source-head-after', [git, 'rev-parse', 'HEAD'], 10) == args.head, 'Final checkout HEAD differs')
        need(not run('source-status-after', [git, 'status', '--porcelain', '--untracked-files=normal'], 10), 'Final checkout dirty')
        index_modes(run('source-index-after', [git, 'ls-files', '--stage', '-z'], 10), frozen)
        monitor()
        record['complete'] = True
    except BaseException as error:
        record['error'] = repr(error)
        record['notes'] = getattr(error, '__notes__', [])
        raise
    finally:
        record['elapsed_seconds'] = time.monotonic() - start
        record['controller_captures_returned'] = all(row['capture_returned'] for row in record['commands'])
        record['cleanup_scope'] = 'Existing Windows helper direct-child ownership; descendant cleanup after timeout remains CI job owner. No instantaneous process/disk quota claim.'
        write_results(output, record, deadline, sys.exception())
if __name__ == '__main__':
    main()
