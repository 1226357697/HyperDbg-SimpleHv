@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo  SimpleHv 编译清理工具
echo ========================================
echo.

echo [1/6] 清理 build 目录...
if exist "build" (
    rmdir /s /q "build" 2>nul
    echo     - 已删除 build 目录
) else (
    echo     - build 目录不存在，跳过
)

echo [2/6] 清理 .vs 目录...
if exist ".vs" (
    rmdir /s /q ".vs" 2>nul
    echo     - 已删除 .vs 目录
) else (
    echo     - .vs 目录不存在，跳过
)

echo [3/6] 清理中间文件和输出目录...
for /d /r %%d in (x64,Debug,Release,obj,intermediate) do (
    if exist "%%d" (
        rmdir /s /q "%%d" 2>nul
        echo     - 已删除 %%d
    )
)

echo [4/6] 清理编译生成的文件...
for /r %%f in (*.obj,*.pch,*.pdb,*.ilk,*.exp,*.lib,*.log,*.tlog,*.lastbuildstate,*.idb,*.ipch,*.suo,*.user,*.sdf,*.opensdf) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
    )
)
echo     - 已清理编译生成的临时文件

echo [5/6] 清理驱动和可执行文件...
for /r %%f in (*.sys,*.exe,*.dll,*.inf) do (
    if exist "%%f" (
        echo     - 删除: %%f
        del /f /q "%%f" 2>nul
    )
)



echo.
echo ========================================
echo  清理完成！
echo ========================================
echo.
echo 提示：如果某些文件无法删除（被占用），请关闭 Visual Studio 后重试
echo.

pause
