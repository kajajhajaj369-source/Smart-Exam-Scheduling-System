@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "PROJECT_DIR=%~dp0"
pushd "%PROJECT_DIR%" >nul 2>&1
if errorlevel 1 goto project_folder_error
set "PUSHD_OK=1"

rem Reuse this app if its health endpoint is already responding.
set "SERVER_RUNNING=0"
for /f "delims=" %%H in ('powershell.exe -NoProfile -Command "try { $r=Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:8000/health' -TimeoutSec 2; if ($r.StatusCode -eq 200 -and $r.Content.Contains('exam-seating')) { '1' } else { '0' } } catch { '0' }" 2^>nul') do set "SERVER_RUNNING=%%H"
if "%SERVER_RUNNING%"=="1" goto already_running

set "PYTHON_CMD="
where py.exe >nul 2>&1
if not errorlevel 1 (
    py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
    if not errorlevel 1 set "PYTHON_CMD=py -3"
)
if not defined PYTHON_CMD (
    where python.exe >nul 2>&1
    if not errorlevel 1 (
        python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
        if not errorlevel 1 set "PYTHON_CMD=python"
    )
)
if not defined PYTHON_CMD goto python_missing

where g++.exe >nul 2>&1
if errorlevel 1 goto compiler_missing

set "NEED_BUILD=0"
if not exist "allocator.exe" set "NEED_BUILD=1"
if exist "allocator.exe" (
    for /f "delims=" %%B in ('powershell.exe -NoProfile -Command "$src=Get-Item -LiteralPath 'allocator.cpp'; $exe=Get-Item -LiteralPath 'allocator.exe'; $src.LastWriteTimeUtc -gt $exe.LastWriteTimeUtc" 2^>nul') do if /i "%%B"=="True" set "NEED_BUILD=1"
)
if "%NEED_BUILD%"=="1" (
    echo Compiling allocator.cpp with C++17...
    g++ -std=c++17 -Wall -Wextra -O2 "allocator.cpp" -o "allocator.exe"
    if errorlevel 1 goto compile_failed
)

echo Starting the exam seating web app...
%PYTHON_CMD% "server.py" --open-browser
if errorlevel 1 goto server_failed
goto done

:already_running
echo The exam seating app is already running at http://127.0.0.1:8000/
echo Reusing it without opening another browser tab.
goto done

:python_missing
echo ERROR: Python 3.8 or newer was not found.
echo Install Python 3.8+ from https://www.python.org/downloads/windows/ and enable the Python Launcher or add Python to PATH.
set "EXIT_CODE=1"
goto finish

:compiler_missing
echo ERROR: g++ was not found.
echo Install GCC for Windows, such as MSYS2 UCRT64 MinGW-w64 GCC, and add its bin folder to PATH.
set "EXIT_CODE=1"
goto finish

:compile_failed
echo ERROR: allocator.cpp did not compile. Check the compiler output above.
set "EXIT_CODE=1"
goto finish

:server_failed
echo ERROR: The web server failed to start. Review the message above; port 8000 may already be occupied.
set "EXIT_CODE=1"
goto finish

:project_folder_error
echo ERROR: Could not open the project folder containing this launcher.
set "EXIT_CODE=1"
goto finish

:done
set "EXIT_CODE=0"

:finish
if defined PUSHD_OK popd >nul 2>&1
echo.
if "%EXIT_CODE%"=="1" pause
endlocal & exit /b %EXIT_CODE%
