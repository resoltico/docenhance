"""Reconcile the actual CLI command closure; never require unbuilt test duplicates."""
from pathlib import Path
import hashlib
import json
import shlex
import struct
import time

ROOT = BUILD = APP = SOURCE = OUT = None
END = None


def need(value, message):
    if not value:
        raise RuntimeError(message)


def receipt(path):
    path = Path(path)
    need(END is None or time.monotonic() < END, 'Original grouped qualification deadline exhausted')
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest}


def stable(source):
    for name, digest in source['current_files'].items():
        path = ROOT / name
        need(path.is_file() and receipt(path)['sha256'] == digest and
             oct(path.stat().st_mode & 0o777) == source['current_modes'][name],
             'Frozen current source bytes/modes changed: ' + name)


def path_of(value):
    path = Path(value)
    return (path if path.is_absolute() else APP / path).resolve()


def archive_path(value):
    path = path_of(value)
    need(APP.resolve().is_relative_to(BUILD.resolve()) and
         (APP / 'src').resolve().is_relative_to(APP.resolve()) and
         (BUILD / 'prefix/lib').resolve().is_relative_to(BUILD.resolve()),
         'Actual archive owner anchor escapes fresh build')
    need(path.is_relative_to((APP / 'src').resolve()) or
         path.is_relative_to((BUILD / 'prefix/lib').resolve()),
         'Actual CLI archive resolves outside its firstparty/privateprefix owner')
    need(path.is_file(), 'Actual private archive missing')
    return path


def production_object(value):
    path = path_of(value)
    need(APP.resolve().is_relative_to(BUILD.resolve()) and
         (APP / 'src/CMakeFiles').resolve().is_relative_to(APP.resolve()),
         'Actual object owner anchor escapes fresh build')
    need(path.is_relative_to((APP / 'src/CMakeFiles').resolve()) and path.is_file(),
         'Actual production object resolves outside its configured owner')
    return path


def normalized(command):
    result = []
    index = 0
    while index < len(command):
        token = command[index]
        if token in ['-MF', '-MT', '-MQ']:
            index += 2
        elif token in ['-MD', '-MMD']:
            index += 1
        else:
            result.append(token)
            index += 1
    return result


def qualify(observed, deadline, context):
    global END, ROOT, BUILD, APP, SOURCE, OUT
    END = deadline
    ROOT, BUILD, OUT, SOURCE = (Path(context[key]) for key in ('root','build','out','source'))
    APP = BUILD / 'app'

    source = json.loads(SOURCE.read_bytes())
    need(time.monotonic() < END, 'Original grouped qualification deadline exhausted')
    stable(source)
    need(observed['source'] == receipt(SOURCE), 'Actual producer source receipt differs')
    need(len(observed['rows']) == 22 and
         all(row['complete'] and row['exit_code'] == 0 and row['reaped'] and row['readers_closed']
             for row in observed['rows']), 'Actual successful producer rows incomplete/adverse')
    for row in observed['rows']:
        need(receipt(Path(row['log']['path'])) == row['log'], 'Actual command diagnostics changed')
        need(b'[engine output omitted:' not in Path(row['log']['path']).read_bytes(),
             'Actual diagnostics truncated')
    build_identity = json.loads((BUILD / 'build-identity.json').read_bytes())
    need(build_identity == json.loads((APP / 'build-identity.json').read_bytes()),
         'Actual private outer/inner configuration differs')
    need(build_identity['CMAKE_CXX_COMPILER_ID'] == 'AppleClang' and
         (build_identity['CMAKE_CXX_COMPILER_VERSION'] == context['clang_semantic_version'] or
          build_identity['CMAKE_CXX_COMPILER_VERSION'].startswith(context['clang_semantic_version']+'.')) and
         build_identity['CMAKE_BUILD_TYPE'] == 'Release' and
         build_identity['CMAKE_OSX_DEPLOYMENT_TARGET'] == '15.0' and
         build_identity['CMAKE_OSX_ARCHITECTURES'] == 'x86_64' and
         build_identity['DE_TOOLCHAIN'] == 'platform' and
         build_identity['CMAKE_CXX_FLAGS'] == build_identity['CMAKE_C_FLAGS'] == '' and
         build_identity['CMAKE_CXX_FLAGS_RELEASE'] == build_identity['CMAKE_C_FLAGS_RELEASE'] == '-O3 -DNDEBUG', 'Actual selected Release compiler/config differs')
    need(all(build_identity[key].upper() in ['OFF', 'FALSE', '0', ''] for key in
             ['DE_ENABLE_IPO', 'DE_ENABLE_ASAN', 'DE_ENABLE_UBSAN', 'DE_ENABLE_TSAN', 'DE_ENABLE_FUZZING']),
         'Actual Release producer has sanitizer/IPO/fuzz')
    need(build_identity['DE_BUILD_TESTS'].upper() in ['ON', 'TRUE', '1'], 'Original testsTRUE preset changed')
    lock = json.loads((ROOT / 'deps/lock.json').read_bytes())
    dependency_caches = [receipt(BUILD / 'deps' / dep['name'] / 'CMakeCache.txt')
                         for dep in lock['dependencies']]
    need(len(dependency_caches) == context['dependency_count'], 'Own lock dependency closure differs')
    native_features = receipt(ROOT / 'deps/features.json')
    for key, expected in context['compiler_hashes'].items():
        need(build_identity[key] == expected, 'Actual configured compiler bytes differ: ' + key)
    from package_inspection import native_import_values
    loader_imports = native_import_values('Darwin', (OUT / 'loader-imports.log').read_text())
    need((APP/'compile_commands.json').stat().st_size<=2*1024**2, 'Actual CDB exceeds2MiB')
    database = json.loads((APP / 'compile_commands.json').read_bytes())
    selected = {}
    ignored_test_duplicates = []
    for entry in database:
        if not Path(entry['file']).resolve().is_relative_to((ROOT / 'src').resolve()):
            continue
        argv = entry.get('arguments') or shlex.split(entry['command'])
        target = path_of(argv[argv.index('-o') + 1])
        if target.is_relative_to((APP / 'tests').resolve()):
            ignored_test_duplicates.append({'file': entry['file'], 'output': str(target)})
            continue
        target = production_object(str(target))
        key = (entry['file'], str(target))
        need(key not in selected, 'Duplicate actual production Cdb source/output')
        selected[key] = normalized(argv)
    actual = {}
    lines = (OUT / 'actual-cli-commands.log').read_text().splitlines()
    for line in lines:
        tokens = shlex.split(line)
        if '--' not in tokens or '-c' not in tokens:
            continue
        argv = tokens[tokens.index('--') + 1:]
        need(argv[0] == '/usr/bin/clang++' and argv.count('-o') == argv.count('-c') == 1,
             'Actual CLI compiler command not direct pinnedC++')
        key = (argv[argv.index('-c') + 1], str(production_object(argv[argv.index('-o') + 1])))
        need(key not in actual and key in selected and normalized(argv) == selected[key],
             'Actual CLI command differs from exact configured compiler/source/object')
        need('-O3' in argv and '-DNDEBUG' in argv and '-Werror' in argv and
             '-mmacosx-version-min=15.0' in argv and
             not any(value.startswith(('-fsanitize=', '-flto')) for value in argv),
             'Actual CLI Release flags differ')
        need('--warnings-as-errors=*' in line and '__run_co_compile' in line,
             'Actual strict first-party clang-tidy wrapper absent')
        actual[key] = receipt(Path(key[1]))
    expected_sources = {(ROOT / name).resolve() for name in source['current_files']
                        if name.startswith('src/') and name.endswith('.cpp')}
    cmake_manifest = APP / 'architecture-targets.json'
    need(cmake_manifest.stat().st_size<=2*1024**2, 'Actual CMake target manifest exceeds2MiB')
    targets = json.loads(cmake_manifest.read_bytes())
    architecture = json.loads((ROOT / 'spec/architecture.json').read_bytes())
    external = {target for package in architecture['packages'].values()
                for target in package['targets']}
    pending, closure, declared_sources = ['docenhance'], set(), set()
    while pending:
        target = pending.pop()
        if target in closure:
            continue
        need(target in targets, 'Actual CLI target is absent from CMake source ownership')
        closure.add(target)
        owner = targets[target]
        declared_sources.update(Path(name).resolve() for name in owner['files']
                                if name.endswith('.cpp'))
        for dependency in owner['links']:
            if dependency in targets:
                pending.append(dependency)
            else:
                need(dependency in external, 'Unknown actual CLI direct-link target')
    need(expected_sources and declared_sources == expected_sources,
         'Actual CMake CLI source closure differs from exact committed production inventory')
    need(len(actual) == len(expected_sources) and
         {Path(key[0]).resolve() for key in actual} == expected_sources and
         set(actual) == set(selected), 'Actual committed-source CLI object closure differs')
    link = shlex.split(lines[-1])
    need('-c' not in link and '/usr/bin/clang++' in link and link.count('-o') == 1 and
         path_of(link[link.index('-o') + 1]) == (BUILD / 'bin/docenhance').resolve(), 'Actual CLI link output differs')
    archives = []
    for value in link:
        if value.endswith('.a'):
            path = archive_path(value)
            archives.append(receipt(path))
        elif value.endswith('.o'):
            production_object(value)
    need(archives and not any('Catch2' in entry['path'] for entry in archives),
         'Actual CLI links empty/test archive closure')
    binary = BUILD / 'bin/docenhance'
    need(binary.stat().st_size<=16*1024**2, 'Actual CLI exceeds16MiB')
    data = binary.read_bytes()
    header = struct.unpack_from('<IIIIIIII', data)
    need(header[0] == 0xfeedfacf and header[1] == 0x1000007 and header[3] == 2,
         'Actual CLI not x86_64 Mach-O executable')
    offset = 32
    versions = []
    for _ in range(header[4]):
        command, length = struct.unpack_from('<II', data, offset)
        need(length >= 8 and offset + length <= len(data), 'Malformed Mach-O metadata')
        if command == 0x32:
            platform, minimum, sdk, _ = struct.unpack_from('<IIII', data, offset + 8)
            versions.append({'platform': platform, 'minimum': minimum, 'sdk': sdk})
        offset += length
    need(versions == [{'platform': 1, 'minimum': 15 << 16, 'sdk': context['sdk_encoding']}],
         'Actual Mach-O deployment/SDK metadata differs')
    stable(source)
    result = {'complete': True, 'kind': 'FRESH_SAME_HOST_PRODUCER_INLINE_QUALIFICATION',
              'actual_completed_rows': observed['rows'], 'source': receipt(SOURCE),
              'binary': receipt(binary), 'build_identity': receipt(BUILD / 'build-identity.json'),
              'database': receipt(APP / 'compile_commands.json'),
              'actual_commands': receipt(OUT / 'actual-cli-commands.log'),
              'actual_production_objects': list(actual.values()), 'actual_link_archives': archives, 'dependency_cache_receipts': dependency_caches,
              'native_features': native_features, 'loader_imports': loader_imports,
              'cmake_target_manifest': receipt(cmake_manifest), 'CLI_target_closure': sorted(closure),
              'expected_production_CPPs': len(expected_sources),
              'excluded_unbuilt_test_duplicates': ignored_test_duplicates,
              'Mach_O_LC_BUILD_VERSION': versions, 'resources': observed['resources'],
              'same_outer_deadline_monotonic': deadline,
              'scope': 'Fresh exact-source Intel macOS15 platform Release CLI under one outer capsule deadline. Actual compiler/Xcode/SDK/linker before/after match; source-owned recipes, private prefix, actual native caches, features, strict CLI CDB/object/link and loader evidence retained. No tests, CLI/SUT execution, campaign or timing.',
              'source_guard': 'Current committed sourcehashes/modes checkedbeforeafter; parent mustcomplete source/HEAD/membership/ownedchild/claimcleanup guards before accepting thisinlinequalification. No oldprefix/adoption.'}
    encoded = (json.dumps(result,indent=2)+'\n').encode()
    need(len(encoded)<=2*1024**2, 'Actual qualification exceeds2MiB')
    with (OUT / 'producer-qualification.json').open('xb') as stream:
        stream.write(encoded)
    need(time.monotonic() < END, 'Original grouped closure qualification exceeded')
    return receipt(OUT / 'producer-qualification.json')


if __name__ == '__main__':
    raise RuntimeError('Inline bound producer qualifier only; no independent clock/execution')
