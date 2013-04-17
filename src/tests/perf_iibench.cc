/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "$Id$"

#include <db.h>
#include <portability/toku_atomic.h>

#include "test.h"
#include "threaded_stress_test_helpers.h"

//
// This test tries to emulate iibench at the ydb layer.
//
// The schema is simple:
// 8 byte primary key
// 8 byte key A
// 8 byte key B
// 8 byte key C
//
// There's one primary DB for the pk and three secondary DBs.
//
// The primary key stores the other columns as the value.
// The secondary keys have the primary key appended to them.
//

static const int iibench_num_dbs = 4;
static const size_t iibench_secondary_key_size = 16;

struct iibench_row {
    uint64_t pk;
    int64_t a;
    int64_t b;
    int64_t c;
};

struct iibench_secondary_row {
    int64_t column;
    uint64_t pk;
};

static int64_t hash(uint64_t key) {
    uint64_t hash = 0;
    uint8_t *buf = (uint8_t *) &key;
    for (int i = 0; i < 8; i++) {
        hash += (((buf[i] + 1) * 17) & 0xFF) << (i * 8);
    }
    return hash;
}

static int64_t iibench_generate_column_by_pk(int64_t pk, int db_idx) {
    invariant(db_idx > 0);
    return hash(pk * db_idx);
}

static void iibench_generate_row(int64_t pk, struct iibench_row *row) {
    row->a = iibench_generate_column_by_pk(pk, 1);
    row->b = iibench_generate_column_by_pk(pk, 2);
    row->c = iibench_generate_column_by_pk(pk, 3);
}

static void iibench_parse_row(const DBT *key, const DBT *val, struct iibench_row *row) {
    char *CAST_FROM_VOIDP(val_buf, val->data);
    invariant(key->size == 8);
    invariant(val->size == 24);
    memcpy(&row->pk, key->data, 8);
    memcpy(&row->a, val_buf + 0, 8);
    memcpy(&row->b, val_buf + 8, 8);
    memcpy(&row->c, val_buf + 16, 8);
}

static void UU() iibench_verify_row(const struct iibench_row *row) {
    struct iibench_row expected_row;
    iibench_generate_row(row->pk, &expected_row);
    invariant(row->a == expected_row.a);
    invariant(row->b == expected_row.b);
    invariant(row->c == expected_row.c);
}

static void iibench_parse_secondary_row(const DBT *key, const DBT *val, struct iibench_secondary_row *row) {
    char *CAST_FROM_VOIDP(key_buf, key->data);
    invariant(key->size == iibench_secondary_key_size);
    invariant(val->size == 0);
    memcpy(&row->column, key_buf + 0, 8);
    memcpy(&row->pk, key_buf + 8, 8);
}

static void UU() iibench_verify_secondary_row(const struct iibench_secondary_row *row, int db_idx) {
    int64_t expected = iibench_generate_column_by_pk(row->pk, db_idx);
    invariant(row->column == expected);
}

static void iibench_fill_key_buf(uint64_t pk, int64_t *buf) {
    memcpy(&buf[0], &pk, 8);
}

static void iibench_fill_val_buf(uint64_t pk, int64_t *buf) {
    struct iibench_row row;
    iibench_generate_row(pk, &row);
    memcpy(&buf[0], &row.a, sizeof(row.a));
    memcpy(&buf[1], &row.b, sizeof(row.b));
    memcpy(&buf[2], &row.c, sizeof(row.c));
}

static int iibench_get_db_idx(DB *db) {
    DESCRIPTOR desc = db->cmp_descriptor;
    invariant_notnull(desc->dbt.data);
    invariant(desc->dbt.size == sizeof(int));
    int db_idx;
    memcpy(&db_idx, desc->dbt.data, desc->dbt.size);
    return db_idx;
}

static void iibench_rangequery_cb(DB *db, const DBT *key, const DBT *val, void *extra) {
    invariant_null(extra);
    int db_idx = iibench_get_db_idx(db);
    if (db_idx == 0) {
        struct iibench_row row;
        iibench_parse_row(key, val, &row);
        iibench_verify_row(&row);
    } else {
        struct iibench_secondary_row row;
        iibench_parse_secondary_row(key, val, &row);
        iibench_verify_secondary_row(&row, db_idx);
    }
}

