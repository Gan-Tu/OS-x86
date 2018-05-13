# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(hit-rate) begin
(hit-rate) create "data"
(hit-rate) open "data"
(hit-rate) write "data"
(hit-rate) close "data"
(hit-rate) open "data"
(hit-rate) read "data"
(hit-rate) close "data"
(hit-rate) open "data"
(hit-rate) read "data"
(hit-rate) close "data"
(hit-rate) more cache hits
(hit-rate) end
EOF
pass;
