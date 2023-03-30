cd glob2

rd /s /q data
rd /s /q maps
rd /s /q games
rd /s /q logs
rd /s /q scripts

md data
md data\fonts
md data\gfx
md data\gfx\cursor
md data\gui
md data\zik
copy ..\data\*.* data
copy ..\data\fonts\*.* data\fonts
copy ..\data\gfx\*.* data\gfx
copy ..\data\gfx\cursor\*.* data\gfx\cursor
copy ..\data\gui\*.* data\gui
copy ..\data\zik\*.* data\zik
del data\makefile.am
del data\fonts\makefile.am
del data\gfx\makefile.am
del data\gfx\cursor\makefile.am
del data\gui\makefile.am
del data\zik\makefile.am
md maps
copy ..\maps\*.map maps

copy ..\release\glob2.exe .
del preferences.txt
del preferences.txt~