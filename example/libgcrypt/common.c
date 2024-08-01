#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <gcrypt.h>

void help(char *name) {
    printf("Usage: %s -i file -o file -k file -n num [-h]\n", name);
    printf("\n");
    printf(" -i file    File with concatenated ciphertexts to decrypt\n");
    printf(" -o file    File where to write the time to decrypt the ciphertext\n");
    printf(" -k file    File with the RSA private key in PEM format\n");
    printf(" -n num     Length of individual ciphertexts in bytes\n");
    printf(" -h         This message\n");
}

/* Get an architecture specific most precise clock source with the lowest
 * overhead. Should be executed at the start of the measurement period
 * (because of barriers against speculative execution
 */
uint64_t get_time_before() {
    uint64_t time_before = 0;
#if defined( __s390x__ )
    /* The 64 bit TOD (time-of-day) value is running at 4096.000MHz, but
     * on some machines not all low bits are updated (the effective frequency
     * remains though)
     */

    /* use STCKE as it has lower overhead,
     * see http://publibz.boulder.ibm.com/epubs/pdf/dz9zr007.pdf
     */
    //asm volatile (
    //    "stck    %0": "=Q" (time_before) :: "memory", "cc");

    uint8_t clk[16];
    asm volatile (
          "stcke %0" : "=Q" (clk) :: "memory", "cc");
    /* since s390x is big-endian we can just do a byte-by-byte copy,
     * First byte is the epoch number (143 year cycle) while the following
     * 8 bytes are the same as returned by STCK */
    *time_before = *(uint64_t *)(clk + 1);
#elif defined( __PPC64__ )
    asm volatile (
        "mftb    %0": "=r" (time_before) :: "memory", "cc");
#elif defined( __aarch64__ )
    asm volatile (
        "mrs %0, cntvct_el0": "=r" (time_before) :: "memory", "cc");
#elif defined( __x86_64__ )
    uint32_t time_before_high = 0, time_before_low = 0;
    asm volatile (
        "CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t" : "=r" (time_before_high),
        "=r" (time_before_low)::
        "%rax", "%rbx", "%rcx", "%rdx");
    time_before = (uint64_t)time_before_high<<32 | time_before_low;
#else
#error Unsupported architecture
#endif /* ifdef __s390x__ */
    return time_before;
}

/* Get an architecture specific most precise clock source with the lowest
 * overhead. Should be executed at the end of the measurement period
 * (because of barriers against speculative execution
 */
uint64_t get_time_after() {
    uint64_t time_after = 0;
#if defined( __s390x__ )
    /* The 64 bit TOD (time-of-day) value is running at 4096.000MHz, but
     * on some machines not all low bits are updated (the effective frequency
     * remains though)
     */

    /* use STCKE as it has lower overhead,
     * see http://publibz.boulder.ibm.com/epubs/pdf/dz9zr007.pdf
     */
    //asm volatile (
    //    "stck    %0": "=Q" (time_before) :: "memory", "cc");

    uint8_t clk[16];
    asm volatile (
          "stcke %0" : "=Q" (clk) :: "memory", "cc");
    /* since s390x is big-endian we can just do a byte-by-byte copy,
     * First byte is the epoch number (143 year cycle) while the following
     * 8 bytes are the same as returned by STCK */
    *time_after = *(uint64_t *)(clk + 1);
#elif defined( __PPC64__ )
    /* Note: mftb can be used with a single instruction on ppc64, for ppc32
     * it's necessary to read upper and lower 32bits of the values in two
     * separate calls and verify that we didn't do that during low value
     * overflow
     */
    asm volatile (
        "mftb    %0": "=r" (time_after) :: "memory", "cc");
#elif defined( __aarch64__ )
    asm volatile (
        "mrs %0, cntvct_el0": "=r" (time_after) :: "memory", "cc");
#elif defined( __x86_64__ )
    uint32_t time_after_high = 0, time_after_low = 0;
    asm volatile (
        "RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t": "=r" (time_after_high),
        "=r" (time_after_low)::
        "%rax", "%rbx", "%rcx", "%rdx");
    time_after = (uint64_t)time_after_high<<32 | time_after_low;
#else
#error Unsupported architecture
#endif /* ifdef __s390x__ */
    return time_after;
}

/* read next line in file, skip commented out or empty */
char * read_line(FILE *fp) {
    char line[4096];
    char *p;

    do {
        if (!fgets(line, sizeof(line), fp)) {
            if (feof(fp))
                return NULL;
            abort();
        }
        p = strchr(line, '\n');
        if (!p)
            abort();
        *p = 0;
    } while (!*line || *line == '#');

    return strdup(line);
}

/* read a line from file, expect tag in expected, put hex decoded value in
 * allocated buffer of length len */
void read_param(FILE *fp, char *expected, char **buffer, size_t *len) {
    char *line;
    char *pos;

    *buffer = NULL;

    line = read_line(fp);
    if (line == NULL) {
        fprintf(stderr, "end of file reached\n");
        abort();
    }

    if (memcmp(line, expected, strlen(expected)) != 0) {
        fprintf(stderr, "tag %s not found in line: %s\n", expected, line);
        abort();
    }
    if (line[strlen(expected)] != '=') {
        fprintf(stderr, "'=' separator not found\n");
        abort();
    }
    *len = strlen(&line[strlen(expected)+1])/2;

    *buffer = malloc(*len);
    if (*buffer == NULL)
        abort();
    pos = *buffer;

    for(char *s = &line[strlen(expected)+1]; *s; s += 2) {
        sscanf(s, "%2hhx", pos);
        pos += 1;
    }

    free(line);
}

gcry_sexp_t read_private_key(FILE *fp) {
    char *n, *e, *d, *p, *q, *iqmp;
    size_t len_n, len_e, len_d, len_p, len_q, len_iqmp;
    gcry_sexp_t key;

    read_param(fp, "n", &n, &len_n);
    fprintf(stderr, "read n, len: %i\n", len_n);
    read_param(fp, "e", &e, &len_e);
    fprintf(stderr, "read e, len: %i\n", len_e);
    read_param(fp, "d", &d, &len_d);
    fprintf(stderr, "read d, len: %i\n", len_d);
    /* libgcrypt expects inverso of p mod q, not q mod p, so we need to
     * reverse the order of primes */
    read_param(fp, "p", &q, &len_q);
    fprintf(stderr, "read q, len: %i\n", len_q);
    read_param(fp, "q", &p, &len_p);
    fprintf(stderr, "read p, len: %i\n", len_p);
    read_param(fp, "qInv", &iqmp, &len_iqmp);
    fprintf(stderr, "read qInv, len: %i\n", len_iqmp);

    if (gcry_sexp_build(&key, NULL,
            "(private-key(rsa(n %b)(e %b)(d %b)(p %b)(q %b)(u %b)))",
            len_n, n,
            len_e, e,
            len_d, d,
            len_p, p,
            len_q, q,
            len_iqmp, iqmp)) {
        fprintf(stderr, "private key construction failed\n");
        abort();
    }
    return key;
}
