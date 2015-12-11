# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(buf-bl-wrt) begin
(buf-bl-wrt) read_diff_middle_before: 0
(buf-bl-wrt) write_diff_middle_before: 0
(buf-bl-wrt) read_diff_end_middle: 0
(buf-bl-wrt) write_diff_end_middle: 17
(buf-bl-wrt) end
EOF
pass;
