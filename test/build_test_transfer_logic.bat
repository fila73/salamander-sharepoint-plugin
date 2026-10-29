@echo off
rem Builds and runs test_transfer_logic.cpp. Run from a VS x64 developer prompt (vcvars64.bat).
setlocal
pushd "%~dp0"
cl /nologo /EHsc /W3 /J test_transfer_logic.cpp user32.lib || (popd & exit /b 1)
.\test_transfer_logic.exe
set RC=%ERRORLEVEL%
popd
exit /b %RC%