struct iibench_op_extra {
    uint64_t autoincrement;
};

static int UU() iibench_put_op(DB_TXN *txn, ARG arg, void *operation_extra, void *stats_extra) {
    DB **dbs = arg->dbp;
    DB_ENV *env = arg->env;
    DBT mult_key_dbt[iibench_num_dbs];
    DBT mult_val_dbt[iibench_num_dbs];
    uint32_t mult_put_flags[iibench_num_dbs];
    memset(mult_key_dbt, 0, sizeof(mult_key_dbt));
    memset(mult_val_dbt, 0, sizeof(mult_val_dbt));

    // The first index is unique with serial autoincrement keys.
    // The rest are have keys generated with this thread's random data.
    mult_put_flags[0] = get_put_flags(arg->cli) | DB_NOOVERWRITE;
    for (int i = 1; i < iibench_num_dbs; i++) {
        // Secondary keys have the primary key appended to them.
        mult_key_dbt[i].size = iibench_secondary_key_size;
        mult_key_dbt[i].data = toku_xmalloc(iibench_secondary_key_size);
        mult_key_dbt[i].flags = DB_DBT_REALLOC;
        mult_put_flags[i] = get_put_flags(arg->cli);
    }

    int r = 0;

    uint64_t puts_to_increment = 0;
    for (uint32_t i = 0; i < arg->cli->txn_size; ++i) {
        struct iibench_op_extra *CAST_FROM_VOIDP(info, operation_extra);

        // Get a random primary key, generate secondary key columns in valbuf
        uint64_t pk = toku_sync_fetch_and_add(&info->autoincrement, 1);
        int64_t keybuf[1];
        int64_t valbuf[3];
        iibench_fill_key_buf(pk, keybuf);
        iibench_fill_val_buf(pk, valbuf);
        dbt_init(&mult_key_dbt[0], keybuf, sizeof keybuf);
        dbt_init(&mult_val_dbt[0], valbuf, sizeof valbuf);

        r = env->put_multiple(
            env, 
            dbs[0], // source db.
            txn, 
            &mult_key_dbt[0], // source db key
            &mult_val_dbt[0], // source db value
            iibench_num_dbs, // total number of dbs
            dbs, // array of dbs
            mult_key_dbt, // array of keys
            mult_val_dbt, // array of values
            mult_put_flags // array of flags
            );
        if (r != 0) {
            goto cleanup;
        }
        puts_to_increment++;
        if (puts_to_increment == 100) {
            increment_counter(stats_extra, PUTS, puts_to_increment);
            puts_to_increment = 0;
        }
    }

cleanup:
    for (int i = 1; i < iibench_num_dbs; i++) {
        toku_free(mult_key_dbt[i].data);
    }
    return r;
}

static int iibench_generate_row_for_put(DB *dest_db, DB *src_db, DBT *dest_key, DBT *dest_val, const DBT *UU(src_key), const DBT *src_val) {
    invariant(src_db != dest_db);
    invariant_notnull(src_key->data);
    invariant(src_key->size == 8);
    invariant(dest_key->size == iibench_secondary_key_size);
    invariant(dest_key->flags == DB_DBT_REALLOC);

    // Get the db index from the descriptor. This is a secondary index
    // so it has to be greater than zero (which would be the pk). Then
    // grab the appropriate secondary key from the source val, which is
    // an array of the 3 columns, so we have to subtract 1 from the index.
    int db_idx = iibench_get_db_idx(dest_db);
    invariant(db_idx > 0 && db_idx < 4);
    int64_t *CAST_FROM_VOIDP(columns, src_val->data);
    int64_t secondary_key = columns[db_idx - 1];

    // First write down the secondary key, then the primary key (in src_key)
    int64_t *CAST_FROM_VOIDP(dest_key_buf, dest_key->data);
    memcpy(&dest_key_buf[0], &secondary_key, sizeof(secondary_key));
    memcpy(&dest_key_buf[1], src_key->data, src_key->size);
    dest_val->data = nullptr;
    dest_val->size = 0;
    return 0;
}

