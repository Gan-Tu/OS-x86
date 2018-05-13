# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(write-full) begin
(write-full) create "data"
(write-full) open "data"
(write-full) write "data"
(write-full) close "data"
(write-full) no additional reads
(write-full) end
EOF
pass;