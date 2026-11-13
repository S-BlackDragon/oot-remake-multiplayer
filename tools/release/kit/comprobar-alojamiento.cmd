@echo off
rem ORM: the host's self-check in Spanish (the English one is check-hosting.cmd). Before Host.bat
rem it says whether the server's ports are free; with the server window open, whether friends can
rem reach it and what to fix. It only reads and prints; it changes nothing.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\orm-check-hosting.ps1" %*
pause
