#!/usr/bin/perl

# Rx Mode and Tx Mode:
#	N = Nint + (Nfrac / 130) = (0.5*Fvco) / 26M
#		where 0 <= Nfrac < 130
#		where 0 <= Ninit <= 127 (as Nint is 7 bit)
# where Fvco = 4 * Fch (GSM 850/900), Fvco = 2 * Fch (GSM 1800/1900)

# (Nint + (Nfrac / 130)) * 52 MHz = Fvco

sub mtk_fvco($$) {
	my ($nint, $nfrac) = @_;
	return ($nint + ($nfrac / 130)) * (26 * 2)
}

sub hr() {
	printf("======================================================================\n");
}

sub vco_print($$$)
{
	my ($nint, $nfrac, $hiband) = @_;
	my $fvco = mtk_fvco($nint, $nfrac);
	my $mult;

	if ($hiband == 1) {
		$mult = 2;
	} else {
		$mult = 4;
	}
	
	printf("Fch=%4.2f (Fvco=%4.2f, Nint=%03u, Nfrac=%03u)\n",
		$fvco/$mult, $fvco, $nint, $nfrac);
}

#for (my $nint = 0; $nint <= 127; $nint++) {
#	for (my $nfrac = 0; $nfrac <= 130; $nfrac++) {
#		vco_print($nint, $nfrac);
#	}
#}

printf("PLL Rx Low Band:\n");
for (my $nint = 68; $nint <= 73; $nint++) {
#for GSM 810
#for (my $b = 132; $b <= 150; $b++) {
	for (my $nfrac = 0; $nfrac <= 130; $nfrac++) {
		vco_print($nint, $nfrac, 0);
	}
}

hr();
printf("PLL Rx High Band:\n");
for (my $nint = 69; $nint <= 79; $nint++) {
	for (my $nfrac = 0; $nfrac <= 130; $nfrac++) {
		vco_print($nint, $nfrac, 1);
	}
}

hr();
printf("PLL Tx Low Band:\n");
for (my $nint = 63; $nint <= 70; $nint++) {
	for (my $nfrac = 0; $nfrac <= 130; $nfrac++) {
		vco_print($nint, $nfrac, 0);
	}
}


hr();
printf("PLL Tx High Band\n");
for (my $nint = 65; $nint <= 73; $nint++) {
	for (my $nfrac = 0; $nfrac <= 130; $nfrac++) {
		vco_print($nint, $nfrac, 1);
	}
}



exit(0);

