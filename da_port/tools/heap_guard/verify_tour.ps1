# [DA_PORT] Verify-тур: обход ВСЕХ локаций под сборкой Mixed с живыми VERIFY.
#
# Отличие от heap_guard level_tour: здесь цель не крах, а СБОР сработавших VERIFY (-da_verify_continue
# их пишет и продолжает), со стеком (-no_call_stack_assert). Каждый заход прыгает на локацию из
# quicksave5, живёт N кадров под ускорением времени (ALife спавнит бои/события), пишет VERIFY в лог.
#
# ⛔ Опознание лога СТРОГО по времени старта захода И метке локации. Без фильтра времени Select-String
# берёт СТАРЫЙ лог с той же меткой (ловушка, уже ловленная в heap_guard-туре). Берём лог, который
# и содержит «локация: <имя>», и записан ПОСЛЕ старта этого захода.
#
# Mixed медленный (-g, 1.7 ГБ xrGame): прыжок = полная перезагрузка уровня. Таймаут щедрый, кадров
# probe немного. При таймауте лог всё равно копируется — частичный урожай ценен.
param(
    [string]$GameRoot = 'D:\Dead Air\Dead Air',
    [string]$Exe = 'bin_mixed\xr_3da.exe',
    [int]$Frames = 40,
    [int]$TimeFactor = 1000,
    [int]$AfterLoadFrames = 250,
    [int]$TimeoutSeconds = 900,
    [string[]]$Only = @()
)

$harness = 'D:\Dead Air\xray-16\da_port\tools\run_headless.ps1'
$vargs = '-r4 -nofpslock -force_flushlog -nointro -da_verify_continue -no_call_stack_assert'
$load = 'load_last_save cap3347 - quicksave5 ; load_last_save'
$outDir = 'D:\Dead Air\xray-16\da_port\tools\heap_guard\tour\verify_all'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# Все локации графа. l07_military исключена намеренно (мёртвая запись, пропустится сама).
$levels = @(
    'k00_marsh','l01_escape','l02_garbage','l03_agroprom','k01_darkscape','l04_darkvalley',
    'l05_bar','l06_rostok','l08_yantar','l09_deadcity','l10_limansk','l10_radar','l10_red_forest',
    'l11_hospital','l11_pripyat','l12_stancia','l12_stancia_2','l13_generators',
    'l03u_agr_underground','l04u_labx18','l08u_brainlab','l10u_bunker','l12u_sarcofag',
    'l12u_control_monolith','l13u_warlab','zaton','jupiter','jupiter_underground','pripyat',
    'labx8','k02_trucks_cemetery','fake_start','new_military'
)
if ($Only.Count) { $levels = $levels | Where-Object { $Only -contains $_ } }

$progress = Join-Path $outDir '_progress.txt'
Set-Content -Path $progress -Value ("verify-тур: {0} локаций, кадров {1}, время x{2}, таймаут {3}с — старт {4}" -f `
    $levels.Count, $Frames, $TimeFactor, $TimeoutSeconds, (Get-Date).ToString('HH:mm:ss')) -Encoding utf8

$logsDir = Join-Path $GameRoot 'appdata\logs'
$idx = 0
foreach ($lv in $levels) {
    $idx++
    $started = Get-Date
    Stop-Process -Name xr_3da -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
    $probe = "da_level_probe $lv $Frames $TimeFactor"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $harness -GameDir $GameRoot -Exe $Exe `
        -Arguments $vargs -Command $load -AfterLoad $probe -AfterLoadFrames $AfterLoadFrames `
        -TimeoutSeconds $TimeoutSeconds *> (Join-Path $outDir ("{0:d2}_{1}.стенд.txt" -f $idx, $lv))
    $secs = [int]((Get-Date) - $started).TotalSeconds

    # ⛔ Лог: и метка локации, и записан ПОСЛЕ старта захода.
    $glog = Get-ChildItem "$logsDir\*.log" -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $started } |
        Where-Object { Select-String -Path $_.FullName -SimpleMatch "локация: $lv" -Quiet -ErrorAction SilentlyContinue } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1

    if ($glog) {
        Copy-Item $glog.FullName (Join-Path $outDir ("{0:d2}_{1}.log" -f $idx, $lv)) -Force
        $q = Select-String -Path $glog.FullName -SimpleMatch 'KERNEL:QUIT' -Quiet
        $v = (Select-String -Path $glog.FullName -SimpleMatch 'FATAL ERROR' -ErrorAction SilentlyContinue).Count
        $done = if ($q) { 'вышла' } else { 'таймаут' }
    } else { $v = 'нет-лога'; $done = 'не-дошла' }

    Add-Content -Path $progress -Value ("[{0:d2}/{1}] {2,-24} {3,-8} VERIFY={4}  ({5}с)" -f `
        $idx, $levels.Count, $lv, $done, $v, $secs) -Encoding utf8
}
Stop-Process -Name xr_3da -Force -ErrorAction SilentlyContinue
Add-Content -Path $progress -Value ("=== ТУР ЗАВЕРШЁН {0} ===" -f (Get-Date).ToString('HH:mm:ss')) -Encoding utf8
