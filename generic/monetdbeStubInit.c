/*
 * monetdbeStubInit.c --
 *
 *	Stubs tables for the foreign MonetDB Embedded library so that
 *	Tcl extensions can use them without the linker's knowing about them.
 *
 * @CREATED@ 2023-11-29 12:45:48Z by genExtStubs.tcl from monetdbeStubDefs.txt
 *
 *-----------------------------------------------------------------------------
 */

#include <tcl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "monetdbe.h"
#include "monetdbeStubs.h"

/*
 * Static data used in this file
 */

/*
 * ABI version numbers of the MonetDB Embedded API that we can cope with.
 */

static const char *const monetdbeSuffixes[] = {
    "", ".26", NULL
};

/*
 * Names of the libraries that might contain the MonetDB Embedded API
 */

static const char *const monetdbeStubLibNames[] = {
    /* @LIBNAMES@: DO NOT EDIT THESE NAMES */
    "libmonetdbe", "monetdbe", NULL
    /* @END@ */
};

/*
 * Names of the functions that we need from MonetDB
 */

static const char *const monetdbeSymbolNames[] = {
    /* @SYMNAMES@: DO NOT EDIT THESE NAMES */
    "monetdbe_version",
    "monetdbe_open",
    "monetdbe_close",
    "monetdbe_get_autocommit",
    "monetdbe_set_autocommit",
    "monetdbe_in_transaction",
    "monetdbe_query",
    "monetdbe_result_fetch",
    "monetdbe_cleanup_result",
    "monetdbe_prepare",
    "monetdbe_bind",
    "monetdbe_execute",
    "monetdbe_cleanup_statement",
    "monetdbe_dump_database",
    "monetdbe_dump_table",
    NULL
    /* @END@ */
};

/*
 * Table containing pointers to the functions named above.
 */

static monetdbeStubDefs monetdbeStubsTable;
monetdbeStubDefs* monetdbeStubs = &monetdbeStubsTable;

/*
 *-----------------------------------------------------------------------------
 *
 * MonetDBEInitStubs --
 *
 *	Initialize the Stubs table for the MonetDB API
 *
 * Results:
 *	Returns the handle to the loaded MonetDB embedded library, or NULL
 *	if the load is unsuccessful. Leaves an error message in the
 *	interpreter.
 *
 *-----------------------------------------------------------------------------
 */

MODULE_SCOPE Tcl_LoadHandle
MonetDBEInitStubs(Tcl_Interp* interp)
{
    int i, j;
    int status;			/* Status of Tcl library calls */
    Tcl_Obj* path;		/* Path name of a module to be loaded */
    Tcl_Obj* shlibext;		/* Extension to use for load modules */
    Tcl_LoadHandle handle = NULL;
				/* Handle to a load module */

    /* Determine the shared library extension */

    status = Tcl_EvalEx(interp, "::info sharedlibextension", -1,
			TCL_EVAL_GLOBAL);
    if (status != TCL_OK) return NULL;
    shlibext = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(shlibext);

    /* Walk the list of possible library names to find an MonetDB client */

    status = TCL_ERROR;
    for (i = 0; status == TCL_ERROR && monetdbeStubLibNames[i] != NULL; ++i) {
	for (j = 0; status == TCL_ERROR && monetdbeSuffixes[j] != NULL; ++j) {
	    path = Tcl_NewStringObj(monetdbeStubLibNames[i], -1);
	    Tcl_AppendObjToObj(path, shlibext);
	    Tcl_AppendToObj(path, monetdbeSuffixes[j], -1);
	    Tcl_IncrRefCount(path);

	    /* Try to load a client library and resolve symbols within it. */

	    Tcl_ResetResult(interp);
	    status = Tcl_LoadFile(interp, path, monetdbeSymbolNames, 0,
			      (void*)monetdbeStubs, &handle);
	    Tcl_DecrRefCount(path);
	}
    }

    /*
     * Either we've successfully loaded a library (status == TCL_OK),
     * or we've run out of library names (in which case status==TCL_ERROR
     * and the error message reflects the last unsuccessful load attempt).
     */
    Tcl_DecrRefCount(shlibext);
    if (status != TCL_OK) {
	return NULL;
    }
    return handle;
}
