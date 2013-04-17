/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:

/* verify that maybe_get_and_pin returns an error while a prefetch block is pending */
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."

#include "test.h"

static int
fetch (CACHEFILE f        __attribute__((__unused__)),
       PAIR UU(p),
       int UU(fd),
       CACHEKEY k         __attribute__((__unused__)),
       uint32_t fullhash __attribute__((__unused__)),
       void **value       __attribute__((__unused__)),
       void** UU(dd),
       PAIR_ATTR *sizep        __attribute__((__unused__)),
       int  *dirtyp       __attribute__((__unused__)),
       void *extraargs    __attribute__((__unused__))
       ) {

    sleep(10);

    *value = 0;
    *sizep = make_pair_attr(1);
    *dirtyp = 1;

    return 0;
}


static void cachetable_prefetch_maybegetandpin_test (void) {
    const int test_limit = 1;
    int r;
    CACHETABLE ct;
    toku_cachetable_create(&ct, test_limit, ZERO_LSN, NULL_LOGGER);
    char fname1[] = __SRCFILE__ "test1.dat";
    unlink(fname1);
    CACHEFILE f1;
    r = toku_cachetable_openf(&f1, ct, fname1, O_RDWR|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO); assert(r == 0);

    // prefetch block 0. this will take 10 seconds.
    CACHEKEY key = make_blocknum(0);
    uint32_t fullhash = toku_cachetable_hash(f1, make_blocknum(0));
    CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
    r = toku_cachefile_prefetch(f1, key, fullhash, wc, fetch, def_pf_req_callback, def_pf_callback, 0, NULL);
    toku_cachetable_verify(ct);

    // verify that maybe_get_and_pin returns an error while the prefetch is in progress
    int i;
    for (i=1; i>=0; i++) {
        void *v;
        r = toku_cachetable_maybe_get_and_pin(f1, key, fullhash, PL_WRITE_EXPENSIVE, &v);
        if (r == 0) break;
        toku_pthread_yield();
    }
    if (verbose) printf("%s:%d %d\n", __FUNCTION__, __LINE__, i);
    assert(i>1);
    toku_cachetable_verify(ct);

    r = toku_test_cachetable_unpin(f1, key, fullhash, CACHETABLE_CLEAN, make_pair_attr(1));
    assert(r == 0);
    toku_cachetable_verify(ct);

    toku_cachefile_close(&f1, false, ZERO_LSN);
    toku_cachetable_close(&ct);
}

int
test_main(int argc, const char *argv[]) {
    default_parse_args(argc, argv);
    cachetable_prefetch_maybegetandpin_test();
    return 0;
}
