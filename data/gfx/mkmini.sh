#!/bin/tcsh
@ i=0
while ($i < 46)
	convert -resize 40x40 building${i}.png buildingmini${i}.png
	convert -resize 40x40 building${i}r.png buildingmini${i}r.png
	@ i = $i + 1
end
