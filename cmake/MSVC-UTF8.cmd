@echo off
chcp 65001 >nul
set VSLANG=1033
%*
exit /b %errorlevel%
