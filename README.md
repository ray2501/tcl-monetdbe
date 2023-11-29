tcl-monetdbe
=====

[MonetDB](https://www.monetdb.org/Home) is an open source column-oriented
database management system developed at the Centrum Wiskunde & Informatica
(CWI) in the Netherlands.

MonetDB Embedded (MonetDB/e) is the embedded version of the open source
MonetDB SQL column store engine. It encapsulates the full SQL functionality
offered by MonetDB.

Tcl-monetdbe is a Tcl extension by using MonetDB Embedded library.
I write this extension to research MonetDB/e.

[Tcl Database Connectivity (TDBC)](http://www.tcl.tk/man/tcl8.6/TdbcCmd/tdbc.htm)
is a common interface for Tcl programs to access SQL databases. Tcl-monetdbe's
TDBC interface (tdbc::monetdbe) is based on tcl-monetdbe extension. I write
Tcl-monetdbe's TDBC interface to study TDBC interface.

This extension is using [Tcl_LoadFile](https://www.tcl.tk/man/tcl/TclLib/Load.htm)
to load MonetDB/e library.
Before using this extension, please setup libmonetdbe path environment variable.
Below is an example on Windows platform:

    set PATH=C:\Program Files\MonetDB\MonetDB5\bin;%PATH%

Below is an example on Linux platform to setup LD_LIBRARY_PATH environment
variable (if need):

    export LD_LIBRARY_PATH=/usr/local/lib64:$LD_LIBRARY_PATH

This extension needs Tcl 8.6.


License
=====

This extension is Licensed under Mozilla Public License, Version 2.0.


UNIX BUILD
=====

Building under most UNIX systems is easy, just run the configure script and
then run make. For more information about the build process, see the
tcl/unix/README file in the Tcl src dist. The following minimal example will
install the extension in the /opt/tcl directory.

    $ cd tclmonetdb
    $ ./configure --prefix=/opt/tcl
    $ make
    $ make install

If you need setup directory containing tcl configuration (tclConfig.sh),
below is an example:

    $ cd tclmonetdb
    $ ./configure --with-tcl=/opt/activetcl/lib
    $ make
    $ make install


WINDOWS BUILD
=====

The recommended method to build extensions under windows is to use the
Msys + Mingw build process. This provides a Unix-style build while generating
native Windows binaries. Using the Msys + Mingw build tools means that you
can use the same configure script as per the Unix build to create a Makefile.

User can use MSYS/MinGW or MSYS2/MinGW-W64 to build tclmonetdb:

    $ ./configure --with-tcl=/c/tcl/lib
    $ make
    $ make install


Implement commands
=====

The interface to the MonetDB Embedded iibrary consists of single tcl command
named `monetdbe`. Once open a MonetDB/e database, it can be controlled using
methods of the HANDLE command.

monetdbe HANDLE ?-url -URL?  
HANDLE monet_version  
HANDLE getAutocommit  
HANDLE setAutocommit autocommit  
HANDLE in_transaction  
HANDLE dump_database backupfile  
HANDLE dump_table schema_name table_name backupfile  
HANDLE query SQL_String  
HANDLE prepare SQL_String  
HANDLE close  
STMT bind index type value  
STMT execute
STMT fetch_row_list  
STMT fetch_row_dict  
STMT get_row_count  
STMT get_col_count  
STMT rows_affected  
STMT get_columns   
STMT close  


`monetdbe` command options are used to open a MonetDB Embedded database.
Below is the option default value (if user does not specify):

| Option            | Type      | Default              | Additional description |
| :---------------- | :-------- | :------------------- | :--------------------- |
| url               | string    | (NULL)               | Default is in-memory mode


The option `-url` is a string for the db directory or `:memory:` for in-memory
mode.

By default, autocommit mode is on. User can use `setAutocommit` command to
switch autocommit mode off.

MonetDB/SQL supports a multi-statement transaction scheme marked by
START TRANSACTION and closed with either COMMIT or ROLLBACK. The session
variable AUTOCOMMIT can be set to true (default) if each SQL statement should
be considered an independent transaction.

In the AUTOCOMMIT mode, you can use START TRANSACTION and COMMIT/ROLLBACK to
indicate transactions containing multiple SQL statements. In this case,
AUTOCOMMIT is automatically disabled by a START TRANSACTION, and reenabled
by a COMMIT or ROLLBACK.

If AUTOCOMMIT mode is OFF, the START TRANSACTION is implicit, and you should
only use COMMIT/ROLLBACK.

`bind` supports below types:  
bool, tinyint, smallint, integer, bigint, float, double, string, time, date,
timestamp and blob.

When you add blob data by using `STMT bind` and `STMT execute`, it is not
necessary to encode these characters to a hex encoded string. It is
different if you query database and get the blob data, you get a hex
encoded string, and you need to decode it to characters.

And if you use `HANDLE query` command to insert into blob data to database,
you should encode blob data to a hex encoded string before you add data to
database. Tcl can use `binary encode hex` and `binary decode hex` to
encode/decode hex string.


TDBC commands
=====

tdbc::monetdbe::connection create db url ?-option value...?

Connection to a MonetDB/e database is established by invoking tdbc::monetdbe::connection
create, passing it the name to be used as a connection handle, followed by a url.
The url option is a string for the db directory or `:memory:` for in-memory mode.

The tdbc::monetdbe::connection create object command supports the -encoding,
-isolation and -readonly option (only gets the default setting).

MonetDB/e driver for TDBC implements a statement object that represents a SQL statement
in a database. Instances of this object are created by executing the prepare or
preparecall object command on a database connection.

The prepare object command against the connection accepts arbitrary SQL code to be
executed against the database.

The paramtype object command allows the script to specify the type and direction of
parameter transmission of a variable in a statement. Now MonetDB/e driver only specify
the type work.

MonetDB/e driver paramtype accepts below type:
bool, tinyint, smallint, integer, bigint, string, float, double,
date, time, timestamp, blob

The execute object command executes the statement.


Examples
=====

## tcl-monetdbe example

List of tables -

    package require monetdbe

    monetdbe db -url ":memory:"

    set stmt [db prepare "SELECT name FROM tables"]
    $stmt execute
    set count [$stmt get_row_count]
    for {set i 0} {$i < $count} {incr i} {
        set result [$stmt fetch_row_list]
        puts $result
    }

    $stmt close
    db close

Below is an example:

    package require monetdbe

    monetdbe db -url ":memory:"

    db query "DROP TABLE IF EXISTS mydata"
    db query "CREATE TABLE mydata (name varchar(32), birth date); "

    db query "START TRANSACTION"
    db query "INSERT INTO mydata VALUES ('danilo', '1989-04-05');"
    db query "INSERT INTO mydata VALUES ('orange', '2005-02-04');"
    db query "COMMIT;"

    set stmt [db prepare "INSERT INTO mydata VALUES (?, ?)"]
    $stmt bind 0 string arthur
    $stmt bind 1 date "1974-11-11"
    $stmt execute
    $stmt close

    set var [db query "select * FROM mydata"]
    set count [llength $var]
    puts "Reults:"
    for {set index 0} {$index < $count} {incr index} {
        set result [lindex $var $index]
        puts $result
    }

    set stmt [db prepare "select * from mydata"]
    $stmt execute
    puts "Reults:"
    set count [$stmt get_row_count]
    for {set i 0} {$i < $count} {incr i} {
        puts [$stmt fetch_row_list]
    }
    $stmt close

    db close

## Tcl Database Connectivity (TDBC) interface

Below is an exmaple:

    package require tdbc::monetdbe

    tdbc::monetdbe::connection create db ":memory:"

    set statement [db prepare "drop table if exists person"]
    $statement execute
    $statement close

    set statement [db prepare \
        {create table person (id integer not null, name varchar(40) not null, primary key(id))}]
    $statement execute
    $statement close

    db begintransaction

    set statement [db prepare {insert into person values(1, 'shujung')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(2, 'orange')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(3, 'jerry')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(4, 'shawn')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(5, 'kimber')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(6, 'wellpy')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(:id, :name)}]
    $statement paramtype id integer
    $statement paramtype name string

    set id 7
    set name mandy
    $statement execute

    set id 8
    set name conan
    $statement execute

    db commit

    set statement [db prepare {SELECT * FROM person}]

    $statement foreach row {
        puts [dict get $row id]
        puts [dict get $row name]
    }

    $statement close

    set statement [db prepare {DROP TABLE person}]
    $statement execute
    $statement close

    db close

