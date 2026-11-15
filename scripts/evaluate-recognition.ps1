[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DatasetDirectory,
    [Parameter(Mandatory)][string]$MaaRoot,
    [Parameter(Mandatory)][string]$Output,
    [ValidateSet('development', 'validation')][string]$Split = 'development',
    [string]$Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\x64\Release\stronghold-capture.exe')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$DatasetDirectory = (Resolve-Path -LiteralPath $DatasetDirectory).Path
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$MaaRoot = (Resolve-Path -LiteralPath $MaaRoot).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw "Report already exists: $outputPath" }

function Read-Document([string]$Name) {
    Get-Content -LiteralPath (Join-Path $DatasetDirectory $Name) -Raw | ConvertFrom-Json
}
function Require-Property($Record, [string]$Name) {
    if ($null -eq $Record.PSObject.Properties[$Name]) { throw "Missing dataset property: $Name" }
}
function External-Records($Document) {
    if ($null -ne $Document.PSObject.Properties['external_samples']) { $Document.external_samples }
}
function Check-Record($Record) {
    foreach ($name in @('sample_id', 'split', 'candidate_category', 'expected_classifier_output',
            'classifier_mapping_status', 'sample_usage', 'classifier_evaluation_reason',
            'use_for_model_development', 'use_for_classifier_evaluation')) {
        Require-Property $Record $name
    }
    foreach ($name in @('use_for_model_development', 'use_for_classifier_evaluation')) {
        if ($Record.$name -isnot [bool]) { throw "Non-boolean dataset property: $name" }
    }
    if ([string]::IsNullOrWhiteSpace($Record.sample_id) -or
        [string]::IsNullOrWhiteSpace($Record.classifier_evaluation_reason)) { throw 'Empty dataset identity or eligibility reason.' }
    if ($Record.split -notin @('development', 'independent_validation') -or
        ($Record.use_for_model_development -and $Record.split -cne 'development') -or
        ($Record.use_for_classifier_evaluation -and $Record.split -cne 'independent_validation')) {
        throw "Contradictory development eligibility: $($Record.sample_id)"
    }
    if ($null -ne $Record.PSObject.Properties['annotation_status']) {
        if ($Record.annotation_status -ceq 'pending_visual_review') {
            if ($null -ne $Record.candidate_category -or $null -ne $Record.expected_classifier_output -or
                $Record.use_for_model_development -or $Record.use_for_classifier_evaluation) {
                throw "Unreviewed sample has labels or eligibility: $($Record.sample_id)"
            }
        } elseif ($Record.annotation_status -cnotin @(
            'visually_reviewed',
            # Legacy manifest used these explicit prose statuses; curated projections below remain strict.
            'confirmed_by_user_after_original_image_review', 'confirmed_by_user_and_visual_review',
            'known_special_view_user_behavior_requirement_recorded',
            'loading_indicator_supported_by_user_reference_and_visual_review',
            'supported_by_visual_review', 'supported_by_visual_review_and_user_context',
            'user_explicitly_confirmed_indicator_and_visually_reviewed',
            'visually_read_user_confirmed_field_semantics',
            'visually_reviewed_field_semantics_confirmed_by_user', 'visually_reviewed_user_confirmed_page')) {
            throw "Unrecognized annotation status: $($Record.sample_id)"
        }
    }
    if ($Record.use_for_model_development -and $Record.use_for_classifier_evaluation) {
        throw "Development sample cannot be independent evaluation: $($Record.sample_id)"
    }
}
$manifest = Read-Document 'manifest.json'
$development = Read-Document 'development-index.json'
$validation = Read-Document 'validation-index.json'
if ($development.split -cne 'development' -or $validation.split -cne 'independent_validation') {
    throw 'Unrecognized dataset index split.'
}
$records = @($manifest.samples) + @(External-Records $manifest)
$byId = @{}
$frameIds = @{}
foreach ($record in $records) {
    Check-Record $record
    if ($byId.ContainsKey($record.sample_id)) { throw "Duplicate sample_id: $($record.sample_id)" }
    $byId[$record.sample_id] = $record
}
foreach ($record in $manifest.samples) {
    foreach ($name in @('frame_id', 'archive_directory', 'scene_group')) { Require-Property $record $name }
    if ([string]::IsNullOrWhiteSpace($record.frame_id) -or $frameIds.ContainsKey($record.frame_id)) {
        throw "Missing or duplicate frame_id: $($record.sample_id)"
    }
    $frameIds[$record.frame_id] = $true
}
$indexed = @{}
foreach ($index in @($development, $validation)) {
    foreach ($record in (@($index.samples) + @(External-Records $index))) {
        Check-Record $record
        if ($record.split -cne $index.split) { throw "Sample stored in wrong split index: $($record.sample_id)" }
        if ($indexed.ContainsKey($record.sample_id) -or -not $byId.ContainsKey($record.sample_id)) {
            throw "Duplicate or unrecognized index sample: $($record.sample_id)"
        }
        # Indices are documented complete projections of manifest records, not override files.
        $expected = $byId[$record.sample_id] | ConvertTo-Json -Depth 80 -Compress
        $actual = $record | ConvertTo-Json -Depth 80 -Compress
        if ($expected -cne $actual) { throw "Index disagrees with manifest: $($record.sample_id)" }
        $indexed[$record.sample_id] = $true
    }
}
if ($indexed.Count -ne $byId.Count) { throw 'Indices do not cover every manifest record.' }
$selected = if ($Split -eq 'development') { $development } else { $validation }
# Legacy development uses only explicit development eligibility. Legacy validation
# retains labeled diagnostic rows; only evaluation=true rows enter independent counts.
# Curated datasets replace this selection with their exact reviewed eligible projection.
$selectionPolicy = 'legacy-development-eligible-validation-labeled-diagnostics'
$selectedRecords = @($selected.samples | Where-Object {
    $null -ne $_.candidate_category -and ($Split -ceq 'validation' -or $_.use_for_model_development)
})
$curatedNames = @('curated-development-index.json', 'curated-validation-index.json')
$curatedCount = @($curatedNames | Where-Object { Test-Path -LiteralPath (Join-Path $DatasetDirectory $_) }).Count
if ($curatedCount -eq 1) { throw 'Both curated indices are required.' }
if ($curatedCount -eq 2) {
    $selectionPolicy = 'curated-reviewed-eligible-only'
    foreach ($name in $curatedNames) {
        $curated = Read-Document $name
        $expectedSplit = if ($name -ceq $curatedNames[0]) { 'development' } else { 'independent_validation' }
        if ($curated.split -cne $expectedSplit) { throw 'Wrong curated split.' }
        $curatedIds = @{}
        foreach ($record in $curated.samples) {
            if ($curatedIds.ContainsKey($record.sample_id) -or -not $byId.ContainsKey($record.sample_id)) {
                throw "Duplicate or unrecognized curated sample: $($record.sample_id)"
            }
            $expected = $byId[$record.sample_id] | ConvertTo-Json -Depth 80 -Compress
            $actual = $record | ConvertTo-Json -Depth 80 -Compress
            if ($expected -cne $actual -or $record.split -cne $expectedSplit -or
                $record.annotation_status -cne 'visually_reviewed' -or $null -eq $record.candidate_category) {
                throw "Curated sample is not a reviewed manifest projection: $($record.sample_id)"
            }
            if ($expectedSplit -ceq 'development' -and -not $record.use_for_model_development) {
                throw "Curated development sample is ineligible: $($record.sample_id)"
            }
            if ($expectedSplit -ceq 'independent_validation' -and -not $record.use_for_classifier_evaluation) {
                throw "Curated validation sample is ineligible: $($record.sample_id)"
            }
            $curatedIds[$record.sample_id] = $true
        }
        $eligibleRecords = @($manifest.samples | Where-Object {
            $_.split -ceq $expectedSplit -and $_.annotation_status -ceq 'visually_reviewed' -and
            ($_.use_for_model_development -or $_.use_for_classifier_evaluation)
        })
        if ($curatedIds.Count -ne $eligibleRecords.Count) { throw 'Curated index omits eligible reviewed samples.' }
        if ($selected.split -ceq $expectedSplit) { $selectedRecords = @($curated.samples) }
    }
}
$rows = foreach ($record in $selectedRecords) {
    $text = & $Executable analyze-archive --archive $record.archive_directory --maa-root $MaaRoot
    $exitCode = $LASTEXITCODE
    $analysis = ($text -join "`n") | ConvertFrom-Json
    if ($exitCode -ne 0 -or -not $analysis.ok) { throw "Analysis failed for $($record.sample_id): $($text -join ' ')" }
    if ($analysis.frame_id -cne $record.frame_id) { throw "Analysis source mismatch: $($record.sample_id)" }
    if ($analysis.observation_status -cne 'historical') { throw 'Offline analysis must remain historical.' }
    if ($analysis.category -notin @('ordinary_in_game', 'out_of_game', 'loading', 'unknown', 'special_in_game', 'special_setup')) {
        throw "Unrecognized classifier output: $($analysis.category)"
    }
    # Frozen ticket06 policy resolves supported information/result semantics without changing the dataset.
    # Other pending special/setup/transition mappings remain unsupported and reject to unknown.
    $target = if ($null -ne $record.expected_classifier_output) { $record.expected_classifier_output }
        elseif ($record.candidate_category -ceq 'special_in_game' -and $null -ne $record.PSObject.Properties['category_detail'] -and
            $record.category_detail -match '(information panel|battle-result overlay)$') { 'special_in_game' }
        else { 'unknown' }
    $contractMatch = $analysis.category -ceq $target
    $outcome = if ($analysis.category -ceq 'unknown' -and $record.candidate_category -cne 'unknown') { 'rejected' }
        elseif ($analysis.category -ceq $record.candidate_category) { 'correct' } else { 'misclassified' }
    $unsafeOrdinary = $analysis.category -ceq 'ordinary_in_game' -and
        ($record.candidate_category -in @('loading', 'special_in_game', 'special_setup', 'transition') -or $target -ceq 'unknown')
    [ordered]@{
        sample_id = $record.sample_id; frame_id = $record.frame_id; scene_group = $record.scene_group
        semantic_category = $record.candidate_category; frozen_target = $target; outcome = $outcome
        frozen_contract_match = $contractMatch
        independent_evaluation_candidate = $record.use_for_classifier_evaluation
        eligibility_reason = $record.classifier_evaluation_reason; sample_usage = $record.sample_usage
        unsafe_ordinary_prediction = $unsafeOrdinary; analysis = $analysis
    }
}
$eligible = @($rows | Where-Object { $_.independent_evaluation_candidate })
$counts = foreach ($category in @('ordinary_in_game', 'out_of_game', 'loading', 'unknown', 'special_in_game', 'special_setup', 'transition')) {
    $group = @($eligible | Where-Object { $_.semantic_category -ceq $category })
    [ordered]@{
        category = $category; samples = $group.Count
        correct = @($group | Where-Object { $_.outcome -ceq 'correct' }).Count
        rejected = @($group | Where-Object { $_.outcome -ceq 'rejected' }).Count
        misclassified = @($group | Where-Object { $_.outcome -ceq 'misclassified' }).Count
    }
}
$report = [ordered]@{
    schema = 4; report_producer = 'evaluate-recognition-v4'; split = $Split; created_utc = [DateTime]::UtcNow.ToString('o')
    executable_sha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
    manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $DatasetDirectory 'manifest.json') -Algorithm SHA256).Hash
    mapping_policy = 'ticket06-information-result-v2'
    selection_policy = $selectionPolicy
    scope = '1920x1200 offline archives; supported information/result views map to special_in_game; loading and other pending special/setup/transition mappings reject to unknown. Counts are frames, not independent scenes.'
    evaluated_archives = @($rows).Count; external_images_not_analyzed = @(External-Records $selected).Count
    unreviewed_archives_excluded = @($selected.samples | Where-Object { $null -eq $_.candidate_category }).Count
    reviewed_diagnostic_archives_excluded = @($selected.samples | Where-Object { $null -ne $_.candidate_category }).Count - $selectedRecords.Count
    independent_candidate_counts = @($counts)
    unsafe_ordinary_predictions = @($rows | Where-Object { $_.unsafe_ordinary_prediction }).Count
    results = @($rows)
}
$report | ConvertTo-Json -Depth 90 | Set-Content -LiteralPath $outputPath -Encoding utf8
Write-Output $outputPath
