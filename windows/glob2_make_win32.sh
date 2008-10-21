#! C:\msys\1.0\bin\sh
rm -rf install_list.nsh
rm -rf uninstall_list.nsh
cd ../
"C:\Python26\python.exe" gen_inst_uninst_list.py
cd windows/
"C:\Program Files\NSIS\makensis.exe" //V2 win32_installer.nsi