// After each DB opens, set the descriptor to store the DB idx value.
// Close and reopen the DB so we can use db->cmp_descriptor during comparisons.
static DB *iibench_set_descriptor_after_db_opens(DB_ENV *env, DB *db, int idx, reopen_db_fn reopen, struct cli_args *cli_args) {
    int r;
    DBT desc_dbt;
    desc_dbt.data = &idx;
    desc_dbt.size = sizeof(idx);
    desc_dbt.ulen = 0;
    desc_dbt.flags = 0;
    r = db->change_descriptor(db, nullptr, &desc_dbt, 0); CKERR(r);
    r = db->close(db, 0); CKERR(r);
    r = db_create(&db, env, 0); CKERR(r);
    reopen(db, idx, cli_args);
    return db;
}

static int iibench_compare_keys(DB *db, const DBT *a, const DBT *b) {
    int db_idx = iibench_get_db_idx(db);
    if (db_idx == 0) {
        invariant(a->size == 8);
        invariant(b->size == 8);
        uint64_t x = *(uint64_t *) a->data;
        uint64_t y = *(uint64_t *) b->data;
        if (x < y) {
            return -1;
        } else if (x == y) {
            return 0;
        } else {
            return 1;
        }
    } else {
        invariant(a->size == 16);
        invariant(b->size == 16);
        int64_t x = *(int64_t *) a->data;
        int64_t y = *(int64_t *) b->data;
        uint64_t pk_x = *(uint64_t *) (((char *) a->data) + 8);
        uint64_t pk_y = *(uint64_t *) (((char *) b->data) + 8);
        if (x < y) {
            return -1;
        } else if (x == y) {
            if (pk_x < pk_y) {
                return -1;
            } else if (pk_x == pk_y) {
                return 0;
            } else {
                return 1;
            }
        } else {
            return 1;
        }
    }
}

// Do a range query over the primary index, verifying the contents of the rows
static int iibench_rangequery_op(DB_TXN *txn, ARG arg, void *UU(operation_extra), void *stats_extra) {
    DB *db = arg->dbp[0];
    rangequery_db(db, txn, arg, iibench_rangequery_cb, nullptr);
    increment_counter(stats_extra, PTQUERIES, 1);
    return 0;
}

static void
stress_table(DB_ENV* env, DB **dbs, struct cli_args *cli_args) {
    if (verbose) printf("starting creation of pthreads\n");
    const int num_threads = cli_args->num_put_threads + cli_args->num_ptquery_threads;
    struct arg myargs[num_threads];

    // Put threads do iibench-like inserts with an auto-increment primary key
    // Query threads do range queries of a certain size, verifying row contents.

    struct iibench_op_extra put_extra = {
        .autoincrement = 0
    };
    for (int i = 0; i < num_threads; i++) {
        arg_init(&myargs[i], dbs, env, cli_args);
        if (i < cli_args->num_put_threads) {
            myargs[i].operation = iibench_put_op;
            myargs[i].operation_extra = &put_extra;
        } else {
            myargs[i].operation = iibench_rangequery_op;
            myargs[i].operation_extra = nullptr;
            myargs[i].txn_flags |= DB_TXN_READ_ONLY;
            myargs[i].sleep_ms = 1000; // 1 second between range queries
        }
    }
    const bool crash_at_end = false;
    run_workers(myargs, num_threads, cli_args->num_seconds, crash_at_end, cli_args);
}

int test_main(int argc, char *const argv[]) {
    struct cli_args args = get_default_args_for_perf();
    args.num_elements = 0;  // want to start with empty DBs
    // Puts per transaction is configurable. It defaults to 1k.
    args.txn_size = 1000;
    parse_stress_test_args(argc, argv, &args);
    // The index count and schema are not configurable. Silently ignore whatever was passed in.
    args.num_DBs = 4;
    args.key_size = 8;
    args.val_size = 32;
    // when there are multiple threads, its valid for two of them to
    // generate the same key and one of them fail with DB_LOCK_NOTGRANTED
    if (args.num_put_threads > 1) {
        args.crash_on_operation_failure = false;
    }
    args.env_args.generate_put_callback = iibench_generate_row_for_put;
    after_db_open_hook = iibench_set_descriptor_after_db_opens;
    perf_test_main_with_cmp(&args, iibench_compare_keys);
    return 0;
}
