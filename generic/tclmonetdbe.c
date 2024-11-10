#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <tcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monetdbe.h"
#include "monetdbeStubs.h"

#ifdef __cplusplus
}
#endif /* __cplusplus */

/*
 * I think I need add MonetDBEInitStubs definition here.
 */
MODULE_SCOPE Tcl_LoadHandle MonetDBEInitStubs(Tcl_Interp *);

/*
 * Windows needs to know which symbols to export.  Unix does not.
 * BUILD_monetdbe should be undefined for Unix.
 */
#ifdef BUILD_monetdbe
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif /* BUILD_monetdbee */

typedef struct ThreadSpecificData {
    int initialized;                    /* initialization flag */
    Tcl_HashTable *monetdbe_hashtblPtr; /* per thread hash table. */
    int stmt_count;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

TCL_DECLARE_MUTEX(myMutex);

/*
 * This struct is to record MonetDB/e database info,
 */
struct MonetDBEDATA {
    monetdbe_database db;
    Tcl_Interp *interp;
};

typedef struct MonetDBEDATA MonetDBEDATA;

/*
 * This struct is to record statement and result info.
 */
struct MonetDBEStmt {
    monetdbe_statement *statement;
    monetdbe_cnt affected_rows;
    monetdbe_cnt index;
    monetdbe_cnt nrows;
    size_t ncols;
    monetdbe_result *result;
    monetdbe_column **columns;
};

typedef struct MonetDBEStmt MonetDBEStmt;

static Tcl_Mutex monetdbeMutex;
static int monetdbeRefCount = 0;
static Tcl_LoadHandle monetdbeLoadHandle = NULL;


void MonetDBE_Thread_Exit(ClientData clientdata) {
    /*
     * This extension records hash table info in ThreadSpecificData,
     * so we need delete it when we exit a thread.
     */
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)Tcl_GetThreadData(
        &dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->monetdbe_hashtblPtr) {
        for (entry = Tcl_FirstHashEntry(tsdPtr->monetdbe_hashtblPtr, &search);
             entry != NULL;
             entry = Tcl_FirstHashEntry(tsdPtr->monetdbe_hashtblPtr, &search)) {
            // Try to delete entry
            Tcl_DeleteHashEntry(entry);
        }

        Tcl_DeleteHashTable(tsdPtr->monetdbe_hashtblPtr);
        ckfree(tsdPtr->monetdbe_hashtblPtr);
    }
}


static void DbDeleteCmd(void *db) {
    MonetDBEDATA *pDb = (MonetDBEDATA *)db;

    /*
     * Warning!!!
     * How to handle STAT_HANDLE command if users do not close correctly?
     */

    monetdbe_close(pDb->db);

    Tcl_Free((char *)pDb);
    pDb = 0;

    Tcl_MutexLock(&monetdbeMutex);
    if (--monetdbeRefCount == 0) {
        Tcl_FSUnloadFile(NULL, monetdbeLoadHandle);
        monetdbeLoadHandle = NULL;
    }
    Tcl_MutexUnlock(&monetdbeMutex);
}


