#!/usr/bin/perl

my $f_in = 26*1000*1000;

for (my $mult = 1; $mult < 31; $mult++) {
	for (my $div = 0; $div < 3; $div++) {
		my $fout = $f_in * ($mult / ($div+1));
		printf("%03.1f MHz (mult=%2u, div=%1u)\n", $fout/(1000*1000), $mult, $div);
	}
}
