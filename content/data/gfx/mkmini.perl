#!/usr/bin/perl

@toMiniFirst = ('swarm', 'market');
@toMiniNormal = ('inn', 'hosp', 'racetrack', 'pool', 'barracks', 'school', 'defencetower');
# only first
foreach $sprite (@toMiniFirst)
{
	system "convert -resize 40x40 ${sprite}0b0.png mini${sprite}0b0.png" ;
	system "convert -resize 40x40 ${sprite}0b0r.png mini${sprite}0b0r.png" ;
	system "convert -resize 40x40 ${sprite}0c0.png mini${sprite}0c0.png" ;
	system "convert -resize 40x40 ${sprite}0c0r.png mini${sprite}0c0r.png" ;
}
# all
foreach $sprite (@toMiniNormal)
{
	for ($i=0; $i<3; $i++)
	{
		system "convert -resize 40x40 ${sprite}${i}b0.png mini${sprite}${i}b0.png";
		system "convert -resize 40x40 ${sprite}${i}b0r.png mini${sprite}${i}b0r.png";
		system "convert -resize 40x40 ${sprite}${i}c0.png mini${sprite}${i}c0.png";
		system "convert -resize 40x40 ${sprite}${i}c0r.png mini${sprite}${i}c0r.png";
	}
}
#building site
for ($i=0; $i<6; $i++)
{
	system "convert -resize 40x40 buildingsite${i}.png minibuildingsite${i}.png";
	system "convert -resize 40x40 buildingsite${i}r.png minibuildingsite${i}r.png";
}
