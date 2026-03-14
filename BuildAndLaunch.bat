@echo off
echo ========================================
echo  TowerDefend - Build and Launch
echo ========================================

echo.
echo [1/2] Building TowerDefendEditor (Development)...
echo.

call "F:\UnrealEngine\Engine\Build\BatchFiles\Build.bat" TowerDefendEditor Win64 Development -project="F:\TowerDefend\TowerDefend.uproject" -waitmutex

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed with exit code %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Launching Unreal Editor...
echo.

"F:\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe" "F:\TowerDefend\TowerDefend.uproject"

exit /b %ERRORLEVEL%
