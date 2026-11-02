############################################################################
#    Copyright (C) 2004 by Matthew Marshall                                #
#    mmarshall@myrealbox.com                                               #
#                                                                          #
############################################################################
#!python
import os

print("Renaming Globules...")

names = [("worker-walk",64,True),("worker-swim",128,False),("worker-harvest",192,True),\
    ("warrior-walk",256,True),("warrior-fight",384,True),("warrior-swim",320,False),\
    ("explorer",0,False)]

filesrenamed = 0

for name in names:
    filesstart = filesrenamed
    outputnum = name[1]
    for num in range(1,65):
        From = '%(N)s/%(#)04d.png' % {'N':name[0], '#':num}
        To = 'unit' + str(outputnum) + 'r.png'
        try:
            os.rename( From, To)
            filesrenamed = filesrenamed + 1
        except OSError:
            None
        outputnum = outputnum + 1
    outputnum = outputnum - 64
    if name[2]:
        for num in range(65,129):
            From = '%(N)s/%(#)04d.png' % {'N':name[0], '#':num}
            To = 'unit' + str(outputnum) + '.png'
            try:
                os.rename( From, To)
                filesrenamed = filesrenamed + 1
            except OSError:
                None
            outputnum = outputnum + 1
    print(("Renamed",filesrenamed - filesstart,"files for '",name[0],"'"))
print(("Renamed a total of",filesrenamed,"files."))