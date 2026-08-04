# [DA_PORT] Офлайн-проверка шейдеров: собирает их тем же fxc, что и движок, ДО запуска игры.
#
# Зачем: неудачная сборка шейдера в игре не роняет ничего и почти ничего не говорит — движок молча
# подставляет заглушку, и вместо поломки видишь Т-позу или пропавшее оружие. Ошибку удобнее поймать
# здесь.
#
# Главная тонкость — включаемые файлы. Движок видит виртуальную файловую систему, где loose-правки
# перекрывают архив, а fxc так не умеет: он ищет рядом с включающим файлом и находит архивную копию.
# Поэтому сначала собирается слепок: распакованные шейдеры, а поверх них наши.
#
#   powershell -File check_shaders.ps1 vert.vs lmapE.vs
#   powershell -File check_shaders.ps1              # все правленые из gamedata\shaders\r3

param(
    [string[]]$Files = @(),
    # Дефайны, без которых у части шейдеров нет точки входа: варианты скиннинга у моделей
    # собираются каждый под своим SKIN_*, отсюда и суффиксы _0/_1/_2 в именах из лога.
    [string]$Defines = '',
    [string]$Loose = 'D:\Dead Air\Dead Air\gamedata\shaders\r3',
    [string]$Stock = 'D:\Dead Air\extracted\shaders\r3'
)

$ErrorActionPreference = 'Stop'

$fxc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\fxc.exe' -ErrorAction SilentlyContinue |
    Sort-Object FullName | Select-Object -Last 1
if (-not $fxc) { throw 'fxc.exe не найден в Windows Kits' }

if (-not (Test-Path $Stock)) {
    throw "нет распакованных шейдеров: $Stock (достать: python da_port\tools\unpack_xdb.py extract shaders\r3\)"
}

$merged = Join-Path $env:TEMP 'da_shader_merge'
if (Test-Path $merged) { Remove-Item $merged -Recurse -Force }
New-Item -ItemType Directory -Path $merged | Out-Null
Copy-Item "$Stock\*" $merged -Recurse -Force
Copy-Item "$Loose\*" $merged -Recurse -Force   # наши правки перекрывают архивные

if ($Files.Count -eq 0) {
    $Files = Get-ChildItem "$Loose\*.vs", "$Loose\*.ps" | ForEach-Object { $_.Name }
}

$bad = 0
foreach ($name in $Files) {
    $path = Join-Path $merged $name
    if (-not (Test-Path $path)) { Write-Host "  ПРОПУЩЕН (нет файла): $name"; continue }

    $profile = if ($name -like '*.vs') { 'vs_5_0' } else { 'ps_5_0' }
    # Через cmd: в PowerShell 5.1 перенаправление stderr нативной программы превращает КАЖДУЮ
    # строку в запись об ошибке, и обычное предупреждение шейдера выглядит как провал.
    $log = Join-Path $env:TEMP 'da_shader_fxc.txt'
    $defArgs = ''
    if ($Defines) { foreach ($d in $Defines -split ',') { $defArgs += ' /D ' + $d.Trim() } }
    $cmd = '"{0}" /nologo /T {1} /E main{6} /I "{2}" /Fo "{3}" "{4}" > "{5}" 2>&1' -f `
        $fxc.FullName, $profile, $merged, (Join-Path $env:TEMP 'da_shader.cso'), $path, $log, $defArgs
    cmd /c $cmd | Out-Null
    $out = if (Test-Path $log) { Get-Content $log } else { @() }
    $errors = $out | Where-Object { $_ -match 'error X' }
    if ($errors) {
        $bad++
        Write-Host "  ОШИБКА: $name"
        $errors | ForEach-Object { Write-Host "      $_" }
    } else {
        Write-Host "  ok: $name"
    }
}

Write-Host ""
if ($bad -gt 0) { Write-Host "ИТОГ: не собралось $bad"; exit 1 }
Write-Host 'ИТОГ: собрались все'
