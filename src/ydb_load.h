/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ifndef YDB_LOAD_H
#define YDB_LOAD_H

#ident "Copyright (c) 2010-2013 Tokutek Inc.  All rights reserved."

/*  ydb functions used by loader
 */

// When the loader is created, it makes this call.
// For each dictionary to be loaded, replace old iname in directory
// with a newly generated iname.  This will also take a write lock
// on the directory entries.  The write lock will be released when
// the transaction of the loader is completed.
// If the transaction commits, the new inames are in place.
// If the transaction aborts, the old inames will be restored.
// The new inames are returned to the caller.  
// It is the caller's responsibility to free them.
// If "mark_as_loader" is true, then include a mark in the iname
// to indicate that the file is created by the brt loader.
// Return 0 on success (could fail if write lock not available).
int locked_load_inames(DB_ENV * env,
                       DB_TXN * txn,
                       int N,
                       DB * dbs[/*N*/],
                       char * new_inames_in_env[/*N*/], /* out */
                       LSN *load_lsn,
                       bool mark_as_loader);

#endif
