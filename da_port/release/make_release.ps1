# Собирает архив релиза для обновлятора: DeadAir-x64-bin-<версия>.zip
#
#   powershell -ExecutionPolicy Bypass -File make_release.ps1 -Version 2026.08.10
#
# Берёт каталог bin из пакета тестеров, кладёт внутрь файл версии и пакует так, как ожидает
# updater.ps1: в корне архива каталог bin, внутри него xrEngine.exe.
#
# Отдельный скрипт, а не «заархивируйте руками», по одной причине: обновлятор проверяет содержимое
# архива и отказывается ставить непохожий. Собранный руками архив с лишним уровнем вложенности
# выглядит нормально и не работает — а узнается это уже у тестера.

param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Source = 'D:\DeadAir_Tester',
    [string]$OutDir = 'D:\DeadAir_Tester\_release'
)

$ErrorActionPreference = 'Stop'

# [DA_PORT] curl и tar берутся ПО ПОЛНОМУ ПУТИ из System32, а не по имени.
#
# По имени они разрешаются через PATH, а там у многих стоит MSYS2, Git Bash или Cygwin со своими
# curl и tar. Юниксовый tar считает "D:\..." сетевым адресом и отвечает "Cannot connect to D:" —
# ошибка выглядит как проблема с диском, а не с тем, что вызвали не ту программу. Поймано на
# собственной машине при первом же прогоне сборки релиза.

$SysTar  = Join-Path $env:SystemRoot 'System32\tar.exe'
$SysCurl = Join-Path $env:SystemRoot 'System32\curl.exe'


function Say([string]$t, [string]$c = 'Gray') { Write-Host $t -ForegroundColor $c }

$binDir = Join-Path $Source 'bin'
if (-not (Test-Path (Join-Path $binDir 'xrEngine.exe')))
{
    Say "  В $binDir нет xrEngine.exe - это не каталог сборки." Red
    exit 1
}

# Версия ложится ВНУТРЬ архива: так она не может разойтись с тем, что реально лежит в bin.
# Если писать её отдельно в релиз, рано или поздно выложится архив от одной сборки с версией от
# другой, и обновлятор будет считать машину обновлённой.
Set-Content -Path (Join-Path $binDir 'da_version.txt') -Value $Version -Encoding ASCII

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$zip = Join-Path $OutDir "DeadAir-x64-bin-$Version.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue

# [DA_PORT] В архив идёт НЕ ТОЛЬКО bin.
#
# Половина правок порта живёт в свободных файлах gamedata: шейдеры r3, разметка настроек, строки
# локализации, скрипты вкладок. Они меняются теми же коммитами, что и движок, и обязаны обновляться
# вместе с ним. Разъедься они — и получится сборка, где движок ждёт одного, а данные говорят другое:
# ровно тот класс расхождений, который мы весь день ловили внутри самого проекта.
$dataDir = Join-Path $Source 'gamedata'
if (-not (Test-Path $dataDir))
{
    Say "  В $Source нет каталога gamedata." Red
    exit 1
}

Say "  Пакую bin + gamedata -> $(Split-Path -Leaf $zip)"

# Compress-Archive кладёт сами каталоги, если указать их без маски - ровно то, что нужно:
# в корне архива окажутся bin и gamedata.
Compress-Archive -Path $binDir, $dataDir -DestinationPath $zip -CompressionLevel Optimal

# Проверка собранного архива тем же способом, каким его будет проверять обновлятор.
$listing = & $SysTar -tf $zip 2>$null
if (-not ($listing | Where-Object { $_ -match 'bin/xrEngine\.exe$' }))
{
    Say '  В собранном архиве нет bin/xrEngine.exe - обновлятор такой не примет.' Red
    exit 1
}
if (-not ($listing | Where-Object { $_ -match '^gamedata/' }))
{
    Say '  В собранном архиве нет gamedata - обновлятор такой не примет.' Red
    exit 1
}

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Say ''
Say "  Готово: $zip ($mb МБ)" Green
Say ''
Say '  Дальше на GitHub:' Cyan
Say "    1. создать релиз с тегом  $Version" DarkGray
Say '    2. приложить к нему этот архив' DarkGray
Say '    3. опубликовать' DarkGray
Say ''
Say '  Тег и версия внутри архива совпадают - обновлятор сверяет именно их.' DarkGray
