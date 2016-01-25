#undef assert

#define	assert(e) (void)0

#if defined(__lint__)
#define __assert_function__	(__static_cast(const void *,0))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define __assert_function__	__func__
#else
#define __assert_function__	__PRETTY_FUNCTION__
#endif

#ifndef __ASSERT_DECLARED
#define __ASSERT_DECLARED
void __assert13(const char *, int, const char *, const char *);
#endif
