/*
 * gthread_stubs.c -- No-op stubs for GCC __gthread_* symbols
 *
 * GCC's libstdc++ on AmigaOS 4 was built expecting pthreads support,
 * but AmigaOS newlib does not provide the __gthread_* symbols that
 * libstdc++ references internally (exception handling, TLS, etc.).
 *
 * AmigaOS 4 GCC uses Thread model: single (see g++ -v), so C++
 * threading primitives (std::mutex, std::thread) are not supported.
 * These stubs satisfy the linker while preserving single-threaded
 * semantics:
 *
 *   - __gthread_active_p() returns 0: threading is not active.
 *     Libraries that check this (including libstdc++ and VMA) will
 *     skip all locking when it returns 0.
 *   - All mutex/key operations are no-ops returning success (0).
 *
 * Reference: https://wiki.amigaos.net/wiki/PThreads_Library
 *   "Thread model: single" -- C++ exceptions and RTTI do not
 *   function correctly across multiple threads on AmigaOS 4.
 *
 * Link any C++ AmigaOS binary that uses libstdc++ with this file
 * to resolve __gthread_* undefined references.
 */

typedef int __gthread_mutex_t;
typedef int __gthread_recursive_mutex_t;
typedef int __gthread_key_t;
typedef int __gthread_once_t;

int __gthread_active_p(void)
{
    return 0; /* single-threaded */
}

int __gthread_mutex_lock(__gthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int __gthread_mutex_unlock(__gthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int __gthread_mutex_init(__gthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int __gthread_mutex_destroy(__gthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int __gthread_once(__gthread_once_t *o, void (*func)(void))
{
    (void)o;
    (void)func;
    return 0;
}

void *__gthread_getspecific(__gthread_key_t k)
{
    (void)k;
    return 0;
}

int __gthread_setspecific(__gthread_key_t k, const void *v)
{
    (void)k;
    (void)v;
    return 0;
}

int __gthread_key_create(__gthread_key_t *k, void (*destructor)(void *))
{
    (void)k;
    (void)destructor;
    return 0;
}

int __gthread_key_delete(__gthread_key_t k)
{
    (void)k;
    return 0;
}

/* Recursive mutex stubs (used by libstdc++ __cxa_guard_* for static init) */

int __gthread_recursive_mutex_init(__gthread_recursive_mutex_t *m)
{
    (void)m;
    return 0;
}

int __gthread_recursive_mutex_lock(__gthread_recursive_mutex_t *m)
{
    (void)m;
    return 0;
}

int __gthread_recursive_mutex_unlock(__gthread_recursive_mutex_t *m)
{
    (void)m;
    return 0;
}
