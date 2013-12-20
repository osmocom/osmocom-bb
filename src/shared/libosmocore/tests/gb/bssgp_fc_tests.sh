#!/bin/sh
T=$1/bssgp_fc_test

# default test (1 second, insufficient queue depth)
$T

# default test (1 second, sufficient queue depth)
$T -d 100

# test with PDU too large for bucket max
$T -l 1000

# test with 100 byte PDUs (10 second)
$T -s 100

