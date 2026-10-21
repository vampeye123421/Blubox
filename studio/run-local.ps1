$ErrorActionPreference = 'Stop'

$studioExe = Join-Path $PSScriptRoot 'build\blubox_studio.exe'
if (-not (Test-Path -LiteralPath $studioExe)) {
  throw "Blubox Studio is not built yet: $studioExe"
}

try {
  $handoffStatus = Invoke-RestMethod -Uri 'http://localhost:3000/api/studio/handoff/status' -TimeoutSec 3
} catch {
  throw 'The website on localhost:3000 is still running its old login handler. Restart the website with npm start, then run this script again.'
}
if ($handoffStatus.service -ne 'blubox-website' -or -not $handoffStatus.studioHandoff) {
  throw 'The website on localhost:3000 does not have the Studio handoff handler. Restart it with npm start, then run this script again.'
}

# Keep website login and session validation on the same browser origin.
$env:BLUBOX_STUDIO_LOGIN_URL = 'http://localhost:3000/auth/studio'
$env:BLUBOX_STUDIO_API_BASE_URL = 'http://localhost:3000'

& $studioExe
