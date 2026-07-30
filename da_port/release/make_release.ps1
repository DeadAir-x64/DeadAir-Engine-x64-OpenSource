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

Say "  Пакую $binDir -> $(Split-Path -Leaf $zip)"

# Compress-Archive кладёт сам каталог, если указать его без маски - ровно то, что нужно:
# в корне архива окажется bin.
Compress-Archive -Path $binDir -DestinationPath $zip -CompressionLevel Optimal

# Проверка собранного архива тем же способом, каким его будет проверять обновлятор.
$check = & tar.exe -tf $zip 2>$null | Where-Object { $_ -match 'bin/xrEngine\.exe$' }
if (-not $check)
{
    Say '  В собранном архиве нет bin/xrEngine.exe - обновлятор такой не примет.' Red
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
