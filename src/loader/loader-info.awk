# Note: This file is included only for targets which have pokedata workaround
#
# Portable hex conversion: mawk/busybox awk have no strtonum(), so a bare
# strtonum() call aborts the whole build with "function strtonum never
# defined" on Debian/Ubuntu hosts. Keeping this POSIX-only makes the build
# work with whichever awk the CI runner ships.
function hex2dec(h,   i, d, n, c) {
	d = 0
	n = length(h)
	for (i = 1; i <= n; i++) {
		c = tolower(substr(h, i, 1))
		d = d * 16 + index("0123456789abcdef", c) - 1
	}
	return d
}
function hexval(s) { sub(/^0x/, "", s); return hex2dec(s) }
/\ypokedata_workaround\y/{pokedata_workaround=hexval($2)}
/\y_start\y/{start=hexval($2)}
END {
	print "#include <unistd.h>"
	print "const ssize_t offset_to_pokedata_workaround=" (pokedata_workaround-start) ";"
}
