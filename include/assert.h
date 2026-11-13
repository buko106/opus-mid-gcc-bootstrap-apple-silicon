#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
void __assert_rtn(const char *, const char *, int, const char *);
#define assert(e) ((e) ? (void)0 : __assert_rtn(__func__, __FILE__, __LINE__, #e))
#endif
