@echo off
rem Double-click entry point of the Nebulae MOD test package: Modulation or SD card.
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Nebulae-Mod-Test.ps1" %*
