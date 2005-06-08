SUBDIRS=fonts gfx gui zik

glob2datadir= $(pkgdatadir)/data
glob2data_DATA= $(wildcard $(srcdir)/*.txt)
EXTRA_DIST= $(glob2data_DATA)

glob2linkdir= $(datadir)/applications
glob2link_DATA= glob2.desktop
EXTRA_DIST= $(glob2link_DATA)

glob2icondir= $(datadir)/pixmaps
glob2icon_DATA= icons/*.png 
EXTRA_DIST= $(glob2icon_DATA)
