# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# REVIEW TEXT ONLY. No authorization to run this collector accompanies this file.
# Targeted installed-file/header reads only; no LIB/DUMPBIN, compiler, link, test or hook.
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
$selectionPayload = $null
$firstCollectorFailure = $null
$activeArchiveRecord = $null
$activeSourceRecord = $null

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
function ReceiptBytesUsed {
    [long]$total = 0
    foreach ($file in Get-ChildItem -LiteralPath $root -File -ErrorAction Stop) {
        $total += [long]$file.Length
    }
    return $total
}
function WriteReceipt([string]$Name, [byte[]]$Bytes, [int]$Ceiling) {
    # Control writes are attempted even after inspection expiry; actual I/O is not interruptible.
    $used = ReceiptBytesUsed
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
        $used = ReceiptBytesUsed
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

# Reviewed action02 identity expectations; this targeted reader never recollects its broad census.
$action02Binding = ConvertFrom-Json -AsHashtable @'
{
  "selected": {
    "VSCMD_VER": "18.10.3",
    "VisualStudioVersion": "18.0",
    "VCToolsVersion": "14.51.36231",
    "VCToolsInstallDir": "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC\\14.51.36231\\",
    "VSINSTALLDIR": "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\",
    "WindowsSdkDir": "C:\\Program Files (x86)\\Windows Kits\\10\\",
    "WindowsSDKVersion": "10.0.26100.0\\",
    "WindowsSDKLibVersion": "10.0.26100.0\\",
    "UniversalCRTSdkDir": "C:\\Program Files (x86)\\Windows Kits\\10\\",
    "UCRTVersion": "10.0.26100.0",
    "VSCMD_ARG_HOST_ARCH": "x64",
    "VSCMD_ARG_TGT_ARCH": "x64"
  },
  "tools": {
    "cl.exe": {
      "path": "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC\\14.51.36231\\bin\\HostX64\\x64\\cl.exe",
      "bytes": 893288,
      "sha256": "b0a1818ebae6da7f18230a84e76ce839212584ac4c2ab55b97a0d5268085da57",
      "file_version": "19.51.36260.0",
      "product_version": "14.51.36260.0"
    },
    "link.exe": {
      "path": "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC\\14.51.36231\\bin\\HostX64\\x64\\link.exe",
      "bytes": 3701088,
      "sha256": "a8c42e25c91ca2453e1110540ac08f0fc12d7539454a11b9af8baeecb469837d",
      "file_version": "14.51.36260.0",
      "product_version": "14.51.36260.0"
    },
    "lib.exe": {
      "path": "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC\\14.51.36231\\bin\\HostX64\\x64\\lib.exe",
      "bytes": 23384,
      "sha256": "d8dae02a50c7571c6c0ea5cd7a853830aaca8fad13f8f4be147828d51dd4adec",
      "file_version": "14.51.36260.0",
      "product_version": "14.51.36260.0"
    },
    "dumpbin.exe": {
      "path": "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Tools\\MSVC\\14.51.36231\\bin\\HostX64\\x64\\dumpbin.exe",
      "bytes": 23376,
      "sha256": "357c989e1c926841df4e6e97c25a60b205b30110cd6db39bde8d81a627e48ebb",
      "file_version": "14.51.36260.0",
      "product_version": "14.51.36260.0"
    }
  },
  "providers": [
    {
      "path": "C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.26100.0\\ucrt\\x64\\libucrt.lib",
      "bytes": 43342854,
      "sha256": "ea2ffa5d9b1ebc3fa76f34c364471750db8951b341dbccbc1bef628ef1a20e39",
      "file_version": null,
      "product_version": null
    },
    {
      "path": "C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.26100.0\\ucrt\\x64\\libucrtd.lib",
      "bytes": 44189856,
      "sha256": "082a3e21ff32beeee86864da52fcaf861b80b09d80b4f77fdc0a39ff549ad0cb",
      "file_version": null,
      "product_version": null
    }
  ],
  "sources": {
    "malloc_base.cpp": {
      "path": "C:\\Program Files (x86)\\Windows Kits\\10\\Source\\10.0.26100.0\\ucrt\\heap\\malloc_base.cpp",
      "bytes": 1635,
      "sha256": "5e8799cca0bf2bb47e14f77f11fc1275db51d27c34c013ff1d929e2f1793d62b",
      "file_version": null,
      "product_version": null
    },
    "free_base.cpp": {
      "path": "C:\\Program Files (x86)\\Windows Kits\\10\\Source\\10.0.26100.0\\ucrt\\heap\\free_base.cpp",
      "bytes": 3656,
      "sha256": "fdd31fc5aa5d34dec658d7b293f559ad623f978c777b17f214ca60cfcdd54fb6",
      "file_version": null,
      "product_version": null
    },
    "free.cpp": {
      "path": "C:\\Program Files (x86)\\Windows Kits\\10\\Source\\10.0.26100.0\\ucrt\\heap\\free.cpp",
      "bytes": 1723,
      "sha256": "728fb1d774506edb5b8b69ae21af85809585e31b72890edcb79b25877ee44b48",
      "file_version": null,
      "product_version": null
    }
  },
  "prior_failures": [
    {
      "original_length": 80,
      "character_truncated": false,
      "message": "Narrow symbol retention bound reached in libucrt.lib; full relation not retained"
    },
    {
      "original_length": 64,
      "character_truncated": false,
      "message": "Member excerpt bound reached in libucrt.lib; relation incomplete"
    },
    {
      "original_length": 81,
      "character_truncated": false,
      "message": "Narrow symbol retention bound reached in libucrtd.lib; full relation not retained"
    },
    {
      "original_length": 65,
      "character_truncated": false,
      "message": "Member excerpt bound reached in libucrtd.lib; relation incomplete"
    },
    {
      "original_length": 65,
      "character_truncated": false,
      "message": "Member excerpt bound reached in libucrtd.lib; relation incomplete"
    },
    {
      "original_length": 78,
      "character_truncated": false,
      "message": "No narrow provider member discovered in libcpmt.lib; relation remains unearned"
    },
    {
      "original_length": 79,
      "character_truncated": false,
      "message": "No narrow provider member discovered in libcpmtd.lib; relation remains unearned"
    },
    {
      "original_length": 71,
      "character_truncated": false,
      "message": "Source relation incomplete at excerpt/coverage boundary: debug_heap.cpp"
    },
    {
      "original_length": 72,
      "character_truncated": false,
      "message": "Source relation incomplete at excerpt/coverage boundary: new_handler.cpp"
    }
  ]
}
'@
try {
    if (-not [OperatingSystem]::IsWindows() -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
        throw 'One native x64 Windows host is required'
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $null = RemainingMilliseconds
    $installation = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'A current Visual Studio C++ toolchain is required' }
    $devshell = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
    $null = RemainingMilliseconds
    & $devshell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $null = RemainingMilliseconds
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC developer environment was not activated' }
    $versions = [ordered]@{}; $tools = [ordered]@{}; $search = [ordered]@{}
    $selectionPayload = [ordered]@{ scope = 'targeted helper declarations/configuration and free.cpp expressions only';
        installation = $installation; selected = $versions; tools = $tools; ordered_search = $search;
        workflow_commit = $env:GITHUB_SHA; runner_os = $env:RUNNER_OS; runner_arch = $env:RUNNER_ARCH;
        action02_head = '9e42d253c7b4d0e9a1bf831bf055c45a60b16800';
        action02_failed_status_preserved = 'prerequisite_incomplete'; prior_nine_failures_unchanged = $action02Binding.prior_failures;
        interpretation = 'Identity rebind only; no native inspection command, compilation, link or forwarding proof' }
    foreach ($key in $action02Binding.selected.Keys) {
        $actual = [Environment]::GetEnvironmentVariable($key)
        $versions[$key] = $actual
        if ($actual -cne $action02Binding.selected[$key]) { throw "Selected context drift from action02: $key" }
    }
    foreach ($name in @('cl.exe','link.exe','lib.exe','dumpbin.exe')) {
        $null = RemainingMilliseconds
        $selected = Get-Command $name -CommandType Application -ErrorAction Stop | Select-Object -First 1
        $tools[$name] = Identity $selected.Source
        $expected = $action02Binding.tools[$name]
        if ($tools[$name].path -ine $expected.path -or $tools[$name].bytes -ne $expected.bytes -or $tools[$name].sha256 -cne $expected.sha256) {
            throw "Selected tool identity drift from action02: $name"
        }
    }
    foreach ($key in @('PATH','LIB','LIBPATH','INCLUDE')) {
        $search[$key] = @([Environment]::GetEnvironmentVariable($key) -split ';' | Where-Object { $_ })
    }
    foreach ($expected in $action02Binding.providers) {
        $null = RemainingMilliseconds
        $name = [IO.Path]::GetFileName($expected.path)
        $first = $null; $considered = [Collections.Generic.List[string]]::new()
        foreach ($directory in $search.LIB) {
            $candidate = Join-Path $directory $name; $considered.Add($candidate)
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $first = $candidate; break }
        }
        if ($null -eq $first) { throw "Missing selected action02 provider $name" }
        $identity = Identity $first
        $record = [ordered]@{ archive = $identity; searched_in_order = $considered;
            status = 'identity_acquired_comparison_pending'; pending_stage = 'compare_action02_identity';
            first_failure = $null; relation_completeness = 'no symbol/member recollection in targeted supplement' }
        $receipts.Add($record); $activeArchiveRecord = $record
        if ($identity.path -ine $expected.path -or $identity.bytes -ne $expected.bytes -or $identity.sha256 -cne $expected.sha256) {
            throw "Selected provider identity drift from action02: $name"
        }
        $record.status = 'identity_equal_action02'; $record.pending_stage = $null; $activeArchiveRecord = $null
    }
    $sdkInclude = Join-Path $env:UniversalCRTSdkDir ('Include\' + $env:UCRTVersion + '\ucrt')
    $sdkSource = Join-Path $env:UniversalCRTSdkDir ('Source\' + $env:UCRTVersion + '\ucrt')
    $vcInclude = Join-Path $env:VCToolsInstallDir 'include'
    $targets = @(
        @{ root = $sdkInclude; leaf = 'corecrt_malloc.h'; role = 'ordinary SDK include candidate'; symbols = @('_malloc_base','_free_base'); before = 3; after = 6 },
        @{ root = $sdkInclude; leaf = 'malloc.h'; role = 'ordinary SDK include candidate'; symbols = @('_malloc_base','_free_base'); before = 3; after = 6 },
        @{ root = $sdkInclude; leaf = 'crtdbg.h'; role = 'ordinary SDK include candidate'; symbols = @('_malloc_dbg','_free_dbg'); before = 3; after = 6 },
        @{ root = $sdkInclude; leaf = 'corecrt.h'; role = 'SDK exception/import macro candidate'; symbols = @('_CRT_NOEXCEPT','_ACRTIMP'); before = 2; after = 2 },
        @{ root = $vcInclude; leaf = 'vcruntime.h'; role = 'VC exception/import macro candidate'; symbols = @('_CRT_NOEXCEPT','_VCRTIMP'); before = 2; after = 2 },
        @{ root = (Join-Path $sdkSource 'inc'); leaf = 'corecrt_internal.h'; role = 'INTERNAL header candidate, not presumed project-usable'; symbols = @('_malloc_base','_free_base'); before = 3; after = 6 },
        @{ root = (Join-Path $sdkSource 'inc'); leaf = 'corecrt_internal_debug_heap.h'; role = 'INTERNAL header candidate, not presumed project-usable'; symbols = @('_malloc_dbg','_free_dbg'); before = 3; after = 6 },
        @{ root = (Join-Path $sdkSource 'heap'); leaf = 'malloc_base.cpp'; role = 'actual Release helper definition candidate'; symbols = @('_malloc_base'); before = 2; after = 4 },
        @{ root = (Join-Path $sdkSource 'heap'); leaf = 'free_base.cpp'; role = 'actual Release helper definition candidate'; symbols = @('_free_base'); before = 2; after = 4 },
        @{ root = (Join-Path $sdkSource 'heap'); leaf = 'free.cpp'; role = 'actual public free Debug/Release call expressions'; symbols = @('_free_dbg','_free_base'); before = 2; after = 2 }
    )
    foreach ($target in $targets) {
        $null = RemainingMilliseconds
        $path = Join-Path $target.root $target.leaf
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $sourceReceipts.Add([ordered]@{ path = $path; role = $target.role; status = 'missing_exact_candidate'; selector_symbols = $target.symbols })
            continue
        }
        $identity = Identity $path
        $sourceRecord = [ordered]@{ identity = $identity; role = $target.role; selector_symbols = $target.symbols;
            status = 'identity_acquired_read_pending'; last_completed_stage = 'identity_acquired';
            pending_stage = 'identity_and_size_admission'; first_failure = $null;
            relation_completeness = 'unearned pending bounded quoted declarations/guards' }
        $sourceReceipts.Add($sourceRecord); $activeSourceRecord = $sourceRecord
        if ($action02Binding.sources.ContainsKey($target.leaf)) {
            $expected = $action02Binding.sources[$target.leaf]
            if ($identity.path -ine $expected.path -or $identity.bytes -ne $expected.bytes -or $identity.sha256 -cne $expected.sha256) {
                throw "Selected source identity drift from action02: $($target.leaf)"
            }
        }
        if ($identity.bytes -gt 1048576) { throw 'Selected source exceeds 1 MiB bounded read' }
        $null = RemainingMilliseconds; $sourceRecord.pending_stage = 'source_read_and_narrow_selection'
        $lines = [IO.File]::ReadAllLines($path); $null = RemainingMilliseconds
        $indexes = [Collections.Generic.SortedSet[int]]::new()
        $anchorLines = [Collections.Generic.List[object]]::new()
        $requiredRole = $target.role -like 'actual*' -or $target.leaf -ceq 'crtdbg.h'
        $macroRole = $target.role -like '*macro candidate'
        $callRole = $target.leaf -ceq 'free.cpp'
        # Classify only leading comment fragments using the already retained conservative rule.
        # This lexical selection is not a C++ or preprocessor parser.
        $codeLines = [string[]]::new($lines.Length)
        $candidateInBlockComment = $false
        for ($i = 0; $i -lt $lines.Length; $i++) {
            $null = RemainingMilliseconds
            $candidateText = $lines[$i].Trim()
            if ($candidateInBlockComment) {
                $close = $candidateText.IndexOf('*/')
                if ($close -lt 0) { $codeLines[$i] = ''; continue }
                $candidateInBlockComment = $false
                $candidateText = $candidateText.Substring($close + 2).Trim()
            }
            while ($candidateText.StartsWith('/*')) {
                $close = $candidateText.IndexOf('*/',2)
                if ($close -lt 0) { $candidateInBlockComment = $true; $candidateText = ''; break }
                $candidateText = $candidateText.Substring($close + 2).Trim()
            }
            $lineComment = $candidateText.IndexOf('//')
            if ($lineComment -ge 0) { $candidateText = $candidateText.Substring(0,$lineComment).TrimEnd() }
            # Conservatively recognize remaining inline block openers too, so an unstarred
            # subsequent comment line cannot become a declaration candidate.
            while (($inlineOpen = $candidateText.IndexOf('/*')) -ge 0) {
                $inlineClose = $candidateText.IndexOf('*/',$inlineOpen + 2)
                if ($inlineClose -lt 0) {
                    $candidateInBlockComment = $true
                    $candidateText = $candidateText.Substring(0,$inlineOpen).TrimEnd(); break
                }
                $candidateText = $candidateText.Substring(0,$inlineOpen) + ' ' + $candidateText.Substring($inlineClose + 2)
            }
            $codeLines[$i] = $candidateText
        }
        for ($i = 0; $i -lt $lines.Length; $i++) {
            $null = RemainingMilliseconds
            $candidateText = $codeLines[$i]
            if (-not $candidateText.Length) { continue }
            foreach ($symbol in $target.symbols) {
                $escaped = [Regex]::Escape($symbol)
                $kind = $null; $spanStart = $i; $spanEnd = $i; $closed = $false
                if ($macroRole) {
                    if ($candidateText -notmatch ('^#\s*define\s+' + $escaped + '(?=\s|\(|$)')) { continue }
                    $kind = 'named_macro_definition'
                    $last = [Math]::Min($lines.Length - 1,$i + $target.after)
                    for ($j = $i; $j -le $last; $j++) {
                        $spanEnd = $j
                        if (-not $codeLines[$j].TrimEnd().EndsWith('\')) { $closed = $true; break }
                    }
                } else {
                    if ($candidateText.StartsWith('#')) { continue }
                    $match = [Regex]::Match($candidateText, ('(?<![A-Za-z0-9_])' + $escaped + '\s*\('))
                    if (-not $match.Success) { continue }
                    if ($callRole) {
                        $kind = 'public_free_helper_call_expression'
                    } else {
                        # Reject obvious expressions; require concrete type AND __cdecl prefix.
                        if ($candidateText -match '^(return|co_return|throw|if|while|for|switch)\b') { continue }
                        $prefix = $candidateText.Substring(0,$match.Index)
                        if ($prefix -match '(?<![=!<>])=(?!=)' -or $prefix -match '[;{}]') { continue }
                        for ($j = $i - 1; $j -ge [Math]::Max(0,$i - $target.before); $j--) {
                            if ($prefix -match '\b__cdecl\b' -and $prefix -match '\b(void|size_t|char|unsigned|int)\b') { break }
                            $previous = $codeLines[$j]
                            if ($previous.StartsWith('#') -or $previous -match '[;{}=]') { break }
                            $prefix = $previous + ' ' + $prefix; $spanStart = $j
                        }
                        if ($prefix -notmatch '\b__cdecl\b' -or $prefix -notmatch '\b(void|size_t|char|unsigned|int)\b') { continue }
                        $kind = 'typed_cdecl_declaration_or_definition_candidate'
                    }
                    $last = [Math]::Min($lines.Length - 1,$i + $target.after)
                    $tail = ''
                    for ($j = $i; $j -le $last; $j++) {
                        $spanEnd = $j; $tail += ' ' + $codeLines[$j]
                        # A visible close plus actual declaration/body/call terminator is required.
                        if ($tail.Contains(')') -and ($tail.Contains(';') -or (-not $callRole -and $tail.Contains('{')))) {
                            $closed = $true; break
                        }
                    }
                }
                $contextStart = [Math]::Max(0,$i - $target.before)
                $contextEnd = [Math]::Min($lines.Length - 1,$i + $target.after)
                for ($j = $contextStart; $j -le $contextEnd; $j++) { $null = $indexes.Add($j) }
                $anchorLines.Add([ordered]@{ symbol = $symbol; original_line = $i + 1; candidate_kind = $kind;
                    required = $requiredRole; local_context_first_line = $contextStart + 1; local_context_last_line = $contextEnd + 1;
                    typed_prefix_first_line = $spanStart + 1;
                    necessary_span_first_line = $contextStart + 1; necessary_span_last_line = $spanEnd + 1;
                    necessary_span_closed_in_local_context = $closed; required_span_retained = $false })
            }
        }
            $meaningful = [Collections.Generic.List[int]]::new()
            for ($i = 0; $i -lt $codeLines.Length; $i++) {
                $null = RemainingMilliseconds
                if ($codeLines[$i].Trim().Length) { $meaningful.Add($i) }
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
            if ($lost) { $failures.Add("Source relation incomplete at excerpt/coverage boundary: $($target.leaf)") }
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

        foreach ($candidate in $anchorLines) {
            $spanRetained = $candidate.necessary_span_closed_in_local_context -and -not $fullMeaningfulCoverage
            for ($j = $candidate.necessary_span_first_line - 1; $j -lt $candidate.necessary_span_last_line; $j++) {
                if ($j -notin $retainedIndexes -or $lines[$j].Length -gt 200) { $spanRetained = $false }
            }
            $candidate.required_span_retained = $spanRetained
            if ($candidate.required -and -not $spanRetained) {
                $failures.Add("Required candidate/span not retained: $($target.leaf):$($candidate.original_line) $($candidate.symbol)")
            }
        }
        $sourceRecord['candidates'] = $anchorLines.ToArray()
        $sourceRecord['original_line_numbers'] = 'candidate/excerpt/span fields are original one-based positions'
        $sourceRecord['configuration_scope'] = 'Bounded local context ONLY; enclosing conditions/macros not visible remain unearned'
        $sourceRecord['project_header_status'] = $target.role
        $missing = @($target.symbols | Where-Object { $symbol = $_; -not @($anchorLines | Where-Object { $_.symbol -ceq $symbol }).Count })
        $sourceRecord['missing_candidate_symbols'] = $missing
        $sourceRecord['required_role'] = $requiredRole
        $sourceRecord['required_retained_span_incomplete'] = $requiredRole -and ($missing.Count -gt 0 -or @($anchorLines | Where-Object { -not $_.required_span_retained }).Count -gt 0)
        if ($sourceRecord.required_retained_span_incomplete) {
            $sourceRecord.relation_completeness = 'unearned_incomplete_required_candidate_or_retained_span'
            if ($missing.Count) { $failures.Add("Required declaration/call candidate absent: $($target.leaf)") }
        }
        $sourceRecord.pending_stage = $null; $activeSourceRecord = $null
    }
    foreach ($leaf in @('malloc_base.cpp','free_base.cpp','free.cpp','crtdbg.h')) {
        if (-not @($sourceReceipts | Where-Object { $_.Contains('identity') -and [IO.Path]::GetFileName($_.identity.path) -ceq $leaf }).Count) {
            $failures.Add("Required targeted source/header candidate missing: $leaf")
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
        $result = [ordered]@{ status = if ($failures.Count) { 'prerequisite_incomplete' } else { 'targeted_source_receipt_collected' };
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
            scope = 'Targeted declaration/signature/configuration and free.cpp expression supplement ONLY';
            prior_action02_nine_failures_unchanged = $action02Binding.prior_failures;
            prior_action02_status = 'prerequisite_incomplete';
            native_inspection_commands_planned = 0; selected_text_semantic_review_required = $true;
            source_ABI_completion_claimed = $false;
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
    } catch {
        Write-Warning "Terminal receipt persistence failed: $($_.Exception.Message)"
        $firstExceptionPrefix = if ($null -eq $firstCollectorFailure) { 'unavailable/not-recorded' } else {
            $firstCollectorFailure.Substring(0,[Math]::Min(600,$firstCollectorFailure.Length)) }
        Write-Warning ("First collector exception (bounded prefix): " + $firstExceptionPrefix)
        $failures.Add('Terminal receipt could not be persisted')
    }
}
if ($failures.Count) { Write-Error 'Selected CRT prerequisite collection incomplete; inspect available bounded receipts' }
