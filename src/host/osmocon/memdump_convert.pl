#!/usr/bin/perl

my $num_line = 0;
my $num_hex = 0;
my $oldaddr;

while (my $line = <STDIN>) {
	chomp($line);
	$num_line++;
	my (@hex) = $line =~ /(\w{8}): (\w{8}) (\w{8}) (\w{8}) (\w{8})  (\w{8}) (\w{8}) (\w{8}) (\w{8})/;
	my $addr = hex(shift @hex);
	if ($addr != 0 && $addr != $oldaddr + 0x20) {
		printf(STDERR "gap of %u between 0x%08x and 0x%08x\n%s\n",
			$addr - $oldaddr, $addr, $oldaddr, $line);
	}
	foreach my $h (@hex) {
		$num_hex++;
		# poor mans endian conversion
		my ($a, $b, $c, $d) = $h =~/(\w\w)(\w\w)(\w\w)(\w\w)/;
		my $h_reorder = $d . $c . $b . $a;
		# convert into actual binary number
		my $tmp = pack('H8', $h_reorder);
		syswrite(STDOUT, $tmp, 4);
	}
	$oldaddr = $addr;
}

printf(STDERR "num lines/num hex: %u/%u\n", $num_line, $num_hex);

