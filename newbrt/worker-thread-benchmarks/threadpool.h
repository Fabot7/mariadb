// A threadpool is a limited set of threads that can be used to apply a 
// function to work contained in a work queue.  The work queue is outside
// of the scope of the threadpool; the threadpool merely provides 
// mechanisms to grow the number of threads in the threadpool on demand.

typedef struct threadpool *THREADPOOL;

// Create a new threadpool
// Effects: a new threadpool is allocated and initialized. the number of 
// threads in the threadpool is limited to max_threads.  initially, there
// are no threads in the pool.
// Returns: if there are no errors, the threadpool is set and zero is returned.
// Otherwise, an error number is returned.

int threadpool_create(THREADPOOL *threadpoolptr, int max_threads);

// Destroy a threadpool
// Effects: the calling thread joins with all of the threads in the threadpool.
// Effects: the threadpool memory is freed.
// Returns: the threadpool is set to null.

void threadpool_destroy(THREADPOOL *threadpoolptr);

// Maybe add a thread to the threadpool.
// Effects: the number of threads in the threadpool is expanded by 1 as long
// as the current number of threads in the threadpool is less than the max
// and there are no idle threads.
// Effects: if the thread is create, it calls the function f with argument arg
// Expects: external serialization on this function; only one thread may
// execute this function

void threadpool_maybe_add(THREADPOOL theadpool, void *(*f)(void *), void *arg);

// Set the current thread busy
// Effects: the threadpool keeps a count of the number of idle threads.  It 
// uses this count to control the creation of additional threads. 

void threadpool_set_thread_busy(THREADPOOL);

// Set the current thread idle

void threadpool_set_thread_idle(THREADPOOL);

// get the current number of threads

int threadpool_get_current_threads(THREADPOOL);
