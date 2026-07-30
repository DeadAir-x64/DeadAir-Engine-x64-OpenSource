# Обновление сборки Dead Air x64 из релизов GitHub.
#
# Запускается через "Обновить Dead Air.cmd" рядом. Ничего не требует от игрока: curl и tar входят
# в состав Windows 10 и 11, git и прочие инструменты не нужны.
#
# ЧТО ОБНОВЛЯЕТСЯ: только каталог bin, то есть движок. Сохранения, настройки (appdata), данные мода
# (gamedata, database) и сезоны не трогаются вовсе — обновление не может испортить прохождение.
#
# ЧТО НУЖНО СО СТОРОНЫ РЕПОЗИТОРИЯ:
#   1. Репозиторий должен быть ПУБЛИЧНЫМ — скрипт ходит без пароля и токена, как и должен ходить
#      инструмент, который раздают вместе с игрой.
#   2. В релизе должен лежать один файл-архив с именем, начинающимся на "DeadAir-x64-bin"
#      (.zip или .7z не нужен — только zip, его распаковывает штатный tar).
#   3. Внутри архива — каталог bin со всем содержимым.
#   4. Имя тега релиза и есть версия: с ним сравнивается bin\da_version.txt.

$ErrorActionPreference = 'Stop'

$Owner = 'DanesCrai1'
$Repo  = 'DeadAir-Engine-x64-OpenSource'
$AssetPrefix = 'DeadAir-x64-bin'

$Root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$BinDir    = Join-Path $Root 'bin'
$VersionFile = Join-Path $BinDir 'da_version.txt'

function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }

Say ''
Say '  Обновление Dead Air x64' Cyan
Say '  -----------------------'
Say ''

# --- игра не должна быть запущена -------------------------------------------------------------
# Библиотеки читаются при старте и держатся открытыми: подмена файлов под работающей игрой либо
# не удастся, либо даст смесь старого и нового кода в следующем запуске.
if (Get-Process -Name 'xrEngine' -ErrorAction SilentlyContinue)
{
    Say '  Игра запущена. Закройте её и запустите обновление снова.' Yellow
    exit 1
}

# --- какая версия стоит сейчас ----------------------------------------------------------------
$local = if (Test-Path $VersionFile) { (Get-Content $VersionFile -First 1).Trim() } else { '(неизвестно)' }
Say "  Установлено: $local"

# --- какая версия доступна --------------------------------------------------------------------
$api = "https://api.github.com/repos/$Owner/$Repo/releases/latest"
try
{
    $release = Invoke-RestMethod $api -Headers @{ 'User-Agent' = 'DeadAir-updater' } -TimeoutSec 30
}
catch
{
    Say ''
    Say '  Не удалось узнать последнюю версию.' Yellow
    Say "  $($_.Exception.Message)" DarkGray
    Say ''
    Say '  Обычные причины: нет интернета, или релизов ещё нет вовсе.' DarkGray
    exit 1
}

$latest = $release.tag_name
Say "  Доступно:    $latest"
Say ''

if ($latest -eq $local)
{
    Say '  У вас последняя версия, обновлять нечего.' Green
    exit 0
}

$asset = $release.assets | Where-Object { $_.name -like "$AssetPrefix*" } | Select-Object -First 1
if (-not $asset)
{
    Say "  В релизе $latest нет файла, начинающегося на $AssetPrefix - обновить нечем." Yellow
    exit 1
}

$sizeMb = [math]::Round($asset.size / 1MB, 1)
Say "  Скачиваю $($asset.name) ($sizeMb МБ)..."

$tmp = Join-Path $env:TEMP ("da_update_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
$archive = Join-Path $tmp $asset.name

try
{
    # curl, а не Invoke-WebRequest: он показывает ход загрузки и не держит весь файл в памяти.
    & curl.exe -L --fail --progress-bar -o "$archive" $asset.browser_download_url
    if ($LASTEXITCODE -ne 0) { throw "curl вернул код $LASTEXITCODE" }

    Say '  Распаковываю...'
    & tar.exe -xf "$archive" -C "$tmp"
    if ($LASTEXITCODE -ne 0) { throw "tar вернул код $LASTEXITCODE" }

    # Проверка ДО подмены: в архиве должно оказаться то, что мы ждём. Пустой или чужой архив
    # обязан остановить обновление здесь, а не после того, как старый bin уже переименован.
    $newBin = Join-Path $tmp 'bin'
    if (-not (Test-Path (Join-Path $newBin 'xrEngine.exe')))
    {
        throw 'в архиве нет bin\xrEngine.exe - похоже, файл собран не так, как ожидает этот скрипт'
    }

    # --- подмена с сохранением предыдущей сборки ----------------------------------------------
    $stamp  = Get-Date -Format 'yyyyMMdd_HHmm'
    $backup = Join-Path $Root "bin_backup_$stamp"

    Say "  Сохраняю нынешнюю сборку в bin_backup_$stamp"
    Move-Item -Path $BinDir -Destination $backup -Force
    Move-Item -Path $newBin -Destination $BinDir -Force

    # Файлы, которые игрок мог положить сам (например ReShade), переносим из старой сборки:
    # обновляется движок, а не всё подряд.
    Get-ChildItem $backup -File | Where-Object { -not (Test-Path (Join-Path $BinDir $_.Name)) } |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $BinDir $_.Name) }

    Set-Content -Path $VersionFile -Value $latest -Encoding ASCII

    Say ''
    Say "  Готово: $local -> $latest" Green
    Say "  Прежняя сборка осталась в bin_backup_$stamp - если что-то пойдёт не так," DarkGray
    Say '  удалите новый bin и переименуйте её обратно.' DarkGray
}
catch
{
    Say ''
    Say "  Обновление не выполнено: $($_.Exception.Message)" Red
    Say '  Сборка осталась прежней.' DarkGray
    exit 1
}
finally
{
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Say ''
