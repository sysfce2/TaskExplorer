call copy_build.cmd x64
call copy_build.cmd x86

echo Ready to build the installer
pause

call make_installer.cmd