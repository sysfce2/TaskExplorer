set inno_path=%~dp0.\InnoSetup

mkdir %~dp0.\Output

rem "%inno_path%\ISCC.exe" /O%~dp0..\temp %~dp0..\temp\plus\Sandboxie-Plus.iss /DMyAppVersion=%SbiePlusVer% /DMyDrvVersion=%SbieVer% /DMyAppArch=x64 /DMyAppSrc=SbiePlus64
"%inno_path%\ISCC.exe" /O%~dp0.\Output %~dp0.\TaskExplorer.iss /DMyAppVersion=1.6.2

pause