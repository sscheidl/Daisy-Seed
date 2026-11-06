# One entry point for the Nebulae on Daisy MOD hardware test: switches between the shared
# Mod on Daisy editor and the shared Daisy Diagnostic SD panel. Only one of them may hold
# USB port J7, so the active client is closed, and has really exited, before the other
# opens. It carries no MIDI, CDC or card logic of its own: sharing, eject and return stay
# in the panel, and this script never starts sharing or force-closes anything.
#
#   Start-Nebulae-Mod-Test.cmd                     window with Modulation and SD card
#   Nebulae-Mod-Test.ps1 -Action Modulation -Yes   the same switch without dialogs (smoke test)
#   -Simulate                                      simulators only, the kit is not touched
#
# Exit codes with -Action: 0 done, 2 cancelled by the user, 3 the other client did not close.
param(
    [ValidateSet('Menu', 'Modulation', 'SdCard', 'Status', 'Close')]
    [string]$Action = 'Menu',
    [string]$Editor = (Join-Path $PSScriptRoot 'windows\Mod on Daisy Editor.exe'),
    [string]$Panel = (Join-Path $PSScriptRoot 'windows\DaisyDiagnostic.exe'),
    [switch]$Simulate,
    [switch]$Yes
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$Title = 'Nebulae on Daisy MOD test'
$CloseTimeoutSeconds = 30  # the panel unpacks itself on start and needs a few seconds

$SaveBeforeShare = @'
Going to SD card restarts the kit, and the modulation patch lives only in RAM.

Have you saved it in the editor with "Save file ..."?

Yes: close the editor and open the SD panel.
No: stay in the editor.
'@
$BackInPerformance = @'
Is the kit back in performance?

Eject the drive in Windows, click "Return to Nebulae" and wait until the panel shows Performance. Closing the panel never returns the card.

Yes: close the panel and open the editor.
No: stay in the panel.
'@
$SdSteps = @'
SD panel is open. Nothing is shared yet.

1. Open the tab "SD share".
2. Click "Share SD card with Windows". The kit restarts. If a Live loop exists, the panel asks first: it will be lost.
3. Copy your samples, then eject the drive in Windows.
4. Click "Return to Nebulae" and wait for Performance.
5. Click Modulation here.
'@
$ModulationSteps = @'
Editor is open and connects by itself.

After a restart the matrix on the kit is empty: click "Open file ..." and choose your saved .modpatch. Save the patch before you go to SD card.
'@

# By name, not by path: any copy of the app can hold J7, and Process.Path can fail on a
# process that is still starting, which would make a starting client look closed.
function Get-Client([string]$Path) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    @(Get-Process -Name $name -ErrorAction SilentlyContinue)
}

# Asks the windows to close and waits for the processes. Never kills: the panel may be in
# the middle of a card hand-over, and the editor may hold a patch that is not saved.
# A client that is still starting has no window yet, so each process is asked once, as soon
# as it has one.
function Close-Client([string]$Path) {
    $asked = @{}
    $deadline = (Get-Date).AddSeconds($CloseTimeoutSeconds)
    while($true) {
        $running = Get-Client $Path
        if($running.Count -eq 0) { return $true }
        if((Get-Date) -gt $deadline) { return $false }
        foreach($process in $running) {
            if($process.MainWindowHandle -ne 0 -and -not $asked.ContainsKey($process.Id)) {
                $asked[$process.Id] = $true
                [void]$process.CloseMainWindow()
            }
        }
        Start-Sleep -Milliseconds 200
    }
}

function Confirm-Step([string]$Text) {
    if($Yes) { return $true }
    $answer = [System.Windows.Forms.MessageBox]::Show($Text, $Title, 'YesNo', 'Question')
    return $answer -eq 'Yes'
}

function Show-Note([string]$Text) {
    if($Yes) { Write-Host $Text; return }
    [void][System.Windows.Forms.MessageBox]::Show($Text, $Title, 'OK', 'Warning')
}

function Start-Client([string]$Target) {
    if($Target -eq 'SdCard') {
        if((Get-Client $Panel).Count -gt 0) { return }
        $arguments = if($Simulate) { @('--sim-nebulae') } else { @() }
        if($arguments.Count) { Start-Process -FilePath $Panel -ArgumentList $arguments }
        else { Start-Process -FilePath $Panel }
    } else {
        if((Get-Client $Editor).Count -gt 0) { return }
        if($Simulate) {
            $env:MOD_ON_DAISY_NO_AUTOCONNECT = '1'  # tests with a kit attached must not touch it
            Start-Process -FilePath $Editor -ArgumentList @('--simulators')
        } else {
            Start-Process -FilePath $Editor
        }
    }
}

