#!/usr/bin/perl

open FILE, $ARGV[0];

$langs = <FILE>;
chomp $langs;

print $langs + 1 . "\n";

while ($h = <FILE>) {
	print $h;
	open PATCH, $ARGV[1];
	$plangs = <PATCH>;
	chomp $plangs;
	while (($p = <PATCH>) && ($p ne $h)) {}
	for($i = 0; $i < $langs; $i++) {
		$l = <FILE>;
		print $l;
		if ($i < $plangs) {
			$p = <PATCH>;
		}
	}
	if ($p) {
		print $p;
	}
	else {
		if ($h =~ /%s/) {
			print "%s\n";
		}
		else {
			print "\n";
		}
	}
	close PATCH;
}
