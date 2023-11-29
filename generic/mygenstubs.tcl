#!/usr/bin/tclsh

set variable [list tclsh ../tools/genExtStubs.tcl monetdbeStubDefs.txt monetdbeStubs.h monetdbeStubInit.c]
exec {*}$variable
