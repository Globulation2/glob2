#!/bin/sh
echo "Making glob2"
make >& /dev/null
cd src
echo "Creating static binary"
./staticlink.sh
cd ..
echo "Creating source distrib"
make dist >& /dev/null
echo "Decompressing dist archive in /tmp"
gunzip -cd glob2-0.1.tar.gz | tar x -C/tmp
echo "Patching archive with binary files"
cp src/glob2 /tmp/glob2-0.1/src
echo "Patching archive with map"
cp ~/.glob2/Miroir.map /tmp/glob2-0.1
cp ~/.glob2/Mondule.map /tmp/glob2-0.1
cp ~/.glob2/Quatruole.map /tmp/glob2-0.1
cp ~/.glob2/Lacs\ rivaux.map /tmp/glob2-0.1
cp ~/.glob2/Islands.map /tmp/glob2-0.1
echo "Recompressing archive"
cd /tmp
tar cfz glob2-0.1-static.tar.gz glob2-0.1/
scp glob2-0.1-static.tar.gz nct@lappc22.epfl.ch:~/public_html/
