# -*- tcl -*-
# Tcl package index file, version 1.1
#
if {[package vsatisfies [package provide Tcl] 9.0-]} {
    package ifneeded monetdbe 0.3.0 \
	    [list load [file join $dir libtcl9monetdbe0.3.0.so] [string totitle monetdbe]]
} else {
    package ifneeded monetdbe 0.3.0 \
	    [list load [file join $dir libmonetdbe0.3.0.so] [string totitle monetdbe]]
}

package ifneeded tdbc::monetdbe 0.3.0 \
    [list source [file join $dir tdbcmonetdbe.tcl]]
