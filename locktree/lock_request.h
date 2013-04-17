/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id: locktree.h 49763 2012-11-09 01:22:44Z esmet $"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#ifndef TOKU_LOCK_REQUEST_H
#define TOKU_LOCK_REQUEST_H

#include <db.h>
#include <toku_pthread.h>

#include <ft/fttypes.h>
#include <ft/comparator.h>

#include "locktree.h"
#include "txnid_set.h"
#include "wfg.h"

namespace toku {

// A lock request contains the db, the key range, the lock type, and
// the transaction id that describes a potential row range lock.
//
// the typical use case is:
// - initialize a lock request
// - start to try to acquire the lock
// - do something else
// - wait for the lock request to be resolved on a timed condition
// - destroy the lock request
// a lock request is resolved when its state is no longer pending, or
// when it becomes granted, or timedout, or deadlocked. when resolved, the
// state of the lock request is changed and any waiting threads are awakened.

class lock_request {
public:
    enum type {
        UNKNOWN,
        READ,
        WRITE
    };

    // effect: Initializes a lock request with a given wait time.
    void create(uint64_t wait_time);

    // effect: Destroys a lock request.
    void destroy(void);

    // effect: Resets the lock request parameters, allowing it to be reused.
    // requires: Lock request was already created at some point
    void set(locktree *lt, TXNID txnid,
            const DBT *left_key, const DBT *right_key, type lock_type);

    // effect: Tries to acquire a lock described by this lock request.
    // returns: The return code of locktree::acquire_[write,read]_lock()
    //          or DB_LOCK_DEADLOCK if this request would end up deadlocked.
    int start(void);

    // effect: Sleeps until either the request is granted or the wait time expires.
    // returns: The return code of locktree::acquire_[write,read]_lock()
    //          or simply DB_LOCK_NOTGRANTED if the wait time expired.
    int wait(void);

    const DBT *get_left_key(void) const;

    const DBT *get_right_key(void) const;

    // effect: Retries all of the lock requests for the given locktree.
    //         Any lock requests successfully restarted is completed and woken up.
    //         The rest remain pending.
    static void retry_all_lock_requests(locktree *lt);

private:

    enum state {
        UNINITIALIZED,
        INITIALIZED,
        PENDING,
        COMPLETE,
    };

    // The keys for a lock request are stored "unowned" in m_left_key
    // and m_right_key. When the request is about to go to sleep, it
    // copies these keys and stores them in m_left_key_copy etc and
    // sets the temporary pointers to null.
    TXNID m_txnid;
    const DBT *m_left_key;
    const DBT *m_right_key;
    DBT m_left_key_copy;
    DBT m_right_key_copy;

    // The lock request type and associated locktree
    type m_type;
    locktree *m_lt;

    // If the lock request is in the completed state, then its
    // final return value is stored in m_complete_r
    int m_complete_r;
    state m_state;

    uint64_t m_wait_time;
    toku_cond_t m_wait_cond;

    // the lock request info state stored in the
    // locktree that this lock request is for.
    struct locktree::lt_lock_request_info *m_info;

    // effect: tries again to acquire the lock described by this lock request
    // returns: 0 if retrying the request succeeded and is now complete
    int retry(void);

    void complete(int complete_r);

    // effect: Finds another lock request by txnid.
    // requires: The lock request info mutex is held
    lock_request *find_lock_request(const TXNID &txnid);

    // effect: Insert this lock request into the locktree's set.
    // requires: the locktree's mutex is held
    void insert_into_lock_requests(void);

    // effect: Removes this lock request from the locktree's set.
    // requires: The lock request info mutex is held
    void remove_from_lock_requests(void);

    // effect: Asks this request's locktree which txnids are preventing
    //         us from getting the lock described by this request.
    // returns: conflicts is populated with the txnid's that this request
    //          is blocked on
    void get_conflicts(txnid_set *conflicts);

    // effect: Builds a wait-for-graph for this lock request and the given conflict set
    void build_wait_graph(wfg *wait_graph, const txnid_set &conflicts);

    // returns: True if this lock request is in deadlock with the given conflicts set
    bool deadlock_exists(const txnid_set &conflicts);

    void copy_keys(void);

    void calculate_cond_wakeup_time(struct timespec *ts);

    static int find_by_txnid(lock_request * const &request, const TXNID &txnid);

    friend class lock_request_unit_test;
};
ENSURE_POD(lock_request);

} /* namespace toku */

#endif /* TOKU_LOCK_REQUEST_H */
