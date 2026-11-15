/* Conventions shared by the test runner and its suites. */
#ifndef CANDYTRON_TEST_SUITE_H
#define CANDYTRON_TEST_SUITE_H

/* Return value of a suite that cannot run on this machine (no OpenGL driver, no player executable). The same code ctest
 * understands as "skipped". */
#define CANDYTRON_TEST_SKIP 77

#ifndef CANDYTRON_TEST_DATA_DIR
#error "CANDYTRON_TEST_DATA_DIR must name tests/data (the reference fixtures)"
#endif

#endif
