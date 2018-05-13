# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(size-normal) begin
(size-normal) create "test.txt"
(size-normal) open "test.txt"
(size-normal) end
size-normal: exit(0)
EOF
pass;