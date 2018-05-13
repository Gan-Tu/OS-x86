# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(tell-remove) begin
(tell-remove) create "test.txt"
(tell-remove) open "test.txt"
(tell-remove) remove "test.txt"
(tell-remove) end
tell-remove: exit(0)
EOF
pass;