/*
 *-----------------------------------------------------------------------------
 *
 * monetdbeStubs.h --
 *
 *	Stubs for procedures in monetdbeStubDefs.txt
 *
 * Generated by genExtStubs.tcl: DO NOT EDIT
 * 2023-11-29 12:45:48Z
 *
 *-----------------------------------------------------------------------------
 */

typedef struct monetdbeStubDefs {

    /* Functions from libraries: libmonetdbe monetdbe */

    const char* (*monetdbe_versionPtr)(void);
    int   (*monetdbe_openPtr)(monetdbe_database *db, char *url, monetdbe_options *opts);
    int   (*monetdbe_closePtr)(monetdbe_database db);
    char* (*monetdbe_get_autocommitPtr)(monetdbe_database dbhdl, int* result);
    char* (*monetdbe_set_autocommitPtr)(monetdbe_database dbhdl, int value);
    int   (*monetdbe_in_transactionPtr)(monetdbe_database dbhdl);
    char* (*monetdbe_queryPtr)(monetdbe_database dbhdl, char* query, monetdbe_result** result, monetdbe_cnt* affected_rows);
    char* (*monetdbe_result_fetchPtr)(monetdbe_result *mres, monetdbe_column** res, size_t column_index);
    char* (*monetdbe_cleanup_resultPtr)(monetdbe_database dbhdl, monetdbe_result* result);
    char* (*monetdbe_preparePtr)(monetdbe_database dbhdl, char *query, monetdbe_statement **stmt, monetdbe_result** result);
    char* (*monetdbe_bindPtr)(monetdbe_statement *stmt, void *data, size_t parameter_nr);
    char* (*monetdbe_executePtr)(monetdbe_statement *stmt, monetdbe_result **result, monetdbe_cnt* affected_rows);
    char* (*monetdbe_cleanup_statementPtr)(monetdbe_database dbhdl, monetdbe_statement *stmt);
    char* (*monetdbe_dump_databasePtr)(monetdbe_database dbhdl, const char *backupfile);
    char* (*monetdbe_dump_tablePtr)(monetdbe_database dbhdl, const char *schema_name, const char *table_name, const char *backupfile);
} monetdbeStubDefs;
#define monetdbe_version (monetdbeStubs->monetdbe_versionPtr)
#define monetdbe_open (monetdbeStubs->monetdbe_openPtr)
#define monetdbe_close (monetdbeStubs->monetdbe_closePtr)
#define monetdbe_get_autocommit (monetdbeStubs->monetdbe_get_autocommitPtr)
#define monetdbe_set_autocommit (monetdbeStubs->monetdbe_set_autocommitPtr)
#define monetdbe_in_transaction (monetdbeStubs->monetdbe_in_transactionPtr)
#define monetdbe_query (monetdbeStubs->monetdbe_queryPtr)
#define monetdbe_result_fetch (monetdbeStubs->monetdbe_result_fetchPtr)
#define monetdbe_cleanup_result (monetdbeStubs->monetdbe_cleanup_resultPtr)
#define monetdbe_prepare (monetdbeStubs->monetdbe_preparePtr)
#define monetdbe_bind (monetdbeStubs->monetdbe_bindPtr)
#define monetdbe_execute (monetdbeStubs->monetdbe_executePtr)
#define monetdbe_cleanup_statement (monetdbeStubs->monetdbe_cleanup_statementPtr)
#define monetdbe_dump_database (monetdbeStubs->monetdbe_dump_databasePtr)
#define monetdbe_dump_table (monetdbeStubs->monetdbe_dump_tablePtr)
MODULE_SCOPE monetdbeStubDefs *monetdbeStubs;
