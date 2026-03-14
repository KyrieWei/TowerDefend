@echo off
echo ========================================
echo  TowerDefend - Build
echo ========================================
echo.

call "F:\UnrealEngine\Engine\Build\BatchFiles\Build.bat" TowerDefendEditor Win64 Development -project="F:\TowerDefend\TowerDefend.uproject" -waitmutex

exit /b %ERRORLEVEL%
