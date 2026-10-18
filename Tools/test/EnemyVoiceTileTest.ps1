# 타일 맵에서 몬스터 외침·비명을 직접 쏴 보며 듣는 시험 (2026-10-18)
# 무엇을: 데디 서버(타일 맵, GM_InGame) + 클라 2개를 띄운다. 서버는 시험 옵션으로
#         플레이어마다 둘레 18~28m 에 몬스터를 놓고(1마리는 플레이어를 봄, 나머지는 등 돌림), 손에 소총을 쥐어 준다.
#         (시험 옵션 설명: Source/ProjectPG/Private/Dev/PGTestBattleSubsystem.h)
# 순서: 서버 켬 → 서버가 맵을 다 열고 접속을 받을 때까지 기다림(최대 10분) → 클라 2개 접속.
#       클라를 먼저 띄우면 20초 안에 접속 못 해 로비로 튕긴다.
# 창 위치: 보조 모니터(아래쪽 1920x1080)에 나란히.
# 끄기: 창을 닫거나, 이 스크립트가 알려 주는 번호로 Stop-Process.
# 실행: powershell -ExecutionPolicy Bypass -File Tools\test\EnemyVoiceTileTest.ps1
param(
    [string]$Engine = "E:\UE_5.6_Source",
    [int]$Enemies = 4,
    [string]$Rifle = "1010",
    # 먼저 들어온 몇 명 둘레에만 몬스터를 놓을지(0 = 모두). 죽으면 로비로 나가기 시험은 1.
    [int]$EnemyPlayers = 0,
    # 첫 몬스터가 플레이어를 보게(조작 없이 전투·사망 통계표까지 자동으로 보는 시험).
    [switch]$FacePlayer
)
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$project = Join-Path $root "ProjectPG.uproject"
$exe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor.exe"
# 클라는 UnrealEditor-Cmd.exe 로 띄운다(같은 엔진, 콘솔 창만 하나 더 뜸).
# 왜: 다른 프로젝트(BamtolChase)의 시험 정리 스크립트가 "UnrealEditor.exe ... -game" 을 프로젝트 구분 없이 다 꺼서, 우리 창이 로딩 중에 꺼졌다.
$clientExe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
# 접속 시간 제한을 60초 → 300초로. 왜: 에디터 방식 클라는 타일 맵을 불러오는 데 60초를 넘겨, 서버가 "응답 없음" 으로 끊고 로비로 튕겼다.
$netTimeout = "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=300,[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=300"
$logs = Join-Path $root ("Saved\EnemyVoiceTileTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $logs | Out-Null
$map = "/Game/PG/Level/TestLevel_MapGenerating?game=/Game/PG/Blueprint/GM_InGame.GM_InGame_C"
$enemyClass = "/Game/PG/Blueprint/Characters/CBP_CustomEnemyCharacter.CBP_CustomEnemyCharacter_C"
$serverLog = Join-Path $logs "server.log"

# 서버가 맵의 무거운 작업까지 끝냈나: 두 줄이 다 나오면 준비 끝.
#   "All facility visuals ready" = 시설 레벨 붙이기 끝, "Navigation build finished" = 몬스터 길찾기 지도 굽기 끝.
# 왜: "맵 열림" 직후엔 시설 레벨을 붙이느라 한 프레임에 10초 넘게 멈춰서, 그때 접속하면 20초 제한에 걸려 로비로 튕긴다.
# 예전엔 맵 검사기의 "Design navigation" 줄을 기다렸는데, 2026-10-19 부터 검사기는 에디터 시험 실행·-PGMapVerify 때만 돈다.
# 로그 파일은 서버가 켜지고 조금 뒤에야 생겨서, 파일이 없으면 "아직" 으로 본다.
function Test-ServerReady {
    (Test-Path $serverLog) -and [bool](Select-String -Path $serverLog -Pattern "All facility visuals ready:" -Quiet) `
        -and [bool](Select-String -Path $serverLog -Pattern "Navigation build finished:" -Quiet)
}
$server = Start-Process $exe -PassThru -WindowStyle Minimized -ArgumentList "`"$project`" $map -server -log -unattended -nullrhi $netTimeout -PGTestEnemies=$Enemies -PGTestEnemyClass=$enemyClass -PGTestRifle=$Rifle $(if ($EnemyPlayers -gt 0) { "-PGTestEnemyPlayers=$EnemyPlayers" }) $(if ($FacePlayer) { "-PGTestFacePlayer" }) -abslog=`"$serverLog`""
Write-Host "서버 켬 (번호 $($server.Id)), 맵 여는 중..."
$deadline = (Get-Date).AddMinutes(10)
while ((Get-Date) -lt $deadline -and -not (Test-ServerReady)) {
    if ($server.HasExited) { Write-Host "서버가 꺼짐. 로그: $serverLog"; exit 1 }
    Start-Sleep 3
}
if (-not (Test-ServerReady)) { Write-Host "10분 안에 서버가 맵 준비를 못 끝냄. 로그: $serverLog"; exit 1 }

$clients = @()
foreach ($i in 0, 1) {
    $x = 888 + 962 * $i
    $clients += Start-Process $clientExe -PassThru -ArgumentList "`"$project`" 127.0.0.1 -game -windowed $netTimeout -WinX=$x -WinY=1470 -ResX=958 -ResY=540 -abslog=`"$(Join-Path $logs "client$($i + 1).log")`""
}
Write-Host "클라 2개 접속 중 (번호 $($clients[0].Id), $($clients[1].Id)). 서버 로그 [PGTest]·[EnemyAI] 줄: $serverLog"
