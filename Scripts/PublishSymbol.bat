@echo off
chcp 65001 > nul
setlocal

REM =========================
REM Path Configuration (상대 경로 기준 설정)
REM =========================
REM %~dp0는 현재 배치 파일이 실행된 폴더 경로(끝에 \ 포함)를 의미합니다. (예: C:\Jungle_Week12_Team5\Scripts\)
REM 이 경로에서 한 단계 상위로 올라간(..) 곳이 프로젝트 루트 폴더가 됩니다.
set ROOT_PATH=%~dp0..
for %%i in ("%ROOT_PATH%") do set ROOT_PATH=%%~fi

REM =========================
REM Project
REM =========================
set PROJECT=KraftonEngine

set BUILD_PATH=%ROOT_PATH%\%PROJECT%\Bin\Debug
set EXE=%BUILD_PATH%\%PROJECT%.exe
set PDB=%BUILD_PATH%\%PROJECT%.pdb

REM =========================
REM Servers
REM =========================
set SOURCE_SERVER=\\DESKTOP-UFL5EJM\sources
set SYMBOL_SERVER=\\DESKTOP-UFL5EJM\symbols

REM =========================
REM Tools
REM =========================
set SDK_TOOLS=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64
set SRCSRV_TOOLS=%SDK_TOOLS%\srcsrv

echo =========================
echo 0. Check files
echo =========================

if not exist "%PDB%" (
    echo [ERROR] PDB not found: %PDB%
    exit /b 1
)

if not exist "%EXE%" (
    echo [WARNING] EXE not found: %EXE%
)

REM =========================
REM 1. Upload PDB and EXE to Symbol Server
REM =========================
echo =========================
echo 1. Upload PDB and EXE to Symbol Server
echo =========================

REM PDB 등록
"%SDK_TOOLS%\symstore.exe" add ^
    /f "%PDB%" ^
    /s "%SYMBOL_SERVER%" ^
    /t "%PROJECT%"

if errorlevel 1 (
    echo [ERROR] symstore PDB failed
    exit /b 1
)

REM EXE 등록
if exist "%EXE%" (
    "%SDK_TOOLS%\symstore.exe" add ^
        /f "%EXE%" ^
        /s "%SYMBOL_SERVER%" ^
        /t "%PROJECT%"
        
    if errorlevel 1 (
        echo [ERROR] symstore EXE failed
        exit /b 1
    )
) else (
    echo [WARNING] Skipping EXE upload because EXE not found.
)

REM =========================
REM 2. Copy EXE + Source
REM =========================
echo =========================
echo 2. Copy EXE and Source
echo =========================

xcopy "%BUILD_PATH%\*" "%SOURCE_SERVER%\%PROJECT%\Bin\" /E /I /Y

if errorlevel 1 (
    echo [ERROR] xcopy failed
    exit /b 1
)

xcopy "%ROOT_PATH%\%PROJECT%" "%SOURCE_SERVER%\%PROJECT%\Source\" /E /I /Y

REM =========================
REM 3. Create srcsrv info
REM =========================
echo =========================
echo 3. Create srcsrv info
echo =========================

set SRCINFO=%BUILD_PATH%\srcsrv.txt

(
echo SRCSRV: ini ------------------------------------------------
echo VERSION=2
echo VERCTRL=StGit
echo SRCSRV: variables = SRCSRVTRG SRCSRVCMD SRCSRVTRG:SOURCE
echo SRCSRVTRG=%SOURCE_SERVER%\%PROJECT%\Source\%%var2%%
echo SRCSRVTRG:SOURCE=%ROOT_PATH%\%PROJECT%
echo SRCSRVCMD=cmd /c copy "%%f" "%%targ%%\%%var2%%"
echo SRCSRV: source files = %SOURCE_SERVER%\%PROJECT%\Source\%%var2%%
echo SRCSRV: end ------------------------------------------------
) > "%SRCINFO%"

REM =========================
REM 4. Inject srcsrv into PDB
REM =========================
echo =========================
echo 4. Inject srcsrv into PDB
echo =========================

"%SRCSRV_TOOLS%\pdbstr.exe" -w -p:"%PDB%" -s:srcsrv -i:"%SRCINFO%"

if errorlevel 1 (
    echo [ERROR] pdbstr failed
    exit /b 1
)

REM =========================
REM 5. Verify
REM =========================
echo =========================
echo 5. Verify PDB
echo =========================

"%SRCSRV_TOOLS%\srctool.exe" -r "%PDB%"

echo =========================
echo DONE
echo =========================
pause

endlocal