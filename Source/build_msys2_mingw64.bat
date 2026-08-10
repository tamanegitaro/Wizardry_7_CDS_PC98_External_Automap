@echo off
setlocal
cd /d "%~dp0"

set "GXX="
set "WINDRES="
set "OBJDUMP="
set "EXE=Wizardry7Automap.exe"
set "RES=Wizardry7Automap.res.o"
set "REPORT=Wizardry7Automap_objdump.txt"

if exist C:\msys64\mingw64\bin\g++.exe set "GXX=C:\msys64\mingw64\bin\g++.exe"
if exist C:\msys64\mingw64\bin\windres.exe set "WINDRES=C:\msys64\mingw64\bin\windres.exe"
if exist C:\msys64\mingw64\bin\objdump.exe set "OBJDUMP=C:\msys64\mingw64\bin\objdump.exe"

if "%GXX%"=="" (
  where g++ >nul 2>nul
  if not errorlevel 1 set "GXX=g++"
)
if "%WINDRES%"=="" (
  where windres >nul 2>nul
  if not errorlevel 1 set "WINDRES=windres"
)
if "%OBJDUMP%"=="" (
  where objdump >nul 2>nul
  if not errorlevel 1 set "OBJDUMP=objdump"
)

if "%GXX%"=="" (
  echo g++.exe not found.
  echo Install MSYS2 MinGW64 and run:
  echo   pacman -S mingw-w64-x86_64-gcc
  pause
  exit /b 1
)
if "%WINDRES%"=="" (
  echo windres.exe not found.
  pause
  exit /b 1
)

if exist "%EXE%" del /q "%EXE%"
if exist "%RES%" del /q "%RES%"
if exist "%REPORT%" del /q "%REPORT%"

echo Building %EXE% with Win32 API/GDI and a static MinGW runtime...
"%WINDRES%" Wizardry7Automap.rc -O coff -o "%RES%"
if errorlevel 1 (
  echo Resource build failed.
  pause
  exit /b 1
)

"%GXX%" -m64 -std=gnu++17 -O2 -DNDEBUG -Wall -Wextra ^
  -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN ^
  -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
  -ffunction-sections -fdata-sections ^
  ExternalAutomapMain.cpp ^
  src\wizardry_am\am_wiz7_external.cpp ^
  src\wizardry_am\am_wiz7_res.cpp ^
  "%RES%" ^
  -o "%EXE%" ^
  -static -static-libgcc -static-libstdc++ ^
  -Wl,--gc-sections ^
  -municode -mwindows ^
  -lcomdlg32 -luser32 -lgdi32 -lshell32 -ladvapi32 ^
  -s
if errorlevel 1 (
  echo Build failed.
  if exist "%RES%" del /q "%RES%"
  pause
  exit /b 1
)

if exist "%RES%" del /q "%RES%"

echo Built %EXE%.

if not "%OBJDUMP%"=="" (
  "%OBJDUMP%" -p "%EXE%" > "%REPORT%" 2>&1
  if errorlevel 1 (
    echo ERROR: objdump could not inspect the EXE.
    type "%REPORT%"
    del /q "%REPORT%" >nul 2>nul
    pause
    exit /b 1
  )

  findstr /I /C:"SDL2.dll" /C:"libgcc_s_" /C:"libstdc++-6.dll" /C:"libwinpthread-1.dll" "%REPORT%" >nul
  if not errorlevel 1 (
    echo ERROR: An external SDL2 or MinGW runtime DLL dependency remains.
    findstr /I /C:"DLL Name:" "%REPORT%"
    del /q "%REPORT%" >nul 2>nul
    pause
    exit /b 1
  )

  echo Verified: no SDL2 or MinGW runtime DLL dependency.
  del /q "%REPORT%" >nul 2>nul
) else (
  echo Note: objdump was not found, so DLL dependencies were not automatically verified.
)

echo Administrator privileges are embedded in the EXE manifest.
pause
