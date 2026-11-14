[CmdletBinding()]
param([string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$exe = Join-Path $RepositoryRoot 'build\x64\Release\stronghold-capture.exe'
$output = & $exe --self-test
if ($LASTEXITCODE -ne 0) { throw "Self-test failed: $output" }
$result = $output | ConvertFrom-Json
if (-not $result.ok) { throw "Invalid self-test result: $output" }
$metadataFiles = @(Get-ChildItem -LiteralPath $result.artifacts -Filter metadata.json -Recurse -File)
if ($metadataFiles.Count -ne 10) { throw "Expected 10 synthetic archives, got $($metadataFiles.Count)" }
foreach ($metadataFile in $metadataFiles) {
    $metadataText = Get-Content -LiteralPath $metadataFile.FullName -Raw -Encoding utf8
    $meta = $metadataText | ConvertFrom-Json
    if ($meta.schema_version -ne 2 -or $meta.frame_id -ne $metadataFile.Directory.Name) { throw 'Broken archive association' }
    if ($meta.backend.name -ne 'windows-graphics-capture' -or $meta.backend.implementation -ne 'stronghold-wgc-v1' -or
        $meta.PSObject.Properties.Name -contains 'framework_version') { throw 'Invalid backend metadata' }
    if ($meta.source_frame.clock -ne 'qpc' -or $meta.source_frame.unit -ne '100ns' -or
        ($null -ne $meta.source_frame.time -and $meta.source_frame.time -notmatch '^\d+$')) { throw 'Invalid source frame timestamp' }
    if ($meta.frame_freshness -notin @('fresh', 'stale', 'unknown') -or $meta.raw.format -ne 'RGB8') { throw 'Invalid frame semantics' }
    if ($null -eq $meta.frame_age_ms_at_archive_check) {
        if ($meta.frame_freshness -ne 'unknown') { throw 'Missing age cannot be marked fresh or stale' }
    } elseif ($meta.frame_age_ms_at_archive_check -lt 0 -or
        (($meta.frame_age_ms_at_archive_check -le 1000) -ne ($meta.frame_freshness -eq 'fresh'))) {
        throw 'Frame age and freshness disagree'
    }
    if ($meta.window.client.width -ne $meta.raw.width -or $meta.window.client.height -ne $meta.raw.height) { throw 'Source/image size mismatch' }
    if (-not ($meta.window.identity_verified -is [bool]) -or
        -not ($meta.window.cloaked -is [bool]) -or
        $meta.window.opacity -isnot [string] -or $meta.window.visibility -isnot [string]) { throw 'Missing window visibility/identity metadata' }
    if ($meta.window.PSObject.Properties.Name -notcontains 'occluding_hwnd' -or
        $meta.window.PSObject.Properties.Name -notcontains 'identity_warning') { throw 'Missing window diagnostic metadata' }
    if ($meta.performance.encoding_ms -lt 0 -or $meta.performance.archive_elapsed_ms -lt 0 -or
        $meta.performance.png_bytes -ne (Get-Item -LiteralPath (Join-Path $metadataFile.DirectoryName 'raw.png')).Length) {
        throw 'Invalid archive performance metadata'
    }
    # New PowerShell versions may automatically convert ISO strings to DateTime objects.
    if ($metadataText -notmatch '"request_start"\s*:\s*"[0-9T:.\-]+Z"' -or
        $metadataText -notmatch '"request_end"\s*:\s*"[0-9T:.\-]+Z"') { throw 'Missing UTC request timestamps' }
}
Write-Output "Passed $($result.checks) native checks; independently parsed $($metadataFiles.Count) synthetic archives."
Write-Output "Synthetic artifacts: $($result.artifacts)"
