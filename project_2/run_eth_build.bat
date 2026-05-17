@echo off
cd /d H:\testproject
del /q H:\testproject\project_2\eth_build_stdout.log 2>nul
del /q H:\testproject\project_2\eth_build_exitcode.txt 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -File H:\testproject\project_2\run_eth_build.ps1
echo EXITCODE=%ERRORLEVEL%> H:\testproject\project_2\eth_build_exitcode.txt
