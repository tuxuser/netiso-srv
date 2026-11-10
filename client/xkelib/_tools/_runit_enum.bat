@echo off


if not exist xam.def goto NOXAMDEF
REM call dump_libheader.bat "C:\Program Files (x86)\Microsoft Xbox 360 SDK\lib\xbox\xapilib.lib"
REM call dump_libheader.bat "C:\Program Files (x86)\Microsoft Xbox 360 SDK\lib\xbox\xav.lib"
call .\deffix.exe -enum xam.def xav.txt xam.h
call .\deffix.exe -enum xam.def xapilib.txt xam1.h
goto DOKERNEL
:NOXAMDEF
echo.
echo ERROR! xam.def was not found!

:DOKERNEL
if not exist kernel.def goto NOKERNELDEF
call dump_libheader.bat "C:\Program Files (x86)\Microsoft Xbox 360 SDK\lib\xbox\xboxkrnl.lib"
call .\deffix.exe -enum kernel.def xboxkrnl.txt kernel.h
goto EXIT

:NOKERNELDEF
echo.
echo ERROR! kernel.def was not found!

:EXIT
REM if not exist xapi_xam.txt goto NOTXT1
REM del /f /q xapi_xam.txt
REM :NOTXT1
REM if not exist xav_xam.txt goto NOTXT2
REM del /f /q xav_xam.txt
REM :NOTXT2
REM if not exist xboxkrnl.txt goto NOTXT3
REM del /f /q xboxkrnl.txt
REM :NOTXT3
echo.
echo.
echo Done!
pause
exit
