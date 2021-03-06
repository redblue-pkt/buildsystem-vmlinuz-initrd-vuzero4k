#! /bin/sh
# Copyright (C) 2013 Red Hat, Inc.
# This file is part of elfutils.
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# elfutils is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

. $srcdir/test-subr.sh

# The following files were obtaining by compiling funcretval_test.c
# from this directory as follows:
#
#   gcc -g funcretval_test.c -o funcretval_test_<YOURARCH>
#
# Pass -DFLOAT128 if the given arch supports __float128.

testfiles funcretval_test_aarch64

# funcretval_test_aarch64 was built with additional flag:
#   -DAARCH64_BUG_1032854
# hence no fun_vec_double_8.
testrun_compare ${abs_top_builddir}/tests/funcretval \
	-e funcretval_test_aarch64 <<\EOF
() fun_char: return value location: {0x50, 0}
() fun_short: return value location: {0x50, 0}
() fun_int: return value location: {0x50, 0}
() fun_ptr: return value location: {0x50, 0}
() fun_iptr: return value location: {0x50, 0}
() fun_long: return value location: {0x50, 0}
() fun_int128: return value location: {0x50, 0} {0x93, 0x8} {0x51, 0} {0x93, 0x8}
() fun_large_struct1: return value location: {0x70, 0}
() fun_large_struct2: return value location: {0x70, 0}
() fun_float: return value location: {0x90, 0x40}
() fun_float_complex: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4}
() fun_double: return value location: {0x90, 0x40}
() fun_double_complex: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_long_double: return value location: {0x90, 0x40}
() fun_long_double_complex: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_vec_char_8: return value location: {0x90, 0x40}
() fun_vec_short_8: return value location: {0x90, 0x40}
() fun_vec_int_8: return value location: {0x90, 0x40}
() fun_vec_long_8: return value location: {0x90, 0x40}
() fun_vec_float_8: return value location: {0x90, 0x40}
() fun_vec_char_16: return value location: {0x90, 0x40}
() fun_vec_short_16: return value location: {0x90, 0x40}
() fun_vec_int_16: return value location: {0x90, 0x40}
() fun_vec_long_16: return value location: {0x90, 0x40}
() fun_vec_int128_16: return value location: {0x90, 0x40}
() fun_vec_float_16: return value location: {0x90, 0x40}
() fun_vec_double_16: return value location: {0x90, 0x40}
() fun_hfa1_float: return value location: {0x90, 0x40}
() fun_hfa1_double: return value location: {0x90, 0x40}
() fun_hfa1_long_double: return value location: {0x90, 0x40}
() fun_hfa1_float_a: return value location: {0x90, 0x40}
() fun_hfa1_double_a: return value location: {0x90, 0x40}
() fun_hfa1_long_double_a: return value location: {0x90, 0x40}
() fun_hfa2_float: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4}
() fun_hfa2_double: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hfa2_long_double: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hfa2_float_a: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4}
() fun_hfa2_double_a: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hfa2_long_double_a: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hfa3_float: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4} {0x90, 0x42} {0x93, 0x4}
() fun_hfa3_double: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hfa3_long_double: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hfa3_float_a: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4} {0x90, 0x42} {0x93, 0x4}
() fun_hfa3_double_a: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hfa3_long_double_a: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hfa4_float: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4} {0x90, 0x42} {0x93, 0x4} {0x90, 0x43} {0x93, 0x4}
() fun_hfa4_double: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hfa4_long_double: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hfa4_float_a: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4} {0x90, 0x42} {0x93, 0x4} {0x90, 0x43} {0x93, 0x4}
() fun_hfa4_double_a: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hfa4_long_double_a: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_nfa5_float: return value location: {0x70, 0}
() fun_nfa5_double: return value location: {0x70, 0}
() fun_nfa5_long_double: return value location: {0x70, 0}
() fun_nfa5_float_a: return value location: {0x70, 0}
() fun_nfa5_double_a: return value location: {0x70, 0}
() fun_nfa5_long_double_a: return value location: {0x70, 0}
() fun_hva1_vec_char_8: return value location: {0x90, 0x40}
() fun_hva1_vec_short_8: return value location: {0x90, 0x40}
() fun_hva1_vec_int_8: return value location: {0x90, 0x40}
() fun_hva1_vec_long_8: return value location: {0x90, 0x40}
() fun_hva1_vec_float_8: return value location: {0x90, 0x40}
() fun_hva1_vec_double_8: return value location: {0x90, 0x40}
() fun_hva1_vec_char_16_t: return value location: {0x90, 0x40}
() fun_hva1_vec_short_16_t: return value location: {0x90, 0x40}
() fun_hva1_vec_int_16_t: return value location: {0x90, 0x40}
() fun_hva1_vec_long_16_t: return value location: {0x90, 0x40}
() fun_hva1_vec_int128_16_t: return value location: {0x90, 0x40}
() fun_hva1_vec_float_16_t: return value location: {0x90, 0x40}
() fun_hva1_vec_double_16_t: return value location: {0x90, 0x40}
() fun_hva2_vec_char_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hva2_vec_short_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hva2_vec_int_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hva2_vec_long_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hva2_vec_float_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hva2_vec_double_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() fun_hva2_vec_char_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva2_vec_short_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva2_vec_int_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva2_vec_long_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva2_vec_int128_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva2_vec_float_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva2_vec_double_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10}
() fun_hva3_vec_char_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hva3_vec_short_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hva3_vec_int_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hva3_vec_long_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hva3_vec_float_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hva3_vec_double_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_hva3_vec_char_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva3_vec_short_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva3_vec_int_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva3_vec_long_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva3_vec_int128_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva3_vec_float_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva3_vec_double_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_hva4_vec_char_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hva4_vec_short_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hva4_vec_int_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hva4_vec_long_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hva4_vec_float_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hva4_vec_double_8: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8} {0x90, 0x43} {0x93, 0x8}
() fun_hva4_vec_char_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hva4_vec_short_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hva4_vec_int_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hva4_vec_long_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hva4_vec_int128_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hva4_vec_float_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_hva4_vec_double_16_t: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10} {0x90, 0x43} {0x93, 0x10}
() fun_mixed_hfa3_cff: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4} {0x90, 0x42} {0x93, 0x4}
() fun_mixed_hfa3_cdd: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_mixed_hfa3_cldld: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_mixed_hfa3_fcf: return value location: {0x90, 0x40} {0x93, 0x4} {0x90, 0x41} {0x93, 0x4} {0x90, 0x42} {0x93, 0x4}
() fun_mixed_hfa3_dcd: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8} {0x90, 0x42} {0x93, 0x8}
() fun_mixed_hfa3_ldcld: return value location: {0x90, 0x40} {0x93, 0x10} {0x90, 0x41} {0x93, 0x10} {0x90, 0x42} {0x93, 0x10}
() fun_mixed_hfa2_fltsht_t: return value location: {0x90, 0x40} {0x93, 0x8} {0x90, 0x41} {0x93, 0x8}
() main: return value location: {0x50, 0}
EOF

exit 0
