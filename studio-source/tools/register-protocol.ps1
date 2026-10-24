param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('Install', 'Uninstall')]
    [string] $Action,

    [Parameter(Position = 1)]
    [string] $ExecutablePath = (Join-Path $PSScriptRoot '..\build\blubox_studio.exe')
)

$protocolKey = 'HKCU:\Software\Classes\blubox-studio'

if ($Action -eq 'Uninstall') {
    Remove-Item -LiteralPath $protocolKey -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host 'Removed the blubox-studio:// protocol registration for the current user.'
    exit 0
}

$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath -ErrorAction Stop).Path
$commandKey = Join-Path $protocolKey 'shell\open\command'
New-Item -Path $commandKey -Force | Out-Null
Set-Item -LiteralPath $protocolKey -Value 'URL:Blubox Studio Protocol'
New-ItemProperty -LiteralPath $protocolKey -Name 'URL Protocol' -Value '' -PropertyType String -Force | Out-Null
Set-Item -LiteralPath $commandKey -Value ('"{0}" "%1"' -f $resolvedExecutable)

Write-Host "Registered blubox-studio:// for the current user: $resolvedExecutable"
