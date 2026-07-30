# Прогон установки обновления целиком, на песочнице.
#
# Настоящий пакет для этого не годится: 12 ГБ и живая игра. Здесь строятся две маленькие «сборки» —
# старая и новая, — и проверяется то, что важно в логике, а не в размере: что заменилось, что
# уцелело, что исчезло и осталась ли запасная копия.

$ErrorActionPreference = 'Stop'
$root = Join-Path $env:TEMP 'da_upd_test'
Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue

$game = Join-Path $root 'game'      # «установленная игра»
$new  = Join-Path $root 'new'       # из чего собираем архив новой версии
$out  = Join-Path $root 'out'

# --- старая установка -------------------------------------------------------------------------
New-Item -ItemType Directory -Path "$game\bin", "$game\gamedata\configs\ui", "$game\appdata" -Force | Out-Null
Set-Content "$game\bin\xrEngine.exe"        'СТАРЫЙ движок'
Set-Content "$game\bin\da_version.txt"      '2026.07.01'
Set-Content "$game\bin\dxgi.dll"            'чужой ReShade игрока'
Set-Content "$game\gamedata\configs\ui\stale.xml" 'файл, убранный из порта'
Set-Content "$game\appdata\user.ltx"        'настройки игрока'

# --- новая версия -----------------------------------------------------------------------------
New-Item -ItemType Directory -Path "$new\bin", "$new\gamedata\configs\ui" -Force | Out-Null
Set-Content "$new\bin\xrEngine.exe"         'НОВЫЙ движок'
Set-Content "$new\gamedata\configs\ui\fresh.xml" 'новая разметка'

& powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\DeadAir_Tester\make_release.ps1' `
    -Version '2026.08.01' -Source $new -OutDir $out | Out-Null

$zip = Get-ChildItem $out -Filter '*.zip' | Select-Object -First 1
if (-not $zip) { Write-Host 'ПРОВАЛ: архив не собрался' -ForegroundColor Red; exit 1 }

# --- установка --------------------------------------------------------------------------------
Copy-Item 'D:\DeadAir_Tester\updater.ps1' "$game\updater.ps1"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$game\updater.ps1" -Archive $zip.FullName | Out-Null

# --- что должно было получиться ---------------------------------------------------------------
$checks = @(
    @{ n = 'движок заменён на новый';        ok = (Get-Content "$game\bin\xrEngine.exe" -Raw).Contains('НОВЫЙ') }
    @{ n = 'чужой ReShade уцелел';           ok = (Test-Path "$game\bin\dxgi.dll") }
    @{ n = 'новая разметка приехала';        ok = (Test-Path "$game\gamedata\configs\ui\fresh.xml") }
    @{ n = 'убранный файл ИСЧЕЗ';            ok = -not (Test-Path "$game\gamedata\configs\ui\stale.xml") }
    @{ n = 'настройки игрока не тронуты';    ok = (Test-Path "$game\appdata\user.ltx") }
    @{ n = 'версия обновилась';              ok = (Get-Content "$game\bin\da_version.txt" -First 1).Trim() -eq '2026.08.01' }
    @{ n = 'запасная копия bin создана';     ok = (Get-ChildItem $game -Directory -Filter 'bin_backup_*').Count -eq 1 }
    @{ n = 'запасная копия gamedata создана'; ok = (Get-ChildItem $game -Directory -Filter 'gamedata_backup_*').Count -eq 1 }
    @{ n = 'старый файл достаётся из копии'; ok = (Get-ChildItem $game -Directory -Filter 'gamedata_backup_*' |
            ForEach-Object { Test-Path "$($_.FullName)\configs\ui\stale.xml" }) -contains $true }
)

$bad = 0
foreach ($c in $checks)
{
    if ($c.ok) { Write-Host ('  ok      ' + $c.n) -ForegroundColor Green }
    else       { Write-Host ('  ПРОВАЛ  ' + $c.n) -ForegroundColor Red; $bad++ }
}

# --- отдельно: порченый архив не должен разобрать установку ------------------------------------
$badZip = Join-Path $out 'broken.zip'
New-Item -ItemType Directory -Path "$root\junk\lib" -Force | Out-Null
Set-Content "$root\junk\lib\nothing.txt" 'мусор'
Compress-Archive -Path "$root\junk\lib" -DestinationPath $badZip -Force

$before = Get-Content "$game\bin\xrEngine.exe" -Raw
& powershell -NoProfile -ExecutionPolicy Bypass -File "$game\updater.ps1" -Archive $badZip | Out-Null
$after = Get-Content "$game\bin\xrEngine.exe" -Raw

if ($before -eq $after) { Write-Host '  ok      чужой архив отвергнут, сборка не тронута' -ForegroundColor Green }
else { Write-Host '  ПРОВАЛ  чужой архив разобрал установку' -ForegroundColor Red; $bad++ }

Write-Host ''
if ($bad -eq 0) { Write-Host '  всё зелено' -ForegroundColor Green } else { Write-Host "  провалов: $bad" -ForegroundColor Red }
