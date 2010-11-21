#!/usr/bin/perl

sub pll_rx($$$$$) {
	my ($a, $b, $p, $r, $l) = @_;

	return (($b*$p+$a)/($r*$l))*26;
}

sub pll_rx_low_band($$) {
	my ($a, $b) = @_;
	my $p = 64; my $r = 65; my $l = 4;
	return pll_rx($a, $b, $p, $r, $l);
}

sub pll_rx_high_band($$) {
	my ($a, $b) = @_;
	my $p = 64; my $r = 65; my $l = 2;
	return pll_rx($a, $b, $p, $r, $l);
}

sub pll_tx_gsm850_1($$) {
	my ($a, $b) = @_;
	my $p = 64; my $r = 55; my $l = 4; my $m = 26;

	my $left = ((1/$l) - (1/$m));
	my $right = (($b*$p+$a)/$r);

	return $left * $right * 26;
}

sub pll_tx_gsm850_2($$) {
	my ($a, $b) = @_;
	my $p = 64; my $r = 30; my $l = 4; my $m = 52;

	my $left = ((1/$l) - (1/$m));
	my $right = (($b*$p+$a)/$r);

	return $left * $right * 26;
}

sub pll_tx_gsm900($$) {
	my ($a, $b) = @_;
	my $p = 64; my $r = 35; my $l = 4; my $m = 52;

	my $left = ((1/$l) + (1/$m));
	my $right = (($b*$p+$a)/$r);

	return $left * $right * 26;
}

sub pll_tx_high($$) {
	my ($a, $b) = @_;
	my $p = 64; my $r = 70; my $l = 2; my $m = 26;

	my $left = ((1/$l) + (1/$m));
	my $right = (($b*$p+$a)/$r);

	return $left * $right * 26;
}

sub hr() {
	printf("======================================================================\n");
}

printf("PLL Rx Low Band:\n");
for (my $b = 135; $b <= 150; $b++) {
#for GSM 810
#for (my $b = 132; $b <= 150; $b++) {
	for (my $a = 0; $a <= 62; $a++) {
		printf("Fout=%4.2f (A=%03u, B=%03u)\n", pll_rx_low_band($a, $b), $a, $b);
	}
}

hr();
printf("PLL Rx High Band:\n");
for (my $b = 141; $b <= 155; $b++) {
	for (my $a = 0; $a <= 62; $a++) {
		printf("Fout=%4.2f (A=%03u, B=%03u)\n", pll_rx_high_band($a, $b), $a, $b);
	}
}

hr();
printf("PLL Tx GSM850_1\n");
for (my $b = 128; $b <= 130; $b++) {
#for GSM 810
#for (my $b = 125; $b <= 130; $b++) {
	for (my $a = 0; $a <= 62; $a++) {
		printf("Fout=%4.2f (A=%03u, B=%03u)\n", pll_tx_gsm850_1($a, $b), $a, $b);
	}
}

hr();
printf("PLL Tx GSM850_2\n");
for (my $b = 65; $b <= 66; $b++) {
	for (my $a = 0; $a <= 63; $a++) {
		printf("Fout=%4.2f (A=%03u, B=%03u)\n", pll_tx_gsm850_2($a, $b), $a, $b);
	}
}

hr();
printf("PLL Tx GSM900\n");
for (my $b = 68; $b <= 71; $b++) {
	for (my $a = 0; $a <= 63; $a++) {
		printf("Fout=%4.2f (A=%03u, B=%03u)\n", pll_tx_gsm900($a, $b), $a, $b);
	}
}

hr();
printf("PLL Tx GSM1800/1900\n");
for (my $b = 133; $b <= 149; $b++) {
	for (my $a = 0; $a <= 63; $a++) {
		printf("Fout=%4.2f (A=%03u, B=%03u)\n", pll_tx_high($a, $b), $a, $b);
	}
}

