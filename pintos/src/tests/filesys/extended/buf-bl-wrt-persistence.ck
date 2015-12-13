# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_archive ({'a' => ["\2" x 8192]});
pass;
