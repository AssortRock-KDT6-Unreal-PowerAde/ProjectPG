<#
맵 같은 시드 시험: "같은 시드면 같은 맵·같은 상자" 를 명령 하나로 확인한다.
에디터 빌드 → 서버(-server -nullrhi) + 클라 1개를 띄움 → 서버 로그에서 지문을 뽑아 기대값 파일과 비교 → 합격/불합격.

쓰는 법 (저장소 맨 위에서):
  powershell -ExecutionPolicy Bypass -File Tools\test\MapSeedCheck.ps1
  powershell -ExecutionPolicy Bypass -File Tools\test\MapSeedCheck.ps1 -NoBuild -Port 7801
  # 맵을 일부러 바꿨으면, 나온 값을 보고 Tools\test\MapSeedExpected.txt 를 고친다.

종료 코드: 0 = 합격, 1 = 불합격, 2 = 준비 실패(빌드 실패, 엔진 없음 등).
주의: Content 의 서드파티 에셋 팩(TSL_CQBModularCore 등)이 없으면 벽이 빠져 지문이 달라진다.
#>
param(
    [int]$Seed = 12345,
    [int]$Port = 7777,
    [string]$Map = "/Game/PG/Level/TestLevel_MapGenerating",
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.6",
    [string]$Expected = "",
    [int]$TimeoutSec = 240,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$project = Join-Path $root "ProjectPG.uproject"
if (-not $Expected) { $Expected = Join-Path $PSScriptRoot "MapSeedExpected.txt" }
$exe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$logDir = Join-Path $root ("Saved\MapSeedCheck\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $logDir | Out-Null

function Say($t) { Write-Host "[시드 시험] $t" }

if (-not (Test-Path $exe)) { Say "엔진이 없어요: $exe"; exit 2 }
if (-not (Test-Path (Join-Path $root "Content\TSL_CQBModularCore"))) {
    Say "경고: Content\TSL_CQBModularCore 가 없어요. 서드파티 에셋 팩이 없으면 지문이 기대값과 다르게 나옵니다."
}

# 1) 에디터 빌드
if (-not $NoBuild) {
    Say "에디터 빌드 중..."
    & (Join-Path $Engine "Engine\Build\BatchFiles\Build.bat") ProjectPGEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE *> (Join-Path $logDir "build.log")
    if ($LASTEXITCODE -ne 0) { Say "빌드 실패. 로그: $logDir\build.log"; exit 2 }
}

# 2) 서버 + 클라 띄우기
$serverLog = Join-Path $logDir "server.log"
$clientLog = Join-Path $logDir "client.log"
$procs = @()
try {
    Say "서버 시작 (시드 $Seed, 포트 $Port)"
    $procs += Start-Process $exe -PassThru -WindowStyle Hidden -ArgumentList "`"$project`" $Map -server -nullrhi -PGMapSeed=$Seed -port=$Port -log -unattended -nosplash -nosound -abslog=`"$serverLog`""
    Start-Sleep 25
    Say "클라 접속"
    $procs += Start-Process $exe -PassThru -WindowStyle Hidden -ArgumentList "`"$project`" 127.0.0.1:$Port -game -nullrhi -log -unattended -nosplash -nosound -abslog=`"$clientLog`""

    # 서버의 시간 걸리는 검사(성능 50번)까지 끝나고, 클라가 상자를 받을 때까지 기다린다.
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep 5
        $serverDone = (Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern "Local performance:|timed checks gave up" -Quiet)
        $clientDone = (Test-Path $clientLog) -and (Select-String -Path $clientLog -Pattern "client received containers=" -Quiet)
        if ($serverDone -and $clientDone) { break }
    }
}
finally {
    $procs | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
}

# 3) 지문 뽑기
function Find($path, $regex) {
    if (-not (Test-Path $path)) { return $null }
    $m = Select-String -Path $path -Pattern $regex -CaseSensitive | Select-Object -Last 1
    if ($m) { return $m.Matches[0].Groups[1].Value } else { return $null }
}
$actual = [ordered]@{
    containers       = Find $serverLog "Loot spawn: containers=(\d+)"
    loot_hash        = Find $serverLog "loot_hash=([0-9A-F]{8})"
    final_point_hash = Find $serverLog "final_point_hash=([0-9A-F]{8})"
    layout_hash      = Find $serverLog "layout_hash=([0-9A-F]{8})"
    client_received  = Find $clientLog "client received containers=(\d+)"
}

# 4) 기대값과 비교 (client_received 는 거리에 따라 달라서 1 이상인지만 본다)
$want = @{}
Get-Content $Expected | Where-Object { $_ -match '^\s*([a-z_]+)\s*=\s*(\S+)' } | ForEach-Object { $want[$Matches[1]] = $Matches[2] }
$pass = $true
Write-Host ""
Write-Host ("{0,-18} {1,-10} {2,-10} {3}" -f "항목", "기대", "실제", "결과")
foreach ($key in $actual.Keys) {
    $got = $actual[$key]
    if ($key -eq "client_received") { $ok = $got -and [int]$got -ge 1; $exp = ">=1" }
    else { $exp = $want[$key]; $ok = $got -and ($got -eq $exp) }
    if (-not $ok) { $pass = $false }
    Write-Host ("{0,-18} {1,-10} {2,-10} {3}" -f $key, $exp, $(if ($got) { $got } else { "(없음)" }), $(if ($ok) { "OK" } else { "틀림" }))
}

# 시설 레벨 실패·시간 초과·검사 포기 Error 가 있으면 불합격
$errors = Select-String -Path $serverLog -Pattern "Facility design level (failed to load|timed out)|Map timed checks gave up" -CaseSensitive -ErrorAction SilentlyContinue
if ($errors) { $pass = $false; Write-Host ""; Write-Host "서버 Error:"; $errors | ForEach-Object { Write-Host ("  " + ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '')) } }

Write-Host ""
Say "로그: $logDir"
if ($pass) { Say "합격"; exit 0 } else { Say "불합격"; exit 1 }
