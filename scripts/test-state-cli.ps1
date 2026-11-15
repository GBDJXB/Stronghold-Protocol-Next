[CmdletBinding()]
param([string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$exe = Join-Path $RepositoryRoot 'build/x64/Release/stronghold-capture.exe'
$root = Join-Path ([IO.Path]::GetTempPath()) ('state-cli-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $record = Join-Path $root 'state.json'
    $timestamp = '2026-11-15T00:00:00.000Z'
    $metadata = @{schema_version=2; frame_id='synthetic-frame'; request_start=$timestamp; request_end=$timestamp; raw=@{path='raw.png'}; source_frame=@{clock='qpc';unit='100ns';time=$null};window=@{hwnd=123;pid=456;process_created=$null}} | ConvertTo-Json -Depth 6 -Compress
    $unknown = @{status='unknown';value=$null;confidence=0;reason='unsupported_dimensions';raw_text='';model_sha256='';dictionary_sha256=''}
    $state = @{schema_version=2; id='synthetic-state'; saved_at=$timestamp; observation_status='historical'; source=@{frame_id='synthetic-frame';directory=$root;request_start=$timestamp;request_end=$timestamp;metadata_json=$metadata};category='unknown';special_view='';reason='unsupported_dimensions';recognizer_version='synthetic';runtime_version='';model_sha256='';dictionary_sha256='';funds=$unknown;stage=$unknown;remaining_time=$unknown;remaining_time_unit='seconds';evidence=@()}
    foreach ($case in @('missing','corrupt','valid')) {
        if($case -eq 'corrupt') { Set-Content -LiteralPath $record -Value '{' -Encoding utf8 }
        if($case -eq 'valid') { $state | ConvertTo-Json -Depth 8 -Compress | Set-Content -LiteralPath $record -Encoding utf8 }
        $output = & $exe inspect-state --record $record
        $exitCode = $LASTEXITCODE
        $result = ($output -join "`n") | ConvertFrom-Json
        if($case -eq 'valid') {
            if($exitCode -ne 0 -or -not $result.ok -or $result.state.observation_status -ne 'historical') { throw 'Valid state inspection failed' }
        } else {
            $expected = if($case -eq 'missing') {'state_missing'} else {'state_invalid'}
            if($exitCode -ne 2 -or $result.ok -ne $false -or $result.error.code -ne $expected) { throw "Invalid $case state error response" }
        }
    }
    Write-Output 'state CLI: missing/corrupt/valid single JSON responses passed'
} finally { Remove-Item -LiteralPath $root -Recurse -Force }
