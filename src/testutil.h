#ifndef MQL_TESTUTIL_H_INCLUDED
#define MQL_TESTUTIL_H_INCLUDED

void _assert_streq(const char* s1, const char* s2,  const char *file, unsigned line) {
    if (strcmp(s1, s2) == 0)
        return;

    fprintf (stderr, "FAIL assert_streq in at %s:%u:\n%s\n------\n%s\n", file, line, s1, s2);
    fflush (stderr);
    abort();
}
#define assert_streq(s1,s2) _assert_streq(s1, s2, __FILE__, __LINE__)

void _assert_strneq(const char* s1, const char* s2,  const char *file, unsigned line) {
    if (strcmp(s1, s2) != 0)
        return;

    fprintf (stderr, "FAIL assert_strneq in at %s:%u: \"%s\" == \"%s\"\n", file, line, s1, s2);
    fflush (stderr);
    abort();
}
#define assert_strneq(s1,s2) _assert_strneq(s1, s2, __FILE__, __LINE__)


#endif
