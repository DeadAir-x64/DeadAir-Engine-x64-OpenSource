<#
.SYNOPSIS
    Прогон игры на СКРЫТОМ рабочем столе Windows с разбором её лога.

.DESCRIPTION
    Приём подсмотрен у Dead Air: Refined (tools/qa): CreateDesktop + lpDesktop в STARTUPINFO
    запускают процесс на отдельном рабочем столе, поэтому окно игры не забирает экран и не
    ворует фокус. Можно работать дальше, пока прогон идёт.

    Их набор шлёт игре синтетические нажатия клавиш и клики. Нам это по большей части не нужно:
    у нас уже есть `da_after_load <кадров> <команда>` (выполнить консольную команду через N кадров
    после загрузки) и `-start server(<сейв>/single/load)`, то есть сценарий задаётся аргументами
    запуска, а не имитацией игрока. Осталось три вещи, которые скрипт и делает: увести окно с
    экрана, дождаться (или убить по таймауту) и ВЫНЕСТИ ПРИГОВОР по логу.

    ⚠️ Скрытый рабочий стол — не то же, что фон. У процесса на нём нет доступа к настоящему
    экрану, но D3D работает: рисуется в оффскрин. Замеры FPS отсюда брать НЕЛЬЗЯ - композитор и
    режим представления другие. Скрипт для проверки «не падает / не виснет / что в логе», а не
    для производительности.

.EXAMPLE
    # просто запустить и убедиться, что доходит до меню и закрывается
    .\run_headless.ps1 -Arguments '-nointro' -TimeoutSeconds 120

.EXAMPLE
    # загрузить сейв, подождать 300 кадров и выйти
    .\run_headless.ps1 -Arguments '-nointro -start server(agr_test/single/load) keypress_on_start 0' `
                       -AfterLoad 'quit' -AfterLoadFrames 300

.EXAMPLE
    # проверить, что оснастка отчётов о крахе жива
    .\run_headless.ps1 -AfterLoad 'da_crash_test' -ExpectCrash
#>
param(
    [string]$GameDir = 'D:\Dead Air\Dead Air',
    [string]$Exe = 'bin\xrEngine.exe',
    [string]$Arguments = '-r4 -nofpslock -force_flushlog -nointro',

    # Консольная команда на первом кадре (например load_last_save).
    [string]$Command = '',

    # Команда, которую надо выполнить через N кадров после загрузки уровня (da_after_load).
    [string]$AfterLoad = '',
    [ValidateRange(1, 100000)]
    [int]$AfterLoadFrames = 300,

    # 90 с: рабочие сценарии укладываются в 22-33 с (запуск, загрузка сейва, 600 кадров), так что
    # запас есть, а НЕудача стоит полторы минуты вместо семи. Прежние 300 с себя не оправдали:
    # каждый промах по синтаксису команды съедал пять-семь минут ожидания на пустом месте.
    [ValidateRange(10, 3600)]
    [int]$TimeoutSeconds = 90,

    [string]$DesktopName = 'DeadAirQA',

    # Падение ОЖИДАЕТСЯ (проверка самой оснастки отчётов) - тогда его отсутствие и есть провал.
    [switch]$ExpectCrash,

    # Не прятать: запустить на обычном рабочем столе. Для случая «надо посмотреть глазами».
    [switch]$Visible
)

$ErrorActionPreference = 'Stop'

$exePath = Join-Path $GameDir $Exe
if (-not (Test-Path -LiteralPath $exePath)) { throw "не найден исполняемый файл: $exePath" }

$logDir = Join-Path $GameDir 'appdata\logs'
if (-not (Test-Path -LiteralPath $logDir)) { throw "не найден каталог логов: $logDir" }

# -da_cmd выполняет консольную команду на первом кадре (мост в da_memory_probe.cpp: своего
# механизма «команда при запуске» у движка нет). Через него взводим и da_after_load, который уже
# дождётся собственно загрузки уровня.
#
# ⚠️ Мост берёт ТОЛЬКО ПЕРВОЕ вхождение -da_cmd, поэтому два ключа в строке не работают: второй
# молча игнорируется (проверено - взвод da_after_load не срабатывал, прогон висел до таймаута).
# Собираем одну команду через консольный разделитель `;`.
if ($Arguments -match '-da_cmd') {
    throw '-da_cmd в -Arguments не поддерживается: пользуйтесь -Command и -AfterLoad'
}

$startupCommands = @()
if ($Command)   { $startupCommands += $Command }
if ($AfterLoad) { $startupCommands += "da_after_load $AfterLoadFrames $AfterLoad" }

if ($startupCommands.Count) {
    $joined = $startupCommands -join ' ; '
    $Arguments = "$Arguments -da_cmd `"$joined`""
}

# ---------------------------------------------------------------------------
# Запуск на отдельном рабочем столе
# ---------------------------------------------------------------------------
$native = @'
using System;
using System.Runtime.InteropServices;

public static class DaHiddenDesktop
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION
    {
        public IntPtr hProcess, hThread;
        public int dwProcessId, dwThreadId;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateDesktop(string desktop, IntPtr device, IntPtr devmode,
        int flags, uint access, IntPtr security);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool CloseDesktop(IntPtr desktop);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateProcess(string application, string commandLine,
        IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles, uint creationFlags,
        IntPtr environment, string currentDirectory, ref STARTUPINFO startupInfo,
        out PROCESS_INFORMATION processInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);

    // Возвращает id запущенного процесса. Бросает при неудаче.
    public static int Start(string exe, string args, string workDir, string desktopName)
    {
        IntPtr desktop = IntPtr.Zero;
        if (desktopName != null)
        {
            const uint DESKTOP_ALL = 0x000F01FF;
            desktop = CreateDesktop(desktopName, IntPtr.Zero, IntPtr.Zero, 0, DESKTOP_ALL, IntPtr.Zero);
            if (desktop == IntPtr.Zero)
                throw new InvalidOperationException("CreateDesktop failed: " + Marshal.GetLastWin32Error());
        }

        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        if (desktopName != null)
            si.lpDesktop = @"WinSta0\" + desktopName;

        PROCESS_INFORMATION pi;
        // Кавычки вокруг exe обязательны: в пути есть пробел ("Dead Air").
        string cmd = "\"" + exe + "\" " + args;
        // CREATE_NO_WINDOW: без него игра ПРИЦЕПЛЯЕТСЯ К КОНСОЛИ запускающего и сыплет в неё свой
        // вывод. Перенаправление stdout у родителя не спасает — консоль наследуется отдельно от
        // дескрипторов. На параллельном обходе локаций это дало 80 КБ сообщений физики поверх
        // полезного вывода. Нам её вывод не нужен: всё, что важно, идёт в лог игры.
        const uint CREATE_NO_WINDOW = 0x08000000;
        bool ok = CreateProcess(null, cmd, IntPtr.Zero, IntPtr.Zero, false, CREATE_NO_WINDOW,
            IntPtr.Zero, workDir, ref si, out pi);
        if (!ok)
        {
            int err = Marshal.GetLastWin32Error();
            if (desktop != IntPtr.Zero) CloseDesktop(desktop);
            throw new InvalidOperationException("CreateProcess failed: " + err);
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        // Рабочий стол намеренно НЕ закрываем: он должен пережить процесс, иначе окно
        // остаётся без стола. Windows уберёт его сама, когда последнее окно исчезнет.
        return pi.dwProcessId;
    }
}
'@

if (-not ('DaHiddenDesktop' -as [type])) {
    Add-Type -TypeDefinition $native -Language CSharp
}

# Логи, которые были ДО запуска: всё, что появится сверх этого, — наше.
$before = @{}
Get-ChildItem -LiteralPath $logDir -Filter '*.log' -ErrorAction SilentlyContinue |
    ForEach-Object { $before[$_.Name] = $_.LastWriteTimeUtc }

# [DA_PORT] Без этого падение показывает МОДАЛЬНОЕ окно, а на скрытом рабочем столе его некому
# нажать: прогон висел бы до таймаута, и «упало» стало бы неотличимо от «зависло». С флагом
# отчёт пишется молча, процесс выходит сам, а стек всё равно попадает в лог.
if ($Arguments -notmatch '-silent_error_mode') {
    $Arguments = "$Arguments -silent_error_mode"
}

$startedAt = Get-Date
Write-Host ("запуск: {0} {1}" -f $exePath, $Arguments)
Write-Host ("рабочий стол: {0}" -f $(if ($Visible) { 'обычный' } else { $DesktopName }))

$desktopArg = if ($Visible) { $null } else { $DesktopName }
$processId = [DaHiddenDesktop]::Start($exePath, $Arguments, $GameDir, $desktopArg)
Write-Host ("процесс: {0}" -f $processId)

# ---------------------------------------------------------------------------
# Ожидание
# ---------------------------------------------------------------------------
$timedOut = $false
try {
    $proc = Get-Process -Id $processId -ErrorAction Stop
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        $timedOut = $true
        Write-Host ("ТАЙМАУТ {0}с — снимаю процесс" -f $TimeoutSeconds)
        try { Stop-Process -Id $processId -Force -ErrorAction Stop } catch {}
        Start-Sleep -Milliseconds 500
    }
} catch {
    # процесс завершился раньше, чем мы успели за него взяться
}

$elapsed = [int]((Get-Date) - $startedAt).TotalSeconds
Write-Host ("прогон занял {0}с" -f $elapsed)

# ---------------------------------------------------------------------------
# Приговор по логу
# ---------------------------------------------------------------------------
$fresh = Get-ChildItem -LiteralPath $logDir -Filter '*.log' |
    Where-Object { -not $before.ContainsKey($_.Name) -or $before[$_.Name] -ne $_.LastWriteTimeUtc } |
    Sort-Object LastWriteTimeUtc -Descending

if (-not $fresh) {
    Write-Host 'ПРОВАЛ: игра не написала ни одной строки лога'
    exit 1
}

$log = $fresh[0].FullName
Write-Host ("лог: {0}" -f $log)
$text = Get-Content -LiteralPath $log -Encoding UTF8 -ErrorAction SilentlyContinue
if (-not $text) { $text = Get-Content -LiteralPath $log -ErrorAction SilentlyContinue }

# ⛔ «stack trace» ищется ОТДЕЛЬНО и строго — с начала строки и с двоеточием. Простое вхождение
# ловит `stack traceback:`, а это штатный вывод LUA: мод печатает его сотнями за заход. Из-за
# этого прибор объявлял ПРОВАЛ на совершенно чистом прогоне — ровно тот случай, когда врущий
# прибор хуже отсутствующего. Тот же дефект был в level_tour.ps1 и починен там раньше; здесь
# оставался второй, независимый экземпляр.
$crashed = @($text | Select-String -Pattern '^stack trace:' -ErrorAction SilentlyContinue) + @(
    $text | Select-String -SimpleMatch -Pattern @(
        '[DA_MODULES]',
        'Unexpected application termination',
        'FATAL ERROR'
    ) -ErrorAction SilentlyContinue
)
$crashed = @($crashed | Where-Object { $_ })

$daWarnings = $text | Select-String -SimpleMatch -Pattern '! [DA]' -ErrorAction SilentlyContinue

Write-Host ''
if ($daWarnings) {
    Write-Host ("наши предупреждения в логе: {0}" -f $daWarnings.Count)
    $daWarnings | Select-Object -First 10 | ForEach-Object { Write-Host ("    " + $_.Line.Trim()) }
}

$failed = $false

if ($crashed) {
    Write-Host ''
    Write-Host ("признаки падения: {0} строк" -f $crashed.Count)
    $crashed | Select-Object -First 5 | ForEach-Object { Write-Host ("    " + $_.Line.Trim()) }
    if (-not $ExpectCrash) { $failed = $true }
} elseif ($ExpectCrash) {
    Write-Host 'ПРОВАЛ: падение ожидалось (-ExpectCrash), но лог чист — оснастка отчётов молчит'
    $failed = $true
}

if ($timedOut) {
    if ($ExpectCrash -and $crashed) {
        # ⚠️ Упавший процесс НЕ выходит сам даже с -silent_error_mode: обработчик дописывает стек и
        # карту модулей, после чего остаётся внутри BugTrap. Проверено: лог обрывается ровно на
        # «конец карты», а процесс живёт до тех пор, пока его не снимут. Для ожидаемого падения
        # снятие по таймауту - штатный конец сценария, а не провал.
        Write-Host 'процесс снят по таймауту — для падения это ожидаемо (обработчик не выходит сам)'
    }
    else {
        Write-Host 'ПРОВАЛ: игра не завершилась сама (зависание либо слишком короткий таймаут)'
        $failed = $true
    }
}

Write-Host ''
if ($failed) { Write-Host 'ИТОГ: ПРОВАЛ'; exit 1 }
Write-Host 'ИТОГ: чисто'
exit 0
