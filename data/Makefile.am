SUBDIRS=fonts gfx gui zik icons

glob2datadir= $(pkgdatadir)/data
glob2data_DATA= $(wildcard $(srcdir)/*.txt)
EXTRA_DIST= $(glob2data_DATA)

glob2linkdir= $(datadir)/applications
glob2link_DATA= glob2.desktop
EXTRA_DIST= $(glob2link_DATA)
