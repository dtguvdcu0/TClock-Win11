@echo off
setlocal
call "%~dp0..\..\..\build_plugin.bat" "%~dp0sample_actions.vcxproj" %*
exit /b %ERRORLEVEL%
