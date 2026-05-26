@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

REM =======================================================================
REM Path Configuration
REM =======================================================================
set ROOT_PATH=%~dp0..
for %%i in ("%ROOT_PATH%") do set ROOT_PATH=%%~fi

REM =======================================================================
REM Project
REM =======================================================================
set PROJECT=KraftonEngine

set BUILD_PATH=%ROOT_PATH%\%PROJECT%\Bin\Debug
set EXE=%BUILD_PATH%\%PROJECT%.exe
set PDB=%BUILD_PATH%\%PROJECT%.pdb
set SRCINFO=%BUILD_PATH%\srcsrv.txt
set TEMP_RAW_LIST=%BUILD_PATH%\raw_files.txt
set PS1=%~dp0gen_srcsrv.ps1

REM =======================================================================
REM Servers
REM =======================================================================
set SOURCE_SERVER=\\172.21.10.44\sources
set SYMBOL_SERVER=\\172.21.10.44\symbols

REM =======================================================================
REM Tools
REM =======================================================================
set SDK_TOOLS=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64
set SRCSRV_TOOLS=%SDK_TOOLS%\srcsrv

echo =========================================
echo 0. Check files
echo =========================================

if not exist "%PDB%" (
    echo [ERROR] PDB not found: %PDB%
    exit /b 1
)

if not exist "%PS1%" (
    echo [ERROR] gen_srcsrv.ps1 not found: %PS1%
    exit /b 1
)

if not exist "%EXE%" (
    echo [WARNING] EXE not found: %EXE%
)

REM =======================================================================
REM 1. Copy Source and Required Files
REM =======================================================================
echo =========================================
echo 1. Copy Source to Source Server
echo =========================================

REM ★ EXE/PDB 복사
if exist "%EXE%" (
    xcopy "%EXE%" "%SOURCE_SERVER%\%PROJECT%\Bin\" /Y
)
if exist "%PDB%" (
    xcopy "%PDB%" "%SOURCE_SERVER%\%PROJECT%\Bin\" /Y
)

REM 1) 프로젝트 루트의 파일들 (main.cpp 등)
xcopy "%ROOT_PATH%\%PROJECT%\*.cpp" "%SOURCE_SERVER%\%PROJECT%\" /Y /D
xcopy "%ROOT_PATH%\%PROJECT%\*.h" "%SOURCE_SERVER%\%PROJECT%\" /Y /D

REM 2) 주요 소스 폴더들
xcopy "%ROOT_PATH%\%PROJECT%\Source\*" "%SOURCE_SERVER%\%PROJECT%\Source\" /E /I /Y /D
xcopy "%ROOT_PATH%\%PROJECT%\Shaders\*" "%SOURCE_SERVER%\%PROJECT%\Shaders\" /E /I /Y /D
xcopy "%ROOT_PATH%\%PROJECT%\ThirdParty\*" "%SOURCE_SERVER%\%PROJECT%\ThirdParty\" /E /I /Y /D

if errorlevel 1 (echo [ERROR] xcopy failed & exit /b 1)

REM =======================================================================
REM 2. Create & Inject FULL SOURCE INDEX
REM =======================================================================
echo =========================================
echo 2. Generating Full Source Index Table
echo =========================================

REM PDB에 링크된 원본 소스 파일 목록 추출 (-r 옵션)
"%SRCSRV_TOOLS%\srctool.exe" -r "%PDB%" > "%TEMP_RAW_LIST%"

echo [DEBUG] raw_files.txt 앞 5줄:
for /f "tokens=*" %%a in ('type "%TEMP_RAW_LIST%" ^| findstr /n "." ^| findstr /b "[1-5]:"') do echo %%a

REM ps1 파일 실행
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" ^
    -Project "%PROJECT%" ^
    -SourceServer "%SOURCE_SERVER%" ^
    -RawList "%TEMP_RAW_LIST%" ^
    -SrcInfo "%SRCINFO%"

if errorlevel 1 (
    echo [ERROR] No source files indexed or PowerShell failed.
    exit /b 1
)

if exist "%TEMP_RAW_LIST%" del "%TEMP_RAW_LIST%"

REM PDB에 소스 인덱스 주입
echo [INFO] Running pdbstr...
"%SRCSRV_TOOLS%\pdbstr.exe" -w -p:"%PDB%" -s:srcsrv -i:"%SRCINFO%"
echo [INFO] pdbstr errorlevel: %errorlevel%
if errorlevel 1 (echo [ERROR] pdbstr injection failed & exit /b 1)

REM =======================================================================
REM 3. Upload Indexed PDB and EXE to Symbol Server
REM =======================================================================
echo =========================================
echo 3. Upload Indexed PDB and EXE to Symbol Server
echo =========================================

set NEED_PDB_UPLOAD=1
if exist "%SYMBOL_SERVER%\000Admin\history.txt" (
    findstr /I /C:"\"%PROJECT%.pdb\"" "%SYMBOL_SERVER%\000Admin\history.txt" >nul 2>&1
    if !errorlevel! equ 0 (set NEED_PDB_UPLOAD=0)
)

if !NEED_PDB_UPLOAD! equ 0 (
    echo [SKIP] PDB is already uploaded to Symbol Server.
) else (
    echo [UPLOAD] Uploading Indexed PDB...
    "%SDK_TOOLS%\symstore.exe" add /f "%PDB%" /s "%SYMBOL_SERVER%" /t "%PROJECT%"
    if !errorlevel! geq 1 (
        echo [ERROR] symstore PDB failed & exit /b 1
    )
)

if exist "%EXE%" (
    set NEED_EXE_UPLOAD=1
    if exist "%SYMBOL_SERVER%\000Admin\history.txt" (
        findstr /I /C:"\"%PROJECT%.exe\"" "%SYMBOL_SERVER%\000Admin\history.txt" >nul 2>&1
        if !errorlevel! equ 0 (set NEED_EXE_UPLOAD=0)
    )

    if !NEED_EXE_UPLOAD! equ 0 (
        echo [SKIP] EXE is already uploaded to Symbol Server.
    ) else (
        echo [UPLOAD] Uploading EXE...
        "%SDK_TOOLS%\symstore.exe" add /f "%EXE%" /s "%SYMBOL_SERVER%" /t "%PROJECT%"
        if !errorlevel! geq 1 (
            echo [ERROR] symstore EXE failed & exit /b 1
        )
    )
) else (
    echo [WARNING] Skipping EXE upload because EXE not found.
)

REM =======================================================================
REM 4. Verify
REM =======================================================================
echo =========================================
echo 4. Verify PDB Indexing Status
echo =========================================

echo [INFO] srctool verify:
"%SRCSRV_TOOLS%\srctool.exe" "%PDB%"
echo [INFO] srctool errorlevel: %errorlevel%

echo [INFO] pdbstr readback:
"%SRCSRV_TOOLS%\pdbstr.exe" -r -p:"%PDB%" -s:srcsrv
echo =========================================
echo DONE
echo =========================================
pause

endlocal
