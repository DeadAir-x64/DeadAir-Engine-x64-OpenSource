<#
.SYNOPSIS
    Обход ВСЕХ локаций под контролем кучи: по отдельному процессу на локацию, пачками.

.DESCRIPTION
    ⛔ ПОЧЕМУ НЕ ВСЕ СРАЗУ. Замер одного захода: 5.8 ГБ ОЗУ и 1.9 ГБ видеопамяти. Тридцать четыре
    процесса это 197 ГБ и 65 ГБ против имеющихся 47.7 и 15.2 — система уйдёт в своп, и часть
    прогонов умрёт от нехватки памяти, а не от наших ошибок. Отличить одно от другого по логу
    будет нельзя, то есть прогон не просто провалится, а СОВРЁТ. Отсюда пачками.

    ⭐ Почему по процессу на локацию, а не один длинный обход: при одном процессе первый же вылет
    обрывает всё остальное. Здесь вылет на одной локации не мешает соседним, и виновная названа точно.

    ⚠️ ЛОГ ОПОЗНАЁТСЯ ПО МЕТКЕ, А НЕ ПО ВРЕМЕНИ ФАЙЛА. Движок нумерует логи по кругу и на втором
    круге затирает старые, а при параллельном запуске два процесса вдобавок могут выбрать один
    номер. Поэтому лог ищется по строке `[DA_TOUR] локация: <имя>` внутри файла — совпадение по
    имени локации однозначно, а «самый свежий файл» в параллели врёт.

    ⚠️ user.ltx сохраняется в начале и возвращается в конце БАЙТ В БАЙТ: несколько процессов пишут
    его на выходе одновременно, и файл может испортиться. Значения при этом не меняются — что было,
    то и вернётся.

.EXAMPLE
    powershell -File level_tour.ps1
    powershell -File level_tour.ps1 -Parallel 3 -Frames 900
#>
param(
    [string]$GameRoot = 'D:\Dead Air\Dead Air',
    [string]$Exe = 'bin_heapguard\xrEngine.exe',

    # Сколько процессов держать одновременно. 5 подобрано по замеру: 5 x 5.8 ГБ = 29 ГБ, остаётся ~19.
    [int]$Parallel = 5,

    # Разбег между запусками: снижает и пик нагрузки на диск при загрузке, и вероятность того,
    # что два процесса выберут один номер лога.
    [int]$StaggerSeconds = 8,

    [int]$Frames = 600,
    [int]$TimeFactor = 1000,
    [int]$TimeoutSeconds = 420,
    [int]$QuarantineMB = 128,

    [string[]]$Only = @(),

    # С какого сохранения начинать каждый заход. Пусто — взять самое свежее НА МОМЕНТ СТАРТА обхода.
    [string]$SaveName = '',

    [string]$OutRoot = 'D:\Dead Air\xray-16\da_port\tools\heap_guard\tour'
)

$ErrorActionPreference = 'Stop'
$harness = Join-Path $PSScriptRoot '..\run_headless.ps1'
if (-not (Test-Path $harness)) { throw "не найден стенд: $harness" }

$logDir = Join-Path $GameRoot 'appdata\logs'
$userLtx = Join-Path $GameRoot 'appdata\user.ltx'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir = Join-Path $OutRoot $stamp
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$env:DA_HEAP_GUARD = '1'
$env:DA_HEAP_GUARD_MB = "$QuarantineMB"

# Снимок настроек: вернём в конце как было.
$userLtxBackup = Join-Path $outDir 'user.ltx.снимок'
if (Test-Path $userLtx) { Copy-Item $userLtx $userLtxBackup -Force }

# ⛔⛔ САМООТРАВЛЕНИЕ СТЕНДА. Прыжок на уровень заставляет игру сделать АВТОСОХРАНЕНИЕ, а каждый
# следующий заход стартует через load_last_save — то есть грузит уже не игру пользователя, а
# автосейв, снятый в момент смены уровня. У такого сейва не выставлено имя уровня, и загрузка
# падает на `level.ai` с пустым путём (R_ASSERT в CLevelGraph::Initialize).
#
# Проверено дорого: шесть заходов подряд умерли на старте, и выглядело это как регресс движка.
# Поэтому после каждого захода всё, что стенд насоздавал в сохранениях, УБИРАЕТСЯ отсюда — не
# удаляется, а переносится к отчёту, чтобы было и обратимо, и доступно для разбора.
$savesDir = Join-Path $GameRoot 'appdata\savedgames'
$savesOut = Join-Path $outDir 'сейвы_созданные_стендом'
$savesBefore = @{}
if (Test-Path $savesDir) {
    Get-ChildItem $savesDir -File | ForEach-Object { $savesBefore[$_.Name] = $true }
}

# ⛔ Сохранение выбирается ОДИН РАЗ и задаётся каждому заходу ЯВНО.
#
# `load_last_save` без аргумента грузит не самый свежий файл, а ЗАПОМНЕННОЕ ИМЯ (оно лежит в
# user.ltx). Автосейвы, которые обход создаёт своими прыжками, это имя перебивают — и дальше заходы
# грузят уже не игру, а состояние посреди смены уровня.
#
# 🪤 С тем же вызовом связана вторая ловушка: `load_last_save <имя>` НЕ ГРУЗИТ, а только запоминает
# имя. Поэтому нужны обе команды подряд: сначала задать, потом выполнить без аргумента.
# ⛔ АВТОСЕЙВЫ ИСКЛЮЧЕНЫ ИЗ ВЫБОРА. «Самое свежее сохранение» — ловушка: свежее всех как раз тот
# автосейв, который наплодил ПРЕДЫДУЩИЙ обход своими прыжками (или прерванный заход, после
# которого уборка Move-TourSaves не отработала). Дальше весь прогон стартует с состояния посреди
# смены уровня, и результаты двух обходов сравнивать НЕЛЬЗЯ — они шли с разных сохранений.
#
# Именно так и случилось: обход 20:41 стартовал с quicksave5 и дал 35 штатных завершений из 35, а
# обход 00:01 — с autosave, и дал 16 из 32. Разница выглядела регрессом кода, а была разницей
# входных данных. Прибор не соврал в вердикте, но соврал в СРАВНИМОСТИ.
if (-not $SaveName) {
    $all = @(Get-ChildItem $savesDir -Filter '*.scop' -ErrorAction SilentlyContinue)
    if (-not $all.Count) { throw "в $savesDir нет ни одного сохранения" }

    $candidates = @($all | Where-Object { $_.BaseName -notmatch 'autosave' })
    if (-not $candidates.Count) {
        throw ("в $savesDir есть только автосейвы ({0} шт.) — обход с них начинать нельзя. " +
               "Сохранитесь в игре вручную или задайте -SaveName явно." -f $all.Count)
    }

    $newest = $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $SaveName = $newest.BaseName

    $skipped = $all.Count - $candidates.Count
    if ($skipped -gt 0) { Write-Host "  (пропущено автосейвов при выборе старта: $skipped)" }
}
Write-Host "стартовое сохранение: $SaveName"
$loadCmd = "load_last_save $SaveName ; load_last_save"

function Move-TourSaves {
    if (-not (Test-Path $script:savesDir)) { return }
    $new = @(Get-ChildItem $script:savesDir -File | Where-Object { -not $script:savesBefore.ContainsKey($_.Name) })
    if (-not $new.Count) { return }
    if (-not (Test-Path $script:savesOut)) { New-Item -ItemType Directory -Path $script:savesOut -Force | Out-Null }
    foreach ($f in $new) {
        try { Move-Item $f.FullName -Destination $script:savesOut -Force } catch {}
    }
    Write-Host ("    (убрано созданных стендом сохранений: {0})" -f $new.Count)
}

Write-Host "обход локаций: одновременно $Parallel, кадров $Frames, время x$TimeFactor, карантин $QuarantineMB МБ"
Write-Host "логи и отчёт: $outDir"
Write-Host ''

# ---------------------------------------------------------------------------
# Список локаций ИЗ ГРАФА ИГРЫ
# ---------------------------------------------------------------------------
if ($Only.Count) {
    # 🪤 При запуске через `powershell -File` массив приходит ОДНОЙ строкой: `-Only a,b` даёт
    # элемент "a,b", и обход честно ищет локацию с таким именем. Разбираем сами.
    $levels = @($Only | ForEach-Object { $_ -split '[,;]' } | Where-Object { $_ } | ForEach-Object { $_.Trim() })
    Write-Host ("проверяются только заданные: {0}" -f ($levels -join ', '))
}
else {
    Write-Host 'запрашиваю список локаций из графа игры...'
    $listStarted = Get-Date
    & $harness -GameDir $GameRoot -Exe $Exe -Command $loadCmd `
               -AfterLoad 'da_list_levels quit' -AfterLoadFrames 60 -TimeoutSeconds 240 | Out-Null

    # ⛔ Только логи, изменённые ПОСЛЕ старта этого захода. Метка `всего локаций` есть и в логах
    # прошлых обходов, а они никуда не делись — без окна по времени берётся старый файл, и обход
    # спокойно работает по позавчерашним данным.
    $listLog = Get-ChildItem -LiteralPath $logDir -Filter '*.log' |
        Where-Object { $_.LastWriteTime -ge $listStarted } |
        Where-Object { Select-String -LiteralPath $_.FullName -Pattern '\[DA_LEVELS\] всего локаций' -Encoding UTF8 -Quiet } |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $listLog) { throw 'не нашёл СВЕЖИЙ лог со списком локаций — заход за списком не отработал' }
    Copy-Item $listLog.FullName (Join-Path $outDir '00_список_локаций.log') -Force

    $levels = @(Select-String -LiteralPath $listLog.FullName -Pattern '^~ \[DA_LEVELS\] (?<n>[A-Za-z0-9_]+)\s*$' -Encoding UTF8 |
        ForEach-Object { $_.Matches[0].Groups['n'].Value })
    if (-not $levels.Count) { throw 'в логе нет строк [DA_LEVELS] — команда не выполнилась' }
    Write-Host ("локаций в графе: {0}" -f $levels.Count)
}
Write-Host ''

# ---------------------------------------------------------------------------
# Пачки процессов
# ---------------------------------------------------------------------------
$queue = [System.Collections.Queue]::new()
$i = 0
foreach ($lv in $levels) { $i++; $queue.Enqueue([pscustomobject]@{ Index = $i; Level = $lv }) }

$running = @()
$results = @()
$claimedLogs = @{}

function Complete-Job($job) {
    $level = $job.Level
    $tag = '{0:d2}_{1}' -f $job.Index, $level

    # ⭐ Лог опознаём ПО МЕТКЕ ВНУТРИ ФАЙЛА. «Самый свежий» в параллели указывает на чужой прогон.
    #
    # ⛔ И ОБЯЗАТЕЛЬНО окно по времени. Метка с тем же именем локации есть в логах ПРОШЛЫХ обходов, а
    # логи никуда не деваются. Без этого условия заход, который на самом деле повис и был снят по
    # таймауту, получал приговор по УДАЧНОМУ логу прошлого прогона — и сводка бодро писала «чисто».
    # Проверено: два «разных» лога совпали побайтно, вплоть до числа блоков в карантине.
    $marker = "[DA_TOUR] локация: $level "
    $mine = Get-ChildItem -LiteralPath $script:logDir -Filter '*.log' |
        Where-Object { $_.LastWriteTime -ge $job.Started } |
        Where-Object { -not $script:claimedLogs.ContainsKey($_.Name) } |
        Where-Object { Select-String -LiteralPath $_.FullName -SimpleMatch $marker -Encoding UTF8 -Quiet } |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1

    $verdict = 'НЕ ДОШЛА до локации'; $kind = ''; $doubles = 0; $saved = ''

    if ($mine) {
        $script:claimedLogs[$mine.Name] = $true

        # ⛔ Ждём КОНЕЦ лога, а не «тишину».
        #
        # Процесс уже завершился, но хвост выключения дописывается не сразу: итог карантина
        # печатается ПОСЛЕДНИМ, из Core::_destroy(). Копия, снятая раньше, обрывается на строках
        # `refCount:`, и заход, отработавший штатно, получает приговор «не завершилась».
        #
        # 🪤 Ждать «пока размер не изменится» ОКАЗАЛОСЬ МАЛО: между выключением рендера и итогом
        # игра молчит дольше паузы опроса, и тишина принималась за конец. Поэтому ждём саму строку
        # итога, а размер остаётся лишь запасным признаком для тех прогонов, что до неё не дошли
        # (упали или были сняты).
        $prev = -1
        $stable = 0
        for ($w = 0; $w -lt 40; ++$w) {
            if (Select-String -LiteralPath $mine.FullName -SimpleMatch '[DA_HEAP_GUARD] карантин:' -Encoding UTF8 -Quiet) {
                break
            }
            $len = (Get-Item $mine.FullName).Length
            if ($len -eq $prev) { $stable++ } else { $stable = 0; $prev = $len }
            if ($stable -ge 6) { break }   # четыре секунды полной тишины — значит уже не допишется
            Start-Sleep -Milliseconds 700
        }

        $dest = Join-Path $script:outDir ($tag + '.log')
        Copy-Item $mine.FullName $dest -Force
        $saved = Split-Path $dest -Leaf

        $text = Get-Content -LiteralPath $dest -Encoding UTF8 -ErrorAction SilentlyContinue
        $doubles = ($text | Select-String -SimpleMatch '[DA_HEAP_GUARD] ДВОЙНОЕ ОСВОБОЖДЕНИЕ' | Measure-Object).Count

        # 🪤 `FATAL ERROR` наравне со стеком. Смерть по R_ASSERT стека НЕ печатает, и проверка
        # только по 'stack trace' записывала её в «не дошла» — то есть настоящий отказ движка
        # выглядел как невнятная неудача стенда. Ровно так и потерялись шесть заходов подряд.
        #
        # ⛔ И «stack trace» ищем СТРОГО с двоеточием, началом строки. Без этого шаблон ловит
        # `stack traceback:` — это вывод LUA, который мод печатает штатно и помногу. Один заход
        # набрал 892 таких совпадения и был объявлен вылетом, хотя в логе не было ни строки отказа,
        # ни [error]. Прибор не ошибся в мелочи — он назвал исправный прогон упавшим.
        $crashed = [bool]($text | Select-String -Pattern '^stack trace:') -or
                   [bool]($text | Select-String -SimpleMatch 'FATAL ERROR')

        # 🪤 Адрес берём ТОЛЬКО из строк настоящего отказа («! [DA_PORT] ... по адресу ...») и только
        # когда вылет вообще был. Простой поиск «по адресу» ловил ШАПКУ ЛОГА: в ней та же фраза
        # стоит пояснением к включённому карантину, и сводка бодро печатала «чисто» вместе с
        # «ОБРАЩЕНИЕ К ОСВОБОЖДЁННОЙ ПАМЯТИ» — то есть прибор противоречил сам себе.
        if ($crashed) {
            $fault = $text | Select-String -Pattern '^!\s+\[DA_PORT\].*по адресу ([0-9a-fA-F]{16})' | Select-Object -First 1
            $addr = if ($fault) { $fault.Matches[0].Groups[1].Value } else { '' }

            # ⭐ Ради этого различия весь контроль кучи и делался — выносим прямо в сводку.
            if ($addr -eq '00007ddddddddddd') { $kind = 'ОБРАЩЕНИЕ К ОСВОБОЖДЁННОЙ ПАМЯТИ' }
            elseif ($addr -match '^0{16}$')   { $kind = 'разыменование нуля' }
            elseif ($addr)                    { $kind = "адрес $addr" }
            else {
                # Отказ по утверждению: адреса нет, зато есть выражение и функция — они и полезны.
                $expr = $text | Select-String -Pattern '\[error\] Expression\s*:\s*(.+)' | Select-Object -First 1
                $func = $text | Select-String -Pattern '\[error\] Function\s*:\s*(.+)' | Select-Object -First 1
                if ($expr) {
                    $kind = "утверждение {0}" -f $expr.Matches[0].Groups[1].Value.Trim()
                    if ($func) { $kind += " в " + $func.Matches[0].Groups[1].Value.Trim() }
                }
                else { $kind = 'адрес не распознан' }
            }
        }

        # ⭐ Признак ШТАТНОГО ЗАВЕРШЕНИЯ: итог карантина печатается из Core::_destroy(), то есть
        # только при нормальном выходе. Снятый по таймауту процесс до него не доходит.
        #
        # 🪤 Без этой проверки сводка врала: заход, висевший все 420 секунд и убитый таймаутом,
        # получал «чисто» — ведь метку локации он поставил, а вылета не было. Так l07_military
        # отчиталась чистой, отработав ровно таймаут. «Дошла и не упала» НЕ ЗНАЧИТ «отработала».
        $finished = [bool]($text | Select-String -SimpleMatch '[DA_HEAP_GUARD] карантин:')

        # Локация, которой нет на диске: запись в графе игры осталась от базовой игры, а данных
        # уровня под неё нет (в Dead Air так с l07_military — мод заменил её на new_military).
        # Это НЕ дефект: игрок туда не попадёт, переходы ведут на замену. Отделяем от вылета,
        # иначе обход каждый раз показывает один и тот же «вылет», к которому все привыкают.
        $skipped = [bool]($text | Select-String -SimpleMatch '[DA_TOUR] ПРОПУСК:')

        if ($skipped)           { $verdict = 'пропущена (нет данных уровня)' }
        elseif ($crashed)       { $verdict = 'ВЫЛЕТ' }
        elseif ($doubles -gt 0) { $verdict = 'двойное освобождение' }
        elseif (-not $finished) { $verdict = 'НЕ ЗАВЕРШИЛАСЬ (таймаут)' }
        else                    { $verdict = 'чисто' }
    }

    $secs = [int]((Get-Date) - $job.Started).TotalSeconds
    Write-Host ("  [{0}/{1}] {2,-24} {3} {4} ({5}с)" -f $job.Index, $script:levels.Count, $level, $verdict, $kind, $secs)

    # ⛔ Убрать за собой ДО того, как стартует следующий заход: иначе он загрузит автосейв стенда.
    Move-TourSaves

    return [pscustomobject]@{
        'N' = $job.Index; 'локация' = $level; 'итог' = $verdict; 'вид' = $kind
        'двойных' = $doubles; 'секунд' = $secs; 'лог' = $saved
    }
}

while ($queue.Count -gt 0 -or $running.Count -gt 0) {

    while ($running.Count -lt $Parallel -and $queue.Count -gt 0) {
        $item = $queue.Dequeue()
        $probe = "da_level_probe {0} {1} {2}" -f $item.Level, $Frames, $TimeFactor
        # ⚠️ Не $args: это служебная переменная PowerShell, присваивание ей ведёт себя неожиданно.
        #
        # 🪤 Кавычки проставляются РУКАМИ. Start-Process в PowerShell 5.1 склеивает -ArgumentList
        # через пробел и НЕ заковычивает элементы сами по себе, а у нас в путях пробел («Dead Air»)
        # и в команде тоже. Без этого получаешь «не удалось обработать -File "D:\Dead"», причём
        # процесс запускается и умирает молча, а обход выглядит как «локация не дошла».
        $q = { param($s) '"' + $s + '"' }
        $psArgs = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', (& $q $harness),
            '-GameDir', (& $q $GameRoot), '-Exe', (& $q $Exe),
            '-Command', (& $q $loadCmd),
            '-AfterLoad', (& $q $probe), '-AfterLoadFrames', '120',
            '-TimeoutSeconds', "$TimeoutSeconds"
        )
        $outFile = Join-Path $outDir ('{0:d2}_{1}.стенд.txt' -f $item.Index, $item.Level)
        $proc = Start-Process powershell -ArgumentList $psArgs -PassThru -WindowStyle Hidden `
                               -RedirectStandardOutput $outFile
        $running += [pscustomobject]@{ Proc = $proc; Level = $item.Level; Index = $item.Index; Started = Get-Date }
        Write-Host ("запущено: {0} (в работе {1})" -f $item.Level, $running.Count)
        if ($queue.Count -gt 0) { Start-Sleep -Seconds $StaggerSeconds }
    }

    Start-Sleep -Seconds 3

    $done = @($running | Where-Object { $_.Proc.HasExited })
    foreach ($j in $done) { $results += (Complete-Job $j) }
    if ($done.Count) { $running = @($running | Where-Object { -not $_.Proc.HasExited }) }
}

# ---------------------------------------------------------------------------
# Возврат настроек и сводка
# ---------------------------------------------------------------------------
if (Test-Path $userLtxBackup) {
    Copy-Item $userLtxBackup $userLtx -Force
    Write-Host ''
    Write-Host 'user.ltx возвращён из снимка (значения не менялись)'
}

$results = @($results | Sort-Object N)
$table = $results | Format-Table -AutoSize | Out-String -Width 200
Set-Content -LiteralPath (Join-Path $outDir 'СВОДКА.txt') -Value $table -Encoding UTF8
$results | Export-Csv -LiteralPath (Join-Path $outDir 'сводка.csv') -Encoding UTF8 -NoTypeInformation

Write-Host ''
Write-Host '================ СВОДКА ================'
Write-Host $table
$bad = @($results | Where-Object { $_.'итог' -ne 'чисто' })
Write-Host ("локаций проверено: {0}, с замечаниями: {1}" -f $results.Count, $bad.Count)
Write-Host ("отчёт и логи: {0}" -f $outDir)
