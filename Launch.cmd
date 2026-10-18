@echo off
rem Builds the solution (backend and launcher), then opens the launcher window.
dotnet build "%~dp0HellForge.slnx" --nologo -v q -m:1 -nr:false
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
start "" "%~dp0src\HellForge.Launcher\bin\Debug\net10.0\HellForge.Launcher.exe"