# Returns 0, 2 or 3 as described at the top.
function Switch-Client([string]$Target) {
    if($Target -eq 'SdCard') { $other = $Editor; $question = $SaveBeforeShare; $name = 'Mod on Daisy Editor' }
    else { $other = $Panel; $question = $BackInPerformance; $name = 'Daisy Diagnostic' }
    if((Get-Client $other).Count -gt 0) {
        if(-not (Confirm-Step $question)) { return 2 }
        if(-not (Close-Client $other)) {
            Show-Note "$name did not close, so J7 is still in use. Answer its dialog or close it yourself, then try again."
            return 3
        }
    }
    Start-Client $Target
    return 0
}

function Get-Active {
    $editorOpen = (Get-Client $Editor).Count -gt 0
    $panelOpen = (Get-Client $Panel).Count -gt 0
    if($editorOpen -and $panelOpen) { return 'Both open: close one, they cannot share J7' }
    if($editorOpen) { return 'Active: Modulation (editor)' }
    if($panelOpen) { return 'Active: SD card (panel)' }
    return 'Active: none'
}

function Show-Menu {
    $form = New-Object System.Windows.Forms.Form
    $form.Text = $Title
    $form.ClientSize = New-Object System.Drawing.Size(520, 400)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedSingle'
    $form.MaximizeBox = $false
    $font = New-Object System.Drawing.Font('Segoe UI', 10)
    $form.Font = $font

    $status = New-Object System.Windows.Forms.Label
    $status.Location = New-Object System.Drawing.Point(16, 14)
    $status.Size = New-Object System.Drawing.Size(488, 24)
    $status.Font = New-Object System.Drawing.Font('Segoe UI', 10, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($status)

    $hint = New-Object System.Windows.Forms.Label
    $hint.Location = New-Object System.Drawing.Point(16, 134)
    $hint.Size = New-Object System.Drawing.Size(488, 252)
    $hint.Text = 'Modulation edits the matrix. SD card moves samples between the kit and Windows. Only one can use J7, so switching closes the other one.'
    $form.Controls.Add($hint)

    $modulation = New-Object System.Windows.Forms.Button
    $modulation.Text = 'Modulation'
    $modulation.Location = New-Object System.Drawing.Point(16, 48)
    $modulation.Size = New-Object System.Drawing.Size(236, 72)
    $modulation.Font = New-Object System.Drawing.Font('Segoe UI', 13)
    $form.Controls.Add($modulation)

    $card = New-Object System.Windows.Forms.Button
    $card.Text = 'SD card'
    $card.Location = New-Object System.Drawing.Point(268, 48)
    $card.Size = New-Object System.Drawing.Size(236, 72)
    $card.Font = New-Object System.Drawing.Font('Segoe UI', 13)
    $form.Controls.Add($card)

    $refresh = { $status.Text = Get-Active }
    $modulation.Add_Click({
        if((Switch-Client 'Modulation') -eq 0) { $hint.Text = $ModulationSteps }
        & $refresh
    })
    $card.Add_Click({
        if((Switch-Client 'SdCard') -eq 0) { $hint.Text = $SdSteps }
        & $refresh
    })
    $timer = New-Object System.Windows.Forms.Timer
    $timer.Interval = 1000
    $timer.Add_Tick($refresh)
    $timer.Start()
    & $refresh
    [void]$form.ShowDialog()
    $timer.Stop()
}

try {
    foreach($path in @($Editor, $Panel)) {
        if(-not (Test-Path -LiteralPath $path)) { throw "Missing: $path" }
    }
    $Editor = (Resolve-Path -LiteralPath $Editor).Path
    $Panel = (Resolve-Path -LiteralPath $Panel).Path
    switch($Action) {
        'Menu' { Show-Menu }
        'Status' { Write-Output (Get-Active) }
        'Close' {
            $closed = (Close-Client $Editor) -and (Close-Client $Panel)
            if(-not $closed) { exit 3 }
        }
        default {
            $result = Switch-Client $Action
            Write-Output (Get-Active)
            exit $result
        }
    }
} catch {
    if($Action -eq 'Menu') {
        [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, $Title, 'OK', 'Error')
    } else {
        Write-Error $_.Exception.Message
    }
    exit 1
}
