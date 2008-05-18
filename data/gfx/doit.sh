#!/bin/tcsh
@ i=0
while ($i < 448)
	convert  -type TrueColorMatte -transparent white unit${i}p.png -scale 400% /tmp/unitcool.png
	mogrify -blur 0 /tmp/unitcool.png
	convert -scale 25% /tmp/unitcool.png unit${i}r.png
	@ i = $i + 1
end
