# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# REVIEW TEXT ONLY. No authorization to run this collector accompanies this file.
# Installation/source archives are read-only. No compiler, linker, build, test or hook.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw 'Fresh collector output directory required' }
$null = New-Item -ItemType Directory -Path $root
$receipts = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[string]]::new()
$commands = [Collections.Generic.List[object]]::new()
$sourceReceipts = [Collections.Generic.List[object]]::new()
$started = [DateTime]::UtcNow
$maximumOutputBytes = 2097152
$commandRawLimit = 16777216
$commandSeconds = 30
$maximumMembers = 24
$memberCount = 0
$selectionPayload = $null
$firstCollectorFailure = $null
$activeArchiveRecord = $null
$activeSourceRecord = $null
$privateRoot = Join-Path $env:RUNNER_TEMP ('crt-members-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $privateRoot

function RemainingMilliseconds {
    $remaining = 480000 - ([DateTime]::UtcNow - $started).TotalMilliseconds
    if ($remaining -le 0) { throw 'Global inspection deadline 480 seconds expired' }
    return [int][Math]::Max(1, [Math]::Floor($remaining))
}
function Identity([string]$Path) {
    $null = RemainingMilliseconds
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $null = RemainingMilliseconds
    $identity = [ordered]@{ path = $item.FullName; bytes = $item.Length; sha256 = $hash;
        file_version = $item.VersionInfo.FileVersion; product_version = $item.VersionInfo.ProductVersion }
    $null = RemainingMilliseconds
    return $identity
}
function EncodeJson($Value) {
    return [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Value -Depth 30) + "`n")
}
function PayloadDigest([byte[]]$Bytes) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}
function WriteReceipt([string]$Name, [byte[]]$Bytes, [int]$Ceiling) {
    # Control writes are attempted even after inspection expiry; actual I/O is not interruptible.
    $used = (Get-ChildItem -LiteralPath $root -File | Measure-Object Length -Sum).Sum
    if ($null -eq $used) { $used = 0 }
    if ($Bytes.Length -gt $Ceiling -or $used + $Bytes.Length -gt $maximumOutputBytes) {
        throw "Receipt $Name cannot fit its reserved output portion"
    }
    [IO.File]::WriteAllBytes((Join-Path $root $Name), $Bytes)
}
function SaveBulk([string]$Name, $Value, [int]$RemainingBulkFiles) {
    # Called ONCE for each of the three bulk files; a rejected payload is never retried.
    $bytes = $null
    $reason = $null
    try {
        $null = RemainingMilliseconds
        $bytes = EncodeJson $Value
        $null = RemainingMilliseconds
        $used = (Get-ChildItem -LiteralPath $root -File | Measure-Object Length -Sum).Sum
        if ($null -eq $used) { $used = 0 }
        # Reserve 64 KiB for commands/result, plus 2 KiB omission space per later bulk file.
        $available = $maximumOutputBytes - 65536 - $used - 2048 * $RemainingBulkFiles
        if ($bytes.Length -gt $available) { $reason = 'bulk_payload_exceeds_available_portion' }
    } catch { $reason = $_.Exception.Message }
    if ($null -ne $reason) {
        $failures.Add("$Name omitted: $reason")
        $bytes = EncodeJson ([ordered]@{ status = 'detail_omitted'; context = $Name;
            reason = $reason; record_count = if ($null -eq $Value) { 0 } else { @($Value).Count };
            rejected_utf8_bytes = if ($null -eq $bytes) { $null } else { $bytes.Length };
            rejected_sha256 = if ($null -eq $bytes) { $null } else { PayloadDigest $bytes };
            size_digest_unavailable_reason = if ($null -eq $bytes) { 'serialization not completed before deadline/failure' } else { $null };
            completeness = 'unearned' })
    }
    try { WriteReceipt $Name $bytes ($maximumOutputBytes - 65536) }
    catch { $failures.Add("Persistence $Name failed: $($_.Exception.Message)"); Write-Warning "Persistence $Name failed" }
}
function Inspect([string]$Tool, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Tool
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $output = [IO.MemoryStream]::new()
    $errorOutput = [IO.MemoryStream]::new()
    $buffers = @([byte[]]::new(8192), [byte[]]::new(8192))
    $streams = @($null, $null)
    $tasks = @($null, $null)
    $done = @($false, $false)
    $rawBytes = 0
    $status = 'not_started'
    $reason = 'global_timeout'
    $didStart = $false
    $startAttempted = $false
    $joined = $false
    $observedExit = $null
    $firstFailure = $null
    $stdout = $null
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    $cleanupStarted = $null
    $workSeconds = 0.0
    try {
        $null = RemainingMilliseconds
        $reason = 'start_failure'
        $startAttempted = $true
        if (-not $process.Start()) { throw 'Process.Start did not confirm launch' }
        $didStart = $true
        $status = 'running'
        $reason = 'read_failure'
        $streams = @($process.StandardOutput.BaseStream, $process.StandardError.BaseStream)
        for ($i = 0; $i -lt 2; $i++) {
            $tasks[$i] = $streams[$i].ReadAsync($buffers[$i], 0, $buffers[$i].Length)
        }
        while (-not ($done[0] -and $done[1])) {
            $reason = 'global_timeout'
            $globalRemaining = RemainingMilliseconds
            $reason = 'command_timeout'
            $commandRemaining = 1000 * $commandSeconds - $clock.Elapsed.TotalMilliseconds
            if ($commandRemaining -le 0) { throw 'Inspection exceeded 30 seconds' }
            for ($i = 0; $i -lt 2; $i++) {
                if ($done[$i] -or -not $tasks[$i].IsCompleted) { continue }
                $reason = 'read_failure'
                $count = $tasks[$i].GetAwaiter().GetResult()
                if ($count -eq 0) { $done[$i] = $true; continue }
                $rawBytes += $count
                $reason = 'raw_bound'
                if ($rawBytes -gt $commandRawLimit) { throw 'Inspection exceeded 16 MiB raw stream bound' }
                $reason = 'read_failure'
                $sink = if ($i -eq 0) { $output } else { $errorOutput }
                $sink.Write($buffers[$i], 0, $count)
                $tasks[$i] = $streams[$i].ReadAsync($buffers[$i], 0, $buffers[$i].Length)
            }
            if (-not ($done[0] -and $done[1])) {
                $reason = 'global_timeout'; $globalRemaining = RemainingMilliseconds
                $reason = 'command_timeout'
                $commandRemaining = 1000 * $commandSeconds - $clock.Elapsed.TotalMilliseconds
                if ($commandRemaining -le 0) { throw 'Inspection exceeded 30 seconds' }
                [Threading.Thread]::Sleep([int][Math]::Max(1,[Math]::Min(5,[Math]::Min($globalRemaining,$commandRemaining))))
            }
        }
        $reason = 'global_timeout'; $globalRemaining = RemainingMilliseconds
        $reason = 'command_timeout'
        $commandRemaining = 1000 * $commandSeconds - $clock.Elapsed.TotalMilliseconds
        if ($commandRemaining -le 0) { throw 'Inspection exceeded 30 seconds before join' }
        $joinMilliseconds = [int][Math]::Max(1,[Math]::Min(1000,[Math]::Min($globalRemaining,$commandRemaining)))
        $reason = 'join_failure'
        $joined = $process.WaitForExit($joinMilliseconds)
        if (-not $joined) {
            $reason = 'global_timeout'; $null = RemainingMilliseconds
            $reason = 'command_timeout'
            if ($clock.Elapsed.TotalSeconds -ge $commandSeconds) { throw 'Inspection deadline expired while joining' }
            $reason = 'join_failure'; throw 'Inspection did not join after EOF within admitted wait'
        }
        $observedExit = $process.ExitCode
        $reason = 'nonzero_exit'
        if ($observedExit -ne 0) {
            $stderr = [Text.Encoding]::Default.GetString($errorOutput.ToArray())
            throw "Inspection exit ${observedExit}: $($stderr.Substring(0,[Math]::Min(600,$stderr.Length)))"
        }
        $reason = 'read_failure'
        $stdout = [Text.Encoding]::Default.GetString($output.ToArray())
        $reason = 'global_timeout'; $null = RemainingMilliseconds
        $reason = 'command_timeout'
        if ($clock.Elapsed.TotalSeconds -ge $commandSeconds) { throw 'Inspection exceeded 30 seconds before completion' }
        $status = 'completed'; $reason = 'normal_exit'
    } catch {
        $firstFailure = $_.Exception
        $status = if ($didStart) { 'failed_after_start' } elseif ($startAttempted) { 'start_not_confirmed' } else { 'not_started' }
    } finally {
        $workSeconds = $clock.Elapsed.TotalSeconds
        $cleanupStarted = [Diagnostics.Stopwatch]::StartNew()
        if ($didStart -and -not $joined) {
            try { if (-not $process.HasExited) { $process.Kill($true) } }
            catch { $cleanupErrors.Add("Kill/exit observation: $($_.Exception.Message)") }
            try { $joined = $process.WaitForExit(5000); if (-not $joined) { $cleanupErrors.Add('Cleanup join unconfirmed after 5000 ms') } }
            catch { $cleanupErrors.Add("Cleanup join: $($_.Exception.Message)") }
        }
        if ($didStart) {
            try { if ($process.HasExited) { $observedExit = $process.ExitCode } }
            catch { $cleanupErrors.Add("Exit observation: $($_.Exception.Message)") }
        }
        foreach ($owner in @($process,$output,$errorOutput)) {
            try { $owner.Dispose() }
            catch { $cleanupErrors.Add("Dispose: $($_.Exception.Message)") }
        }
        if ($cleanupErrors.Count -and $null -eq $firstFailure) {
            $firstFailure = [Exception]::new($cleanupErrors[0])
            $status = 'failed_after_start'; $reason = 'cleanup_failure'
        }
        # Receipt insertion occurs after contained cleanup, retaining the FIRST real failure.
        $commands.Add([ordered]@{ tool = $Tool; arguments = $Arguments; start_attempted = $startAttempted;
            did_start = if ($startAttempted -and -not $didStart) { $null } else { $didStart };
            start_confirmed = $didStart;
            status = $status; terminal_reason = $reason; first_error = if ($firstFailure) { $firstFailure.Message } else { $null };
            joined = $joined; observed_exit_code = $observedExit; raw_bytes = $rawBytes;
            inspection_seconds = $workSeconds; cleanup_seconds = $cleanupStarted.Elapsed.TotalSeconds;
            cleanup_errors = $cleanupErrors.ToArray(); elapsed_seconds = $clock.Elapsed.TotalSeconds })
    }
    if ($null -ne $firstFailure) { throw $firstFailure }
    return $stdout
}
function RelevantLines([string]$Text, [int]$Limit = 160) {
    $null = RemainingMilliseconds
    $lines = @($Text -split '\r?\n' | Where-Object {
        $_ -match '(?i)(malloc|free|new_handler|newhandler|new_mode|newmode|new_scalar|new_array|operator new|operator delete|heapalloc|heapfree|relocations|external|archive member name|machine \(x64\))'
    })
    $retained = @($lines | Select-Object -First $Limit)
    $characterClipped = @($retained | Where-Object { $_.Length -gt 260 }).Count
    $null = RemainingMilliseconds
    return [ordered]@{ matching_lines = $lines.Count; retained_lines = $retained.Count; retained_limit = $Limit;
        line_count_truncated = $lines.Count -gt $Limit; character_truncated = $characterClipped -gt 0;
        character_clipped_lines = $characterClipped; original_retained_line_lengths = @($retained | ForEach-Object { $_.Length });
        truncated = $lines.Count -gt $Limit -or $characterClipped -gt 0;
        lines = @($retained | ForEach-Object { $_.Substring(0,[Math]::Min(260,$_.Length)) }) }
}
try {
    if (-not [OperatingSystem]::IsWindows() -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
        throw 'One native x64 Windows host is required'
    }
    # EXACT selection/activation owner: tools/ci_windows.ps1 at f39.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $null = RemainingMilliseconds
    $installation = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'A current Visual Studio C++ toolchain is required' }
    $devshell = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
    $null = RemainingMilliseconds
    & $devshell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $null = RemainingMilliseconds
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC developer environment was not activated' }
    $toolPaths = [ordered]@{}
    $toolIdentities = [ordered]@{}
    foreach ($tool in @('cl.exe', 'link.exe', 'lib.exe', 'dumpbin.exe')) {
        $null = RemainingMilliseconds
        $selected = Get-Command $tool -CommandType Application -ErrorAction Stop | Select-Object -First 1
        $toolPaths[$tool] = $selected.Source
        $toolIdentities[$tool] = Identity $selected.Source
        if ($selected.Source -notlike (Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\*')) {
            throw "Selected $tool is outside the selected native x64 toolset"
        }
    }
    $allowlistedEnvironment = [ordered]@{}
    foreach ($name in @('VSCMD_VER','VisualStudioVersion','VCToolsVersion','VCToolsInstallDir',
        'VSINSTALLDIR','WindowsSdkDir','WindowsSDKVersion','WindowsSDKLibVersion',
        'UniversalCRTSdkDir','UCRTVersion','VSCMD_ARG_HOST_ARCH','VSCMD_ARG_TGT_ARCH')) {
        $allowlistedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
    }
    $search = [ordered]@{}
    foreach ($name in @('PATH','LIB','LIBPATH','INCLUDE')) {
        # Ordered build-tool search paths only; no other environment values are collected.
        $search[$name] = @([Environment]::GetEnvironmentVariable($name) -split ';' | Where-Object { $_ })
    }
    $selectionPayload = [ordered]@{ installation = $installation;
        vswhere = Identity $vswhere; activation = Identity $devshell;
        selected = $allowlistedEnvironment; ordered_search = $search; tools = $toolIdentities;
        workflow_commit = $env:GITHUB_SHA; runner_os = $env:RUNNER_OS; runner_arch = $env:RUNNER_ARCH;
        claim = 'Installed selected tools only; no compiler invocation or actual product link/runtime proof' }
    $archiveNames = @('libcmt.lib','libcmtd.lib','libucrt.lib','libucrtd.lib',
        'libvcruntime.lib','libvcruntimed.lib','libcpmt.lib','libcpmtd.lib')
    foreach ($name in $archiveNames) {
        $null = RemainingMilliseconds
        $first = $null
        $considered = [Collections.Generic.List[string]]::new()
        foreach ($directory in $search['LIB']) {
            $candidate = Join-Path $directory $name
            $considered.Add($candidate)
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $first = $candidate; break }
        }
        if ($null -eq $first) {
            $receipts.Add([ordered]@{ archive = $name; status = 'missing'; searched_in_order = $considered })
            $failures.Add("Missing FIRST-resolved static CRT archive $name")
            continue
        }
        $archive = Identity $first
        $record = [ordered]@{ archive = $archive; status = 'identity_acquired_inspection_pending';
            resolution = 'FIRST existing name in activated LIB order';
            configuration = if ($name -match 'd\.lib$') { '/MTd Debug' } else { '/MT Release/RelWithDebInfo' };
            searched_in_order = $considered;
            selected_provider_context = [ordered]@{ vc_tools = $env:VCToolsInstallDir;
                universal_crt_sdk = $env:UniversalCRTSdkDir; ucrt_version = $env:UCRTVersion };
            expected = $null; relevant_members = @(); narrow_archive_symbols = $null;
            member_inspections = [Collections.Generic.List[object]]::new();
            last_completed_stage = 'identity_acquired'; pending_stage = 'provider_root_validation';
            first_failure = $null; relation_completeness = 'unearned_inspection_pending' }
        $receipts.Add($record)
        $activeArchiveRecord = $record
        $expectedRoot = if ($name -like 'libucrt*') {
            Join-Path $env:UniversalCRTSdkDir ('Lib\' + $env:UCRTVersion + '\ucrt\x64')
        } else { Join-Path $env:VCToolsInstallDir 'lib\x64' }
        $record.expected = Join-Path $expectedRoot $name
        if ([IO.Path]::GetFullPath($first) -ne [IO.Path]::GetFullPath($record.expected)) {
            $record.status = 'first_resolution_outside_selected_provider_root'
            $record.pending_stage = $null
            $record.relation_completeness = 'unearned_provider_refusal'
            $failures.Add("FIRST resolved $name is outside its selected provider root; no substitute inspected")
            $activeArchiveRecord = $null
            continue
        }
        $record.last_completed_stage = 'provider_root_validated'
        $record.pending_stage = 'member_list'
        $membersText = Inspect $toolPaths['lib.exe'] @('/nologo','/list',$first)
        $members = @($membersText -split '\r?\n' | Where-Object {
            $_ -match '(?i)(^|[\\/])(malloc|free|malloc_base|free_base|new_handler|newhandler|new_mode|newmode|new_scalar|new_array|debug_heap)\.obj$'
        } | Select-Object -Unique)
        $record.relevant_members = $members
        $record.last_completed_stage = 'member_list_read'
        $record.pending_stage = 'archive_symbol_inspection'
        if ($members.Count -eq 0 -and $name -in @('libucrt.lib','libucrtd.lib','libcpmt.lib','libcpmtd.lib')) {
            $failures.Add("No narrow provider member discovered in $name; relation remains unearned")
        }
        $symbols = Inspect $toolPaths['dumpbin.exe'] @('/nologo','/linkermember:1',$first)
        $record.narrow_archive_symbols = RelevantLines $symbols 160
        $record.last_completed_stage = 'archive_symbols_read'
        $record.pending_stage = 'selected_member_inspection'
        $record.status = 'archive_symbols_acquired_member_inspection_pending'
        $archiveIncomplete = $members.Count -eq 0 -and $name -in @('libucrt.lib','libucrtd.lib','libcpmt.lib','libcpmtd.lib')
        foreach ($member in $members) {
            $null = RemainingMilliseconds
            if ($memberCount -ge $maximumMembers) {
                $archiveIncomplete = $true
                $failures.Add('Provider-member bound 24 reached; remaining relations unearned')
                break
            }
            $memberCount++
            # Read-only ORIGINAL archive: LIB extraction only materializes one selected member
            # in fresh private runner scratch, never in checkout/artifact output. No edits/relink.
            $objectPath = Join-Path $privateRoot ('member-' + $memberCount + '.obj')
            $null = Inspect $toolPaths['lib.exe'] @('/nologo',$first,('/extract:' + $member),('/out:' + $objectPath))
            if (-not (Test-Path -LiteralPath $objectPath -PathType Leaf)) { throw 'Selected provider member was not materialized' }
            $object = Get-Item -LiteralPath $objectPath
            if ($object.Length -gt 1048576) { throw 'Selected member exceeds 1 MiB private scratch bound' }
            $detail = Inspect $toolPaths['dumpbin.exe'] @('/nologo','/headers','/symbols','/relocations',$objectPath)
            $record.member_inspections.Add([ordered]@{ archive_member = $member;
                member_identity = Identity $objectPath; evidence = RelevantLines $detail 120;
                limitation = 'Symbol/relocation names do not prove legal original forwarding or allocator semantics' })
        }
        if ($record.narrow_archive_symbols.truncated) {
            $archiveIncomplete = $true
            $failures.Add("Narrow symbol retention bound reached in $name; full relation not retained")
        }
        foreach ($inspection in $record.member_inspections) {
            if ($inspection.evidence.truncated) { $archiveIncomplete = $true; $failures.Add("Member excerpt bound reached in $name; relation incomplete") }
        }
        $record.last_completed_stage = 'bounded_member_inspections_finished'
        $record.pending_stage = $null
        $record.status = if ($archiveIncomplete) { 'inspection_incomplete' } else { 'bounded_inspection_completed' }
        $record.relation_completeness = if ($archiveIncomplete) { 'unearned_incomplete' } else { 'initial identity/symbol evidence only; no forwarding proof' }
        $activeArchiveRecord = $null
    }
    # Exact candidate leaves under selected roots only; no recursive source/drive inventory.
    $sourceRoots = @(
        (Join-Path $env:VCToolsInstallDir 'crt\src\vcruntime'),
        (Join-Path $env:UniversalCRTSdkDir ('Source\' + $env:UCRTVersion + '\ucrt\heap')),
        (Join-Path $env:UniversalCRTSdkDir ('Source\' + $env:UCRTVersion + '\ucrt\misc')))
    $sourceNames = @('malloc.cpp','free.cpp','malloc_base.cpp','free_base.cpp','debug_heap.cpp',
        'new_handler.cpp','new_scalar.cpp','new_array.cpp','new_mode.cpp')
    foreach ($directory in $sourceRoots) {
        foreach ($name in $sourceNames) {
            $null = RemainingMilliseconds
            $path = Join-Path $directory $name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                $sourceReceipts.Add([ordered]@{ path = $path; status = 'not_present_at_selected_candidate' })
                continue
            }
            $identity = Identity $path
            $sourceRecord = [ordered]@{ identity = $identity; status = 'identity_acquired_inspection_pending';
                provenance = 'Selected VS/SDK installed directory, versions in selection.json; no archive-source build equivalence inferred';
                selected_source_root = $directory; candidate_leaf = $name;
                last_completed_stage = 'identity_acquired'; pending_stage = 'source_size_admission';
                first_failure = $null; relation_completeness = 'unearned_inspection_pending' }
            $sourceReceipts.Add($sourceRecord)
            $activeSourceRecord = $sourceRecord
            if ($identity.bytes -gt 1048576) { throw 'Selected source exceeds 1 MiB bounded read' }
            $null = RemainingMilliseconds
            $sourceRecord.pending_stage = 'source_read'
            $lines = [IO.File]::ReadAllLines($path)
            $sourceRecord.last_completed_stage = 'source_read'
            $sourceRecord['source_lines'] = $lines.Length
            $sourceRecord.pending_stage = 'coverage_and_excerpt'
            $null = RemainingMilliseconds
            $indexes = [Collections.Generic.SortedSet[int]]::new()
            $meaningful = [Collections.Generic.List[int]]::new()
            $inBlockComment = $false
            for ($i = 0; $i -lt $lines.Length; $i++) {
                $null = RemainingMilliseconds
                if ($lines[$i] -match '(?i)(copyright|malloc\(|free\(|new_handler|newmode|heapalloc|heapfree|return .*alloc|return .*free)') {
                    for ($j = [Math]::Max(0,$i - 1); $j -le [Math]::Min($lines.Length - 1,$i + 1); $j++) { $null = $indexes.Add($j) }
                }
                # Local conservative coverage test, not a source-language parser.
                # Strip ordinary whole-line/header comments so dropping blanks/copyright cannot
                # turn full meaningful-source disclosure into a supposedly partial excerpt.
                $trimmed = $lines[$i].Trim()
                if ($inBlockComment) {
                    $close = $trimmed.IndexOf('*/')
                    if ($close -lt 0) { continue }
                    $inBlockComment = $false; $trimmed = $trimmed.Substring($close + 2).Trim()
                }
                while ($trimmed.StartsWith('/*')) {
                    $close = $trimmed.IndexOf('*/',2)
                    if ($close -lt 0) { $inBlockComment = $true; $trimmed = ''; break }
                    $trimmed = $trimmed.Substring($close + 2).Trim()
                }
                if ($trimmed.Length -and -not $trimmed.StartsWith('//')) { $meaningful.Add($i) }
            }
            $retainedIndexes = @($indexes | Select-Object -First 24)
            $characterClipped = @($retainedIndexes | Where-Object { $lines[$_].Length -gt 200 }).Count
            $coveredMeaningful = @($meaningful | Where-Object { $_ -in $retainedIndexes }).Count
            $fullMeaningfulCoverage = $meaningful.Count -eq 0 -or $coveredMeaningful -eq $meaningful.Count
            $excerpt = @()
            if (-not $fullMeaningfulCoverage) {
                $excerpt = @($retainedIndexes | ForEach-Object {
                    [ordered]@{ line = $_ + 1; original_length = $lines[$_].Length;
                        character_truncated = $lines[$_].Length -gt 200;
                        text = $lines[$_].Substring(0,[Math]::Min(200,$lines[$_].Length)) }
                })
            }
            $lost = $indexes.Count -gt 24 -or $characterClipped -gt 0 -or $fullMeaningfulCoverage
            if ($lost) { $failures.Add("Source relation incomplete at excerpt/coverage boundary: $name") }
            $completedSourceFields = [ordered]@{ identity = $identity;
                status = if ($fullMeaningfulCoverage) { 'identity_only_full_meaningful_source_withheld' } else { 'installed_candidate_read' };
                provenance = 'Selected VS/SDK installed directory, versions in selection.json; no archive-source build equivalence inferred';
                source_lines = $lines.Length; meaningful_source_lines = $meaningful.Count;
                meaningful_lines_covered_by_candidate = $coveredMeaningful; full_meaningful_coverage = $fullMeaningfulCoverage;
                excerpt_line_bound = 24; matching_context_lines = $indexes.Count;
                candidate_retained_context_lines = $retainedIndexes.Count; retained_context_lines = $excerpt.Count;
                line_count_truncated = $indexes.Count -gt 24; character_truncated = $characterClipped -gt 0;
                character_clipped_lines = $characterClipped;
                original_candidate_line_lengths = @($retainedIndexes | ForEach-Object { $lines[$_].Length });
                text_omitted_reason = if ($fullMeaningfulCoverage) { 'candidate covers entire meaningful proprietary leaf; identity only' } else { $null };
                relation_completeness = if ($lost) { 'unearned_incomplete' } else { 'bounded initial excerpt only; no forwarding proof' };
                excerpt = $excerpt }
            foreach ($key in $completedSourceFields.Keys) { $sourceRecord[$key] = $completedSourceFields[$key] }
            $sourceRecord.last_completed_stage = 'bounded_coverage_and_excerpt_completed'
            $sourceRecord.pending_stage = 'post_excerpt_deadline_check'
            $null = RemainingMilliseconds
            $sourceRecord.pending_stage = $null
            $activeSourceRecord = $null
        }
    }
    foreach ($required in @('malloc_base.cpp', 'free_base.cpp', 'new_handler.cpp')) {
        if (-not @($sourceReceipts | Where-Object {
            $_.Contains('identity') -and [IO.Path]::GetFileName($_.identity.path) -eq $required
        }).Count) {
            $failures.Add("Missing selected installed source candidate $required; no forwarding relation substituted")
        }
    }
} catch {
    $firstCollectorFailure = $_.Exception.Message
    foreach ($partialRecord in @($activeArchiveRecord,$activeSourceRecord)) {
        if ($null -ne $partialRecord) {
            $partialRecord.status = 'identity_acquired_later_inspection_failed'
            $partialRecord.first_failure = $firstCollectorFailure
            $partialRecord.relation_completeness = 'unearned_incomplete'
        }
    }
    $failures.Add($firstCollectorFailure)
} finally {
    # Each bulk payload gets ONE attempt and a small omission receipt if rejected.
    # Failure/control writes are independent and still attempted after inspection expiry.
    SaveBulk 'selection.json' $selectionPayload 2
    SaveBulk 'archives.json' $receipts.ToArray() 1
    SaveBulk 'sources.json' $sourceReceipts.ToArray() 0
    $commandBytes = $null
    try {
        $commandBytes = EncodeJson $commands.ToArray()
        if ($commandBytes.Length -gt 49152) {
            $failures.Add('Full command detail omitted at reserved 48 KiB command portion')
            $fullCommandSize = $commandBytes.Length; $fullCommandHash = PayloadDigest $commandBytes
            $summary = @($commands | ForEach-Object {
                [ordered]@{ tool = [IO.Path]::GetFileName($_.tool); did_start = $_.did_start;
                    status = $_.status; terminal_reason = $_.terminal_reason; joined = $_.joined;
                    observed_exit_code = $_.observed_exit_code; raw_bytes = $_.raw_bytes;
                    first_error_original_length = if ($null -eq $_.first_error) { 0 } else { $_.first_error.Length };
                    first_error_prefix = if ($null -eq $_.first_error) { $null } else { $_.first_error.Substring(0,[Math]::Min(120,$_.first_error.Length)) } }
            })
            $commandBytes = EncodeJson ([ordered]@{ status = 'command_detail_omitted'; command_count = $commands.Count;
                original_utf8_bytes = $fullCommandSize; original_sha256 = $fullCommandHash;
                reason = '48 KiB reserved command portion'; completeness = 'unearned'; summary = $summary })
            if ($commandBytes.Length -gt 49152) {
                $commandBytes = EncodeJson ([ordered]@{ status = 'command_detail_and_per_command_summary_omitted';
                    command_count = $commands.Count; started_count = @($commands | Where-Object { $_.did_start }).Count;
                    joined_count = @($commands | Where-Object { $_.joined }).Count;
                    original_utf8_bytes = $fullCommandSize; original_sha256 = $fullCommandHash;
                    outcomes = @($commands | Group-Object status,terminal_reason | ForEach-Object {
                        [ordered]@{ outcome = $_.Name; count = $_.Count } }); completeness = 'unearned' })
            }
        }
        WriteReceipt 'commands.json' $commandBytes 49152
    } catch {
        $failures.Add("Command receipt persistence failed: $($_.Exception.Message)")
        Write-Warning 'Command receipt persistence failed; terminal result is independently attempted'
    }
    try {
        $result = [ordered]@{ status = if ($failures.Count) { 'prerequisite_incomplete' } else { 'initial_receipt_collected' };
            first_collector_exception = if ($null -eq $firstCollectorFailure) { $null } else {
                $firstCollectorFailure.Substring(0,[Math]::Min(600,$firstCollectorFailure.Length)) };
            failure_count = $failures.Count; retained_failure_limit = 48;
            failure_list_truncated = $failures.Count -gt 48;
            failures = @($failures | Select-Object -First 48 | ForEach-Object {
                [ordered]@{ original_length = $_.Length; character_truncated = $_.Length -gt 200;
                    message = $_.Substring(0,[Math]::Min(200,$_.Length)) } });
            command_count = $commands.Count; started_command_count = @($commands | Where-Object { $_.did_start }).Count;
            unconfirmed_starts = @($commands | Where-Object { $_.start_attempted -and -not $_.start_confirmed }).Count;
            unconfirmed_joins = @($commands | Where-Object { $_.did_start -and -not $_.joined }).Count;
            started_utc = $started.ToString('o'); finished_utc = [DateTime]::UtcNow.ToString('o');
            limits = [ordered]@{ artifact_bytes = $maximumOutputBytes; command_raw_bytes = $commandRawLimit;
                command_seconds = $commandSeconds; total_inspection_seconds = 480; selected_members = $maximumMembers;
                private_member_bytes_each_postcheck = 1048576; reserved_control_bytes = 65536;
                command_receipt_bytes = 49152; terminal_receipt_bytes = 16384 };
            time_scope = 'Inspection admission/supervision and cooperative file/hash/serialization boundaries; cleanup/control persistence separate; not OS-enforced hard wall-time or storage quotas';
            excluded = @('product configuration','compiler/link invocation','actual FFT/resource composition',
                'CRT hook','forwarding correctness','runtime calibration','/MD substitution','full proprietary payload upload');
            authority = 'Initial selected-installation prerequisite only; later real linked test-EXE/map/calibration requires separate grant' }
        $resultBytes = EncodeJson $result
        if ($resultBytes.Length -gt 16384) {
            $resultBytes = EncodeJson ([ordered]@{ status = 'prerequisite_incomplete'; reason = 'detailed terminal receipt omitted at reserved 16 KiB portion';
                omitted_utf8_bytes = $resultBytes.Length; omitted_sha256 = PayloadDigest $resultBytes;
                failure_count = $failures.Count; command_count = $commands.Count;
                first_failure_prefix = if ($failures.Count) { $failures[0].Substring(0,[Math]::Min(600,$failures[0].Length)) } else { $null };
                completeness = 'unearned' })
        }
        WriteReceipt 'result.json' $resultBytes 16384
    } catch { Write-Warning "Terminal receipt persistence failed: $($_.Exception.Message)"; $failures.Add('Terminal receipt could not be persisted') }
}
if ($failures.Count) { Write-Error 'Selected CRT prerequisite collection incomplete; inspect available bounded receipts' }
