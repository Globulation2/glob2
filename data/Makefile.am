SUBDIRS=src

glob2datadir= $(pkgdatadir)/data
glob2data_DATA= MacPal \
buildings.data buildings.txt font.png font2.png gui01.data gui02.data gui03.data \
gui05.data guiedit.data guiscr.txt menuFont.png misc.data ressources.data \
terrain.data texts.txt units.data units.txt

EXTRA_DIST= buildings.txt font.png font2.png guiscr.txt menuFont.png texts.txt \
	    units.txt guiedit.data  misc.data  ressources.data  terrain.data  units.data \
	    buildings.data MacPal


gui01.data: @srcdir@/src/guiscr1a.png @srcdir@/src/guiscr1b1.png @srcdir@/src/guiscr1b2.png @srcdir@/src/guiscr1b3.png @srcdir@/src/guiscr1b4.png @srcdir@/src/guiscr1b5.png @srcdir@/src/guiscr1b6.png
	cat @srcdir@/src/guiscr1a.png > $@
	cat @srcdir@/src/guiscr1b1.png >> $@
	cat @srcdir@/src/guiscr1b2.png >> $@
	cat @srcdir@/src/guiscr1b3.png >> $@
	cat @srcdir@/src/guiscr1b4.png >> $@
	cat @srcdir@/src/guiscr1b5.png >> $@
	cat @srcdir@/src/guiscr1b6.png >> $@

gui02.data: @srcdir@/src/guiscr2a.png @srcdir@/src/guiscr2b1.png @srcdir@/src/guiscr2b2.png @srcdir@/src/guiscr2b3.png
	cat @srcdir@/src/guiscr2a.png > $@
	cat @srcdir@/src/guiscr2b1.png >> $@
	cat @srcdir@/src/guiscr2b2.png >> $@
	cat @srcdir@/src/guiscr2b3.png >> $@

gui03.data: @srcdir@/src/guiscr3a.png @srcdir@/src/guiscr3b1.png @srcdir@/src/guiscr3b2.png
	cat @srcdir@/src/guiscr3a.png > $@
	cat @srcdir@/src/guiscr3b1.png >> $@
	cat @srcdir@/src/guiscr3b2.png >> $@

gui05.data: @srcdir@/src/guiscr5a.png @srcdir@/src/guiscr5b1.png @srcdir@/src/guiscr5b2.png @srcdir@/src/guiscr5b3.png
	cat @srcdir@/src/guiscr5a.png > $@
	cat @srcdir@/src/guiscr5b1.png >> $@
	cat @srcdir@/src/guiscr5b2.png >> $@
	cat @srcdir@/src/guiscr5b3.png >> $@