static int MONETE_STMT(void *cd, Tcl_Interp *interp, int objc,
                       Tcl_Obj *const *objv) {
    MonetDBEDATA *pDb = (MonetDBEDATA *)cd;
    MonetDBEStmt *pStmt;
    Tcl_HashEntry *hashEntryPtr;
    int choice;
    int rc = TCL_OK;
    char *mHandle;

    /*
     * Check it is not NULL.
     */
    if (pDb == NULL) {
        return TCL_ERROR;
    }

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)Tcl_GetThreadData(
        &dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->initialized == 0) {
        tsdPtr->initialized = 1;
        tsdPtr->monetdbe_hashtblPtr =
            (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(tsdPtr->monetdbe_hashtblPtr, TCL_STRING_KEYS);
    }

    static const char *STMT_strs[] = {
        "bind",
        "execute",
        "fetch_row_list",
        "fetch_row_dict",
        "get_row_count",
        "get_col_count",
        "rows_affected",
        "get_columns",
        "close",
        0
    };

    enum STMT_enum {
        STMT_BIND,
        STMT_EXECUTE,
        STMT_FETCH_ROW_LIST,
        STMT_FETCH_ROW_DICT,
        STMT_GET_ROW_COUNT,
        STMT_GET_COL_COUNT,
        STMT_ROW_AFFECTED,
        STMT_COLUMNS,
        STMT_CLOSE
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "SUBCOMMAND ...");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], STMT_strs, "option", 0, &choice)) {
        return TCL_ERROR;
    }

    /*
     * Get back MonetDBEStmt pointer from our hash table
     */
    mHandle = Tcl_GetStringFromObj(objv[0], 0);
    hashEntryPtr = Tcl_FindHashEntry(tsdPtr->monetdbe_hashtblPtr, mHandle);
    if (!hashEntryPtr) {
        if (interp) {
            Tcl_Obj *resultObj = Tcl_GetObjResult(interp);
            Tcl_AppendStringsToObj(resultObj, "invalid handle ", mHandle,
                                   (char *)NULL);
        }

        return TCL_ERROR;
    }

    pStmt = Tcl_GetHashValue(hashEntryPtr);
    if (pStmt->statement == NULL) {
        return TCL_ERROR;
    }

    switch ((enum STMT_enum)choice) {

    case STMT_BIND: {
        char *type = NULL;
        Tcl_Size len = 0;
        Tcl_WideInt index = 0;
        char *errorStr = NULL;

        if (objc == 5) {
            if (Tcl_GetWideIntFromObj(interp, objv[2], &index) != TCL_OK) {
                return TCL_ERROR;
            }

            if (index < 0) {
                return TCL_ERROR;
            }

            type = Tcl_GetStringFromObj(objv[3], &len);

            if (!type || len < 1) {
                return TCL_ERROR;
            }
        } else {
            Tcl_WrongNumArgs(interp, 2, objv, "index type value");
            return TCL_ERROR;
        }

        if (strcmp(type, "bool") == 0) {
            int value = 0;
            int8_t data = 0;

            if (Tcl_GetBooleanFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            data = (int8_t)value;
            errorStr = monetdbe_bind(pStmt->statement, &data, index);
        } else if (strcmp(type, "tinyint") == 0) {
            int value = 0;
            int8_t data = 0;

            if (Tcl_GetIntFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            data = (int8_t)value;
            errorStr = monetdbe_bind(pStmt->statement, &data, index);
        } else if (strcmp(type, "smallint") == 0) {
            int value = 0;
            int16_t data = 0;

            if (Tcl_GetIntFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            data = (int16_t)value;
            errorStr = monetdbe_bind(pStmt->statement, &data, index);
        } else if (strcmp(type, "integer") == 0) {
            int value = 0;

            if (Tcl_GetIntFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            errorStr = monetdbe_bind(pStmt->statement, &value, index);
        } else if (strcmp(type, "bigint") == 0) {
            Tcl_WideInt value = 0;

            if (Tcl_GetWideIntFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            errorStr = monetdbe_bind(pStmt->statement, &value, index);
        } else if (strcmp(type, "float") == 0) {
            double value = 0.0f;

            if (Tcl_GetDoubleFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            /*
             * If change to float the behavior is strange,
             * so I pass double to MonetDB Embedded.
             */
            errorStr = monetdbe_bind(pStmt->statement, &value, index);
        } else if (strcmp(type, "double") == 0) {
            double value = 0;

            if (Tcl_GetDoubleFromObj(interp, objv[4], &value) != TCL_OK) {
                return TCL_ERROR;
            }

            errorStr = monetdbe_bind(pStmt->statement, &value, index);
        } else if (strcmp(type, "string") == 0) {
            char *value = NULL;
            Tcl_Size length = 0;

            value = Tcl_GetStringFromObj(objv[4], &length);
            if (!value || length < 1) {
                return TCL_ERROR;
            }

            errorStr = monetdbe_bind(pStmt->statement, value, index);
        } else if (strcmp(type, "time") == 0) {
            char *value = NULL;
            Tcl_Size length = 0;
            unsigned int ms;
            unsigned char seconds;
            unsigned char minutes;
            unsigned char hours;
            monetdbe_data_time t;
            int rnum = 0;

            value = Tcl_GetStringFromObj(objv[4], &length);
            if (!value || length < 1) {
                return TCL_ERROR;
            }

            rnum = sscanf(value, "%hhd:%hhd:%hhd.%d", &hours, &minutes, &seconds, &ms);
            if (rnum != 4) {
                return TCL_ERROR;
            }

            t.ms = ms;
            t.seconds = seconds;
            t.minutes = minutes;
            t.hours = hours;
            errorStr = monetdbe_bind(pStmt->statement, &t, index);
        } else if (strcmp(type, "date") == 0) {
            char *value = NULL;
            Tcl_Size length = 0;
            unsigned char day = 0;
            unsigned char month = 0;
            short year = 0;
            monetdbe_data_date d;
            int rnum = 0;

            value = Tcl_GetStringFromObj(objv[4], &length);
            if (!value || length < 1) {
                return TCL_ERROR;
            }

            rnum = sscanf(value, "%hd-%hhd-%hhd", &year, &month, &day);
            if (rnum != 3) {
                return TCL_ERROR;
            }

            d.day = day;
            d.month = month;
            d.year = year;
            errorStr = monetdbe_bind(pStmt->statement, &d, index);
        } else if (strcmp(type, "timestamp") == 0) {
            char *value = NULL;
            Tcl_Size length = 0;
            unsigned int ms;
            unsigned char seconds;
            unsigned char minutes;
            unsigned char hours;
            unsigned char day = 0;
            unsigned char month = 0;
            short year = 0;
            monetdbe_data_timestamp tt;
            int rnum = 0;

            value = Tcl_GetStringFromObj(objv[4], &length);
            if (!value || length < 1) {
                return TCL_ERROR;
            }

            rnum = sscanf(value, "%hd-%hhd-%hhd %hhd:%hhd:%hhd.%d", &year, &month,
                   &day, &hours, &minutes, &seconds, &ms);
            if (rnum != 7) {
                return TCL_ERROR;
            }

            tt.time.ms = ms;
            tt.time.seconds = seconds;
            tt.time.minutes = minutes;
            tt.time.hours = hours;
            tt.date.day = day;
            tt.date.month = month;
            tt.date.year = year;
            errorStr = monetdbe_bind(pStmt->statement, &tt, index);
        } else if (strcmp(type, "blob") == 0) {
            char *value = NULL;
            Tcl_Size length = 0;
            monetdbe_data_blob data;

            value = Tcl_GetStringFromObj(objv[4], &length);
            if (!value || length < 1) {
                return TCL_ERROR;
            }

            data.size = length;
            data.data = value;
            errorStr = monetdbe_bind(pStmt->statement, &data, index);
        } else {
            Tcl_SetResult(interp, "unknown type", NULL);
            return TCL_ERROR;
        }

        if (errorStr != NULL) {
            Tcl_SetResult(interp, errorStr, NULL);
            return TCL_ERROR;
        }

        break;
    }

    case STMT_EXECUTE: {
        char *errorStr = NULL;
        monetdbe_cnt affected_rows;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        /*
         * Clean previous result
         */
        if (pStmt->columns) {
            for (size_t i = 0; i < pStmt->nrows; i++) {
                if (pStmt->columns[i])
                    free(pStmt->columns[i]);
            }

            free(pStmt->columns);
            pStmt->columns = NULL;
        }

        if (pStmt->result) {
            monetdbe_cleanup_result(pDb->db, pStmt->result);

            pStmt->result = 0;
            pStmt->affected_rows = 0;
            pStmt->index = 0;
            pStmt->nrows = 0;
            pStmt->ncols = 0;
        }

        errorStr = monetdbe_execute(pStmt->statement, &(pStmt->result),
                                    &affected_rows);
        if (errorStr != NULL) {
            Tcl_SetResult(interp, errorStr, NULL);

            monetdbe_cleanup_result(pDb->db, pStmt->result);
            pStmt->result = 0;
            pStmt->affected_rows = 0;
            pStmt->index = 0;
            pStmt->nrows = 0;
            pStmt->ncols = 0;

            return TCL_ERROR;
        } else {
            pStmt->index = 0;
            pStmt->affected_rows = affected_rows;
            pStmt->nrows = pStmt->result->nrows;
            pStmt->ncols = pStmt->result->ncols;

            if (pStmt->nrows > 0 && pStmt->ncols > 0) {
                pStmt->columns = (monetdbe_column **)malloc(
                    pStmt->nrows * sizeof(monetdbe_column *));
                if (!pStmt->columns) {
                    monetdbe_cleanup_result(pDb->db, pStmt->result);
                    pStmt->result = 0;
                    pStmt->affected_rows = 0;
                    pStmt->index = 0;
                    pStmt->nrows = 0;
                    pStmt->ncols = 0;

                    return TCL_ERROR;
                }

                if (pStmt->columns) {
                    for (size_t i = 0; i < pStmt->nrows; i++) {
                        pStmt->columns[i] = (monetdbe_column *)malloc(
                            pStmt->ncols * sizeof(monetdbe_column));
                    }
                }

                for (size_t r = 0; r < pStmt->nrows; r++) {
                    for (size_t c = 0; c < pStmt->ncols; c++) {
                        monetdbe_column *rcol = NULL;
                        errorStr =
                            monetdbe_result_fetch(pStmt->result, &rcol, c);

                        if (errorStr != NULL) {
                            if (pStmt->columns) {
                                for (size_t i = 0; i < pStmt->nrows; i++) {
                                    if (pStmt->columns[i])
                                        free(pStmt->columns[i]);
                                }

                                free(pStmt->columns);
                                pStmt->columns = NULL;
                            }

                            monetdbe_cleanup_result(pDb->db, pStmt->result);
                            pStmt->result = 0;
                            pStmt->affected_rows = 0;
                            pStmt->index = 0;
                            pStmt->nrows = 0;
                            pStmt->ncols = 0;

                            Tcl_AppendResult(interp, errorStr, (char *)0);
                            return TCL_ERROR;
                        }

                        memcpy(&(pStmt->columns[r][c]), rcol,
                               sizeof(monetdbe_column));
                    }
                }
            }
        }

        break;
    }

    case STMT_FETCH_ROW_LIST: {
        Tcl_Obj *pResultStr;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        if (pStmt->nrows > 0 && pStmt->ncols > 0 &&
            pStmt->index < pStmt->nrows) {
            pResultStr = Tcl_NewListObj(0, NULL);
            monetdbe_column *rcol;

            for (size_t c = 0; c < pStmt->ncols; c++) {
                rcol = pStmt->columns[pStmt->index] + c;

                switch (rcol->type) {

                case monetdbe_bool: {
                    monetdbe_column_bool *col = (monetdbe_column_bool *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewBooleanObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int8_t: {
                    monetdbe_column_int8_t *col =
                        (monetdbe_column_int8_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int16_t: {
                    monetdbe_column_int16_t *col =
                        (monetdbe_column_int16_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int32_t: {
                    monetdbe_column_int32_t *col =
                        (monetdbe_column_int32_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int64_t: {
                    monetdbe_column_int64_t *col =
                        (monetdbe_column_int64_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewWideIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_float: {
                    monetdbe_column_float *col = (monetdbe_column_float *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewDoubleObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_double: {
                    monetdbe_column_double *col =
                        (monetdbe_column_double *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewDoubleObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_str: {
                    monetdbe_column_str *col = (monetdbe_column_str *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewStringObj(col->data[pStmt->index], -1));
                    }
                    break;
                }

                case monetdbe_date: {
                    monetdbe_column_date *col = (monetdbe_column_date *)rcol;
                    if (col->data[pStmt->index].year == col->null_value.year &&
                        col->data[pStmt->index].month ==
                            col->null_value.month &&
                        col->data[pStmt->index].day == col->null_value.day) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        char buffer[24];
                        sprintf(buffer, "%d-%d-%d",
                                col->data[pStmt->index].year,
                                col->data[pStmt->index].month,
                                col->data[pStmt->index].day);
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj(buffer, -1));
                    }
                    break;
                }

                case monetdbe_time: {
                    monetdbe_column_time *col = (monetdbe_column_time *)rcol;
                    if (col->data[pStmt->index].hours ==
                            col->null_value.hours &&
                        col->data[pStmt->index].minutes ==
                            col->null_value.minutes &&
                        col->data[pStmt->index].seconds ==
                            col->null_value.seconds &&
                        col->data[pStmt->index].ms == col->null_value.ms) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        char buffer[32];
                        sprintf(buffer, "%d:%d:%d.%d",
                                col->data[pStmt->index].hours,
                                col->data[pStmt->index].minutes,
                                col->data[pStmt->index].seconds,
                                col->data[pStmt->index].ms);
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj(buffer, -1));
                    }
                    break;
                }

                case monetdbe_timestamp: {
                    monetdbe_column_timestamp *col =
                        (monetdbe_column_timestamp *)rcol;
                    if ((col->data[pStmt->index].date.year ==
                             col->null_value.date.year &&
                         col->data[pStmt->index].date.month ==
                             col->null_value.date.month &&
                         col->data[pStmt->index].date.day ==
                             col->null_value.date.day) &&
                        (col->data[pStmt->index].time.hours ==
                             col->null_value.time.hours &&
                         col->data[pStmt->index].time.minutes ==
                             col->null_value.time.minutes &&
                         col->data[pStmt->index].time.seconds ==
                             col->null_value.time.seconds &&
                         col->data[pStmt->index].time.ms ==
                             col->null_value.time.ms)) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        char buffer[64];
                        sprintf(buffer, "%d-%d-%d %d:%d:%d.%d",
                                col->data[pStmt->index].date.year,
                                col->data[pStmt->index].date.month,
                                col->data[pStmt->index].date.day,
                                col->data[pStmt->index].time.hours,
                                col->data[pStmt->index].time.minutes,
                                col->data[pStmt->index].time.seconds,
                                col->data[pStmt->index].time.ms);
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj(buffer, -1));
                    }
                    break;
                }

                case monetdbe_blob: {
                    monetdbe_column_blob *col = (monetdbe_column_blob *)rcol;
                    if (!col->data[pStmt->index].data) {
                        Tcl_ListObjAppendElement(interp, pResultStr,
                                                 Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_DString hexstring;
                        char hexit[] = "0123456789abcdef";

                        Tcl_DStringInit(&hexstring);

                        for (size_t i = 0; i < col->data[pStmt->index].size;
                             i++) {
                            int hval =
                                (col->data[pStmt->index].data[i] >> 4) & 15;
                            int lval = col->data[pStmt->index].data[i] & 15;
                            char buffer[3];
                            sprintf(buffer, "%c%c", hexit[hval], hexit[lval]);
                            Tcl_DStringAppend(&hexstring, buffer,
                                              strlen(buffer));
                        }

                        Tcl_ListObjAppendElement(
                            interp, pResultStr,
                            Tcl_NewStringObj(Tcl_DStringValue(&hexstring), -1));

                        Tcl_DStringFree(&hexstring);
                    }
                    break;
                }

                default: {
                    Tcl_AppendResult(interp, "Not supported data type",
                                     (char *)0);
                    return TCL_ERROR;
                }
                }
            }

            pStmt->index++;
            Tcl_SetObjResult(interp, pResultStr);
        }

        break;
    }

    case STMT_FETCH_ROW_DICT: {
        Tcl_Obj *pResultStr;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        if (pStmt->nrows > 0 && pStmt->ncols > 0 &&
            pStmt->index < pStmt->nrows) {
            pResultStr = Tcl_NewListObj(0, NULL);
            monetdbe_column *rcol;

            for (size_t c = 0; c < pStmt->ncols; c++) {
                rcol = pStmt->columns[pStmt->index] + c;

                switch (rcol->type) {

                case monetdbe_bool: {
                    monetdbe_column_bool *col = (monetdbe_column_bool *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(
                            interp, pResultStr, Tcl_NewStringObj(col->name, -1),
                            Tcl_NewBooleanObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int8_t: {
                    monetdbe_column_int8_t *col =
                        (monetdbe_column_int8_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(interp, pResultStr,
                                       Tcl_NewStringObj(col->name, -1),
                                       Tcl_NewIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int16_t: {
                    monetdbe_column_int16_t *col =
                        (monetdbe_column_int16_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(interp, pResultStr,
                                       Tcl_NewStringObj(col->name, -1),
                                       Tcl_NewIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int32_t: {
                    monetdbe_column_int32_t *col =
                        (monetdbe_column_int32_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(interp, pResultStr,
                                       Tcl_NewStringObj(col->name, -1),
                                       Tcl_NewIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_int64_t: {
                    monetdbe_column_int64_t *col =
                        (monetdbe_column_int64_t *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(
                            interp, pResultStr, Tcl_NewStringObj(col->name, -1),
                            Tcl_NewWideIntObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_float: {
                    monetdbe_column_float *col = (monetdbe_column_float *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(
                            interp, pResultStr, Tcl_NewStringObj(col->name, -1),
                            Tcl_NewDoubleObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_double: {
                    monetdbe_column_double *col =
                        (monetdbe_column_double *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(
                            interp, pResultStr, Tcl_NewStringObj(col->name, -1),
                            Tcl_NewDoubleObj(col->data[pStmt->index]));
                    }
                    break;
                }

                case monetdbe_str: {
                    monetdbe_column_str *col = (monetdbe_column_str *)rcol;
                    if (col->data[pStmt->index] == col->null_value) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DictObjPut(
                            interp, pResultStr, Tcl_NewStringObj(col->name, -1),
                            Tcl_NewStringObj(col->data[pStmt->index], -1));
                    }
                    break;
                }

                case monetdbe_date: {
                    monetdbe_column_date *col = (monetdbe_column_date *)rcol;
                    if (col->data[pStmt->index].year == col->null_value.year &&
                        col->data[pStmt->index].month ==
                            col->null_value.month &&
                        col->data[pStmt->index].day == col->null_value.day) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        char buffer[24];
                        sprintf(buffer, "%d-%d-%d",
                                col->data[pStmt->index].year,
                                col->data[pStmt->index].month,
                                col->data[pStmt->index].day);
                        Tcl_DictObjPut(interp, pResultStr,
                                       Tcl_NewStringObj(col->name, -1),
                                       Tcl_NewStringObj(buffer, -1));
                    }
                    break;
                }

                case monetdbe_time: {
                    monetdbe_column_time *col = (monetdbe_column_time *)rcol;
                    if (col->data[pStmt->index].hours ==
                            col->null_value.hours &&
                        col->data[pStmt->index].minutes ==
                            col->null_value.minutes &&
                        col->data[pStmt->index].seconds ==
                            col->null_value.seconds &&
                        col->data[pStmt->index].ms == col->null_value.ms) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        char buffer[32];
                        sprintf(buffer, "%d:%d:%d.%d",
                                col->data[pStmt->index].hours,
                                col->data[pStmt->index].minutes,
                                col->data[pStmt->index].seconds,
                                col->data[pStmt->index].ms);
                        Tcl_DictObjPut(interp, pResultStr,
                                       Tcl_NewStringObj(col->name, -1),
                                       Tcl_NewStringObj(buffer, -1));
                    }
                    break;
                }

                case monetdbe_timestamp: {
                    monetdbe_column_timestamp *col =
                        (monetdbe_column_timestamp *)rcol;
                    if ((col->data[pStmt->index].date.year ==
                             col->null_value.date.year &&
                         col->data[pStmt->index].date.month ==
                             col->null_value.date.month &&
                         col->data[pStmt->index].date.day ==
                             col->null_value.date.day) &&
                        (col->data[pStmt->index].time.hours ==
                             col->null_value.time.hours &&
                         col->data[pStmt->index].time.minutes ==
                             col->null_value.time.minutes &&
                         col->data[pStmt->index].time.seconds ==
                             col->null_value.time.seconds &&
                         col->data[pStmt->index].time.ms ==
                             col->null_value.time.ms)) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        char buffer[64];
                        sprintf(buffer, "%d-%d-%d %d:%d:%d.%d",
                                col->data[pStmt->index].date.year,
                                col->data[pStmt->index].date.month,
                                col->data[pStmt->index].date.day,
                                col->data[pStmt->index].time.hours,
                                col->data[pStmt->index].time.minutes,
                                col->data[pStmt->index].time.seconds,
                                col->data[pStmt->index].time.ms);
                        Tcl_DictObjPut(interp, pResultStr,
                                       Tcl_NewStringObj(col->name, -1),
                                       Tcl_NewStringObj(buffer, -1));
                    }
                    break;
                }

                case monetdbe_blob: {
                    monetdbe_column_blob *col = (monetdbe_column_blob *)rcol;
                    if (!col->data[pStmt->index].data) {
                        /*
                         * If a value is NULL, the returned dictionary for the
                         * row will not contain the corresponding key.
                         */
                        continue;
                    } else {
                        Tcl_DString hexstring;
                        char hexit[] = "0123456789abcdef";

                        Tcl_DStringInit(&hexstring);

                        for (size_t i = 0; i < col->data[pStmt->index].size;
                             i++) {
                            int hval =
                                (col->data[pStmt->index].data[i] >> 4) & 15;
                            int lval = col->data[pStmt->index].data[i] & 15;
                            char buffer[3];
                            sprintf(buffer, "%c%c", hexit[hval], hexit[lval]);
                            Tcl_DStringAppend(&hexstring, buffer,
                                              strlen(buffer));
                        }

                        Tcl_DictObjPut(
                            interp, pResultStr, Tcl_NewStringObj(col->name, -1),
                            Tcl_NewStringObj(Tcl_DStringValue(&hexstring), -1));

                        Tcl_DStringFree(&hexstring);
                    }
                    break;
                }

                default: {
                    Tcl_AppendResult(interp, "Not supported data type",
                                     (char *)0);
                    return TCL_ERROR;
                }
                }
            }

            pStmt->index++;
            Tcl_SetObjResult(interp, pResultStr);
        }

        break;
    }

    case STMT_GET_ROW_COUNT: {
        Tcl_Obj *return_obj;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        return_obj = Tcl_NewWideIntObj(pStmt->nrows);
        Tcl_SetObjResult(interp, return_obj);

        break;
    }

    case STMT_GET_COL_COUNT: {
        Tcl_Obj *return_obj;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        return_obj = Tcl_NewWideIntObj(pStmt->ncols);
        Tcl_SetObjResult(interp, return_obj);

        break;
    }

    case STMT_ROW_AFFECTED: {
        Tcl_Obj *return_obj;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        return_obj = Tcl_NewWideIntObj(pStmt->affected_rows);
        Tcl_SetObjResult(interp, return_obj);

        break;
    }

    case STMT_COLUMNS: {
        Tcl_Obj *pResultStr;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        pResultStr = Tcl_NewListObj(0, NULL);

        if (pStmt->nrows > 0 && pStmt->ncols > 0 && pStmt->columns != NULL) {
            for (size_t c = 0; c < pStmt->ncols; c++) {
                Tcl_ListObjAppendElement(
                    interp, pResultStr,
                    Tcl_NewStringObj(pStmt->columns[0][c].name, -1));
            }
        }

        Tcl_SetObjResult(interp, pResultStr);
        break;
    }

    case STMT_CLOSE: {
        char *errorStr = NULL;
        Tcl_Obj *return_obj;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        if (pStmt->columns) {
            for (size_t i = 0; i < pStmt->nrows; i++) {
                if (pStmt->columns[i])
                    free(pStmt->columns[i]);
            }

            free(pStmt->columns);
            pStmt->columns = NULL;
        }

        /*
         * Clean results.
         */
        if (pStmt->result) {
            errorStr = monetdbe_cleanup_result(pDb->db, pStmt->result);
            pStmt->result = 0;
            pStmt->index = 0;
            pStmt->nrows = 0;
            pStmt->ncols = 0;
            pStmt->affected_rows = 0;
        }

        errorStr = monetdbe_cleanup_statement(pDb->db, pStmt->statement);
        if (errorStr != NULL) {
            return_obj = Tcl_NewBooleanObj(0);
        } else {
            return_obj = Tcl_NewBooleanObj(1);
        }

        Tcl_Free((char *)pStmt);
        pStmt = 0;

        Tcl_MutexLock(&myMutex);
        if (hashEntryPtr)
            Tcl_DeleteHashEntry(hashEntryPtr);
        Tcl_MutexUnlock(&myMutex);

        Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], 0));
        Tcl_SetObjResult(interp, return_obj);

        break;
    }

    } /* End of the SWITCH statement */

    return rc;
}


static int DbObjCmd(void *cd, Tcl_Interp *interp, int objc,
                    Tcl_Obj *const *objv) {
    MonetDBEDATA *pDb = (MonetDBEDATA *)cd;
    int choice;
    int rc = TCL_OK;

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)Tcl_GetThreadData(
        &dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->initialized == 0) {
        tsdPtr->initialized = 1;
        tsdPtr->monetdbe_hashtblPtr =
            (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(tsdPtr->monetdbe_hashtblPtr, TCL_STRING_KEYS);
    }

    static const char *DB_strs[] = {
        "monetdbe_version",
        "getAutocommit",
        "setAutocommit",
        "in_transaction",
        "dump_database",
        "dump_table",
        "query",
        "prepare",
        "close",
        0
    };

    enum DB_enum {
        DB_MONET_VERSION,
        DB_GETAUTOCOMMIT,
        DB_SETAUTOCOMMIT,
        DB_IN_TRANSACTION,
        DB_DUMP_DATABASE,
        DB_DUMP_TABLE,
        DB_QUERY,
        DB_PREPARE,
        DB_CLOSE,
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "SUBCOMMAND ...");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], DB_strs, "option", 0, &choice)) {
        return TCL_ERROR;
    }

    if (pDb == NULL) {
        return TCL_ERROR;
    }

    switch ((enum DB_enum)choice) {

    case DB_MONET_VERSION: {
        Tcl_Obj *return_obj;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        return_obj = Tcl_NewStringObj(monetdbe_version(), -1);
        Tcl_SetObjResult(interp, return_obj);
        break;
    }

    case DB_GETAUTOCOMMIT: {
        int result = 0;
        char *err = NULL;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        err = monetdbe_get_autocommit(pDb->db, &result);

        if (err != NULL) {
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
        break;
    }

    case DB_SETAUTOCOMMIT: {
        int autocommit = 0;
        char *err = NULL;
        Tcl_Obj *return_obj;

        if (objc == 3) {
            if (Tcl_GetIntFromObj(interp, objv[2], &autocommit) != TCL_OK) {
                return TCL_ERROR;
            }

            if (autocommit < 0) {
                return TCL_ERROR;
            }
        } else {
            Tcl_WrongNumArgs(interp, 2, objv, "autocommit");
            return TCL_ERROR;
        }

        if ((err = monetdbe_set_autocommit(pDb->db, autocommit)) != NULL) {
            return_obj = Tcl_NewBooleanObj(0);
            rc = TCL_ERROR;
        } else {
            return_obj = Tcl_NewBooleanObj(1);
        }

        Tcl_SetObjResult(interp, return_obj);
        break;
    }

    case DB_IN_TRANSACTION: {
        int result = 0;

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        result = monetdbe_in_transaction(pDb->db);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(result));

        break;
    }

    case DB_DUMP_DATABASE: {
        char *zPath = NULL;
        char *errorStr = NULL;
        Tcl_Size len = 0;

        if (objc == 3) {
            zPath = Tcl_GetStringFromObj(objv[2], &len);

            if (!zPath || len < 1) {
                return TCL_ERROR;
            }
        } else {
            Tcl_WrongNumArgs(interp, 2, objv, "backupfile");
            return TCL_ERROR;
        }

        errorStr = monetdbe_dump_database(pDb->db, zPath);
        if (errorStr != NULL) {
            Tcl_SetResult(interp, errorStr, NULL);
            return TCL_ERROR;
        }

        break;
    }

    case DB_DUMP_TABLE: {
        char *schema_name = NULL, *table_name = NULL, *zPath = NULL;
        char *errorStr = NULL;
        Tcl_Size len = 0;

        if (objc == 5) {
            schema_name = Tcl_GetStringFromObj(objv[2], &len);

            if (!schema_name || len < 1) {
                return TCL_ERROR;
            }

            table_name = Tcl_GetStringFromObj(objv[3], &len);

            if (!table_name || len < 1) {
                return TCL_ERROR;
            }

            zPath = Tcl_GetStringFromObj(objv[4], &len);

            if (!zPath || len < 1) {
                return TCL_ERROR;
            }
        } else {
            Tcl_WrongNumArgs(interp, 2, objv,
                             "schema_name table_name backupfile");
            return TCL_ERROR;
        }

        errorStr = monetdbe_dump_table(pDb->db, schema_name, table_name, zPath);
        if (errorStr != NULL) {
            Tcl_SetResult(interp, errorStr, NULL);
            return TCL_ERROR;
        }

        break;
    }

    case DB_QUERY: {
        char *zQuery = NULL;
        char *errorStr = NULL;
        Tcl_Size len = 0;
        Tcl_Obj *pResultStr = NULL;
        monetdbe_cnt affected_rows;
        monetdbe_result *result;

        if (objc == 3) {
            zQuery = Tcl_GetStringFromObj(objv[2], &len);

            if (!zQuery || len < 1) {
                return TCL_ERROR;
            }
        } else {
            Tcl_WrongNumArgs(interp, 2, objv, "SQL_String");
            return TCL_ERROR;
        }

        errorStr = monetdbe_query(pDb->db, zQuery, &result, &affected_rows);
        pResultStr = Tcl_NewListObj(0, NULL);
        if (errorStr != NULL) {
            Tcl_SetResult(interp, errorStr, NULL);

            monetdbe_cleanup_result(pDb->db, result);
            result = 0;

            return TCL_ERROR;
        } else {
            if (result->nrows > 0 && result->ncols > 0) {
                monetdbe_column *rcol;

                for (size_t r = 0; r < result->nrows; r++) {
                    Tcl_Obj *pColResultStr = Tcl_NewListObj(0, NULL);

                    for (size_t c = 0; c < result->ncols; c++) {
                        errorStr = monetdbe_result_fetch(result, &rcol, c);
                        if (errorStr != NULL) {
                            monetdbe_cleanup_result(pDb->db, result);
                            result = 0;

                            Tcl_AppendResult(interp, errorStr, (char *)0);
                            return TCL_ERROR;
                        }

                        switch (rcol->type) {

                        case monetdbe_bool: {
                            monetdbe_column_bool *col =
                                (monetdbe_column_bool *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewBooleanObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_int8_t: {
                            monetdbe_column_int8_t *col =
                                (monetdbe_column_int8_t *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewIntObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_int16_t: {
                            monetdbe_column_int16_t *col =
                                (monetdbe_column_int16_t *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewIntObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_int32_t: {
                            monetdbe_column_int32_t *col =
                                (monetdbe_column_int32_t *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewIntObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_int64_t: {
                            monetdbe_column_int64_t *col =
                                (monetdbe_column_int64_t *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewWideIntObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_float: {
                            monetdbe_column_float *col =
                                (monetdbe_column_float *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewDoubleObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_double: {
                            monetdbe_column_double *col =
                                (monetdbe_column_double *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewDoubleObj(col->data[r]));
                            }
                            break;
                        }

                        case monetdbe_str: {
                            monetdbe_column_str *col =
                                (monetdbe_column_str *)rcol;
                            if (col->data[r] == col->null_value) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj(col->data[r], -1));
                            }
                            break;
                        }

                        case monetdbe_date: {
                            monetdbe_column_date *col =
                                (monetdbe_column_date *)rcol;
                            if (col->data[r].year == col->null_value.year &&
                                col->data[r].month == col->null_value.month &&
                                col->data[r].day == col->null_value.day) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                char buffer[24];
                                sprintf(buffer, "%d-%d-%d", col->data[r].year,
                                        col->data[r].month, col->data[r].day);
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj(buffer, -1));
                            }
                            break;
                        }

                        case monetdbe_time: {
                            monetdbe_column_time *col =
                                (monetdbe_column_time *)rcol;
                            if (col->data[r].hours == col->null_value.hours &&
                                col->data[r].minutes ==
                                    col->null_value.minutes &&
                                col->data[r].seconds ==
                                    col->null_value.seconds &&
                                col->data[r].ms == col->null_value.ms) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                char buffer[32];
                                sprintf(buffer, "%d:%d:%d.%d",
                                        col->data[r].hours,
                                        col->data[r].minutes,
                                        col->data[r].seconds, col->data[r].ms);
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj(buffer, -1));
                            }
                            break;
                        }

                        case monetdbe_timestamp: {
                            monetdbe_column_timestamp *col =
                                (monetdbe_column_timestamp *)rcol;
                            if ((col->data[r].date.year ==
                                     col->null_value.date.year &&
                                 col->data[r].date.month ==
                                     col->null_value.date.month &&
                                 col->data[r].date.day ==
                                     col->null_value.date.day) &&
                                (col->data[r].time.hours ==
                                     col->null_value.time.hours &&
                                 col->data[r].time.minutes ==
                                     col->null_value.time.minutes &&
                                 col->data[r].time.seconds ==
                                     col->null_value.time.seconds &&
                                 col->data[r].time.ms ==
                                     col->null_value.time.ms)) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                char buffer[64];
                                sprintf(buffer, "%d-%d-%d %d:%d:%d.%d",
                                        col->data[r].date.year,
                                        col->data[r].date.month,
                                        col->data[r].date.day,
                                        col->data[r].time.hours,
                                        col->data[r].time.minutes,
                                        col->data[r].time.seconds,
                                        col->data[r].time.ms);
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj(buffer, -1));
                            }
                            break;
                        }

                        case monetdbe_blob: {
                            monetdbe_column_blob *col =
                                (monetdbe_column_blob *)rcol;
                            if (!col->data[r].data) {
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj("", -1));
                            } else {
                                Tcl_DString hexstring;
                                char hexit[] = "0123456789abcdef";

                                Tcl_DStringInit(&hexstring);

                                for (size_t i = 0; i < col->data[r].size; i++) {
                                    int hval = (col->data[r].data[i] >> 4) & 15;
                                    int lval = col->data[r].data[i] & 15;
                                    char buffer[3];
                                    sprintf(buffer, "%c%c", hexit[hval],
                                            hexit[lval]);
                                    Tcl_DStringAppend(&hexstring, buffer,
                                                      strlen(buffer));
                                }

                                /*
                                 * Maybe not good if output data onto the screen
                                 */
                                Tcl_ListObjAppendElement(
                                    interp, pColResultStr,
                                    Tcl_NewStringObj(
                                        Tcl_DStringValue(&hexstring), -1));

                                Tcl_DStringFree(&hexstring);
                            }
                            break;
                        }

                        default: {
                            monetdbe_cleanup_result(pDb->db, result);
                            result = 0;

                            Tcl_AppendResult(interp, "Not supported data type",
                                             (char *)0);
                            return TCL_ERROR;
                        }
                        }
                    }

                    Tcl_ListObjAppendElement(interp, pResultStr, pColResultStr);
                }
            }

            monetdbe_cleanup_result(pDb->db, result);
            result = 0;
        }

        Tcl_SetObjResult(interp, pResultStr);

        break;
    }

    case DB_PREPARE: {
        char *zQuery = NULL;
        char *errorStr = NULL;
        Tcl_Size len = 0;
        Tcl_HashEntry *newHashEntryPtr;
        char handleName[16 + TCL_INTEGER_SPACE];
        Tcl_Obj *pResultStr = NULL;
        MonetDBEStmt *pStmt;
        int newvalue;

        if (objc == 3) {
            zQuery = Tcl_GetStringFromObj(objv[2], &len);

            if (!zQuery || len < 1) {
                return TCL_ERROR;
            }
        } else {
            Tcl_WrongNumArgs(interp, 2, objv, "SQL_String");
            return TCL_ERROR;
        }

        pStmt = (MonetDBEStmt *)Tcl_Alloc(sizeof(*pStmt));
        if (pStmt == 0) {
            Tcl_SetResult(interp, (char *)"malloc failed", TCL_STATIC);
            return TCL_ERROR;
        }

        memset(pStmt, 0, sizeof(*pStmt));
        pStmt->index = 0;
        pStmt->nrows = 0;
        pStmt->ncols = 0;
        pStmt->affected_rows = 0;

        /*
         * Pass NULL since I want to get results later.
         */
        errorStr = monetdbe_prepare(pDb->db, zQuery, &(pStmt->statement), NULL);
        if (errorStr != NULL) {
            Tcl_SetResult(interp, errorStr, NULL);

            Tcl_Free((char *)pStmt);
            pStmt = 0;

            return TCL_ERROR;
        } else {
            Tcl_MutexLock(&myMutex);
            sprintf(handleName, "monete_stat%d", tsdPtr->stmt_count++);

            pResultStr = Tcl_NewStringObj(handleName, -1);

            newHashEntryPtr = Tcl_CreateHashEntry(tsdPtr->monetdbe_hashtblPtr,
                                                  handleName, &newvalue);
            Tcl_SetHashValue(newHashEntryPtr, pStmt);
            Tcl_MutexUnlock(&myMutex);

            Tcl_CreateObjCommand(interp, handleName,
                                 (Tcl_ObjCmdProc *)MONETE_STMT, (char *)pDb,
                                 (Tcl_CmdDeleteProc *)NULL);
        }

        Tcl_SetObjResult(interp, pResultStr);

        break;
    }

    case DB_CLOSE: {
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, 0);
            return TCL_ERROR;
        }

        Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], 0));

        break;
    }

    } /* End of the SWITCH statement */

    return rc;
}


static int MONETDBE_MAIN(void *cd, Tcl_Interp *interp, int objc,
                         Tcl_Obj *const *objv) {
    MonetDBEDATA *p;
    const char *zArg;
    int i;
    char *url = NULL;
    int result;

    if (objc < 2 || (objc & 1) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv,
                         "HANDLE ?-url URL? ");
        return TCL_ERROR;
    }

    for (i = 2; i + 1 < objc; i += 2) {
        zArg = Tcl_GetStringFromObj(objv[i], 0);

        if (strcmp(zArg, "-url") == 0) {
            url = Tcl_GetStringFromObj(objv[i + 1], 0);

            /*
             * Handle in memory database case.
             */
            if (strcmp(url, ":memory:") == 0) {
                url = NULL;
            }
        } else {
            Tcl_AppendResult(interp, "unknown option: ", zArg, (char *)0);
            return TCL_ERROR;
        }
    }

    Tcl_MutexLock(&monetdbeMutex);
    if (monetdbeRefCount == 0) {
        if ((monetdbeLoadHandle = MonetDBEInitStubs(interp)) == NULL) {
            Tcl_MutexUnlock(&monetdbeMutex);
            return TCL_ERROR;
        }
    }
    ++monetdbeRefCount;
    Tcl_MutexUnlock(&monetdbeMutex);

    p = (MonetDBEDATA *)Tcl_Alloc(sizeof(*p));
    if (p == 0) {
        Tcl_SetResult(interp, (char *)"malloc failed", TCL_STATIC);
        return TCL_ERROR;
    }

    memset(p, 0, sizeof(*p));

    result = monetdbe_open(&(p->db), url, NULL);

    if (result != 0) {
        /*
         * It is a problem for me. Could I need to unload here?
         */
        Tcl_MutexLock(&monetdbeMutex);
        if (--monetdbeRefCount == 0) {
            Tcl_FSUnloadFile(NULL, monetdbeLoadHandle);
            monetdbeLoadHandle = NULL;
        }
        Tcl_MutexUnlock(&monetdbeMutex);

        if (p)
            Tcl_Free((char *)p);
        Tcl_SetResult(interp, "Open MonetDB/e database fail", NULL);
        return TCL_ERROR;
    }

    p->interp = interp;

    zArg = Tcl_GetStringFromObj(objv[1], 0);
    Tcl_CreateObjCommand(interp, zArg, DbObjCmd, (char *)p, DbDeleteCmd);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Monetdbe_Init --
 *
 *  Initialize the new package.  The string "Monetdbe" in the
 *  function name must match the PACKAGE declaration at the top of
 *  configure.ac.
 *
 * Results:
 *  A standard Tcl result
 *
 * Side effects:
 *  The Monetdbe package is created.
 *
 *----------------------------------------------------------------------
 */
#ifdef __cplusplus
extern "C" {
#endif                                          /* __cplusplus */
DLLEXPORT int Monetdbe_Init(Tcl_Interp *interp) /* Tcl interpreter */
{
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }

    /* Provide the current package */

    if (Tcl_PkgProvideEx(interp, PACKAGE_NAME, PACKAGE_VERSION, NULL) !=
        TCL_OK) {
        return TCL_ERROR;
    }

    /*
     *  Tcl_GetThreadData handles the auto-initialization of all data in
     *  the ThreadSpecificData to NULL at first time.
     */
    Tcl_MutexLock(&myMutex);
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)Tcl_GetThreadData(
        &dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->initialized == 0) {
        tsdPtr->initialized = 1;
        tsdPtr->monetdbe_hashtblPtr =
            (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(tsdPtr->monetdbe_hashtblPtr, TCL_STRING_KEYS);

        tsdPtr->stmt_count = 0;
    }
    Tcl_MutexUnlock(&myMutex);

    /* Add a thread exit handler to delete hash table */
    Tcl_CreateThreadExitHandler(MonetDBE_Thread_Exit, (ClientData)NULL);

    Tcl_CreateObjCommand(interp, "monetdbe", (Tcl_ObjCmdProc *)MONETDBE_MAIN,
                         (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

    return TCL_OK;
}
#ifdef __cplusplus
}
#endif /* __cplusplus */
