@echo off
setlocal
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=amd64 -arch=amd64
msbuild "%~dp0ComponentCtrl.vcxproj" /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:GenerateFullPaths=true /v:m
exit /b %errorlevel%
