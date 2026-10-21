$ErrorActionPreference = 'Stop'

$studioExe = Join-Path $PSScriptRoot 'build\blubox_studio.exe'
if (-not (Test-Path -LiteralPath $studioExe)) {
  throw "Blubox Studio is not built yet: $studioExe"
}

$existingStudio = Get-Process -Name 'blubox_studio' -ErrorAction SilentlyContinue
if ($existingStudio) {
  throw 'Close the currently open Blubox Studio first. Then run this script so the debug console belongs to the active instance.'
}

try {
  $handoffStatus = Invoke-RestMethod -Uri 'http://localhost:3000/api/studio/handoff/status' -TimeoutSec 3
} catch {
  throw 'The website on localhost:3000 is still running its old login handler. Restart the website with npm start, then run this script again.'
}
if ($handoffStatus.service -ne 'blubox-website' -or -not $handoffStatus.studioHandoff) {
  throw 'The website on localhost:3000 does not have the Studio handoff handler. Restart it with npm start, then run this script again.'
}

$env:BLUBOX_STUDIO_LOGIN_URL = 'http://localhost:3000/auth/studio'
$env:BLUBOX_STUDIO_API_BASE_URL = 'http://localhost:3000'
$env:BLUBOX_STUDIO_DEBUG = '1'

# The browser starts the protocol handler as a separate process, so it does
# not inherit this PowerShell process' environment. This marker enables the
# same token-redacted file logger in both Studio processes.
$diagnosticsDirectory = Join-Path $env:LOCALAPPDATA 'BluboxStudio'
$diagnosticsLog = Join-Path $diagnosticsDirectory 'studio-debug.log'
$diagnosticsMarker = Join-Path $diagnosticsDirectory 'debug.enabled'
New-Item -ItemType Directory -Path $diagnosticsDirectory -Force | Out-Null
Set-Content -LiteralPath $diagnosticsMarker -Value 'enabled' -NoNewline
Set-Content -LiteralPath $diagnosticsLog -Value '' -NoNewline

Write-Host 'Starting Blubox Studio with local sign-in and diagnostic logging.'
Write-Host "Diagnostic log: $diagnosticsLog"
Write-Host 'Tokens are never printed. Keep this terminal open; Ctrl+C closes Studio.'

$studio = $null
$lastPosition = 0L
try {
  $studio = Start-Process -FilePath $studioExe -ArgumentList '--debug-console' -PassThru -NoNewWindow
  while (-not $studio.HasExited) {
    if (Test-Path -LiteralPath $diagnosticsLog) {
      $file = [System.IO.File]::Open($diagnosticsLog, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
      try {
        if ($file.Length -lt $lastPosition) { $lastPosition = 0L }
        [void]$file.Seek($lastPosition, [System.IO.SeekOrigin]::Begin)
        $reader = [System.IO.StreamReader]::new($file, [System.Text.Encoding]::UTF8, $true, 1024, $true)
        $newText = $reader.ReadToEnd()
        $lastPosition = $file.Position
        $reader.Dispose()
        if ($newText.Length -gt 0) { Write-Host $newText.TrimEnd() }
      } finally {
        $file.Dispose()
      }
    }
    Start-Sleep -Milliseconds 250
    $studio.Refresh()
  }

  # Flush any final diagnostics written as the app shut down.
  if (Test-Path -LiteralPath $diagnosticsLog) {
    $remaining = [System.IO.File]::ReadAllText($diagnosticsLog, [System.Text.Encoding]::UTF8)
    if ($remaining.Length -gt $lastPosition) { Write-Host $remaining.Substring([int]$lastPosition).TrimEnd() }
  }
  exit $studio.ExitCode
} finally {
  Remove-Item -LiteralPath $diagnosticsMarker -Force -ErrorAction SilentlyContinue
}
