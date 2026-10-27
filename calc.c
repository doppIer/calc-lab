// Sukru Bedirhan Hocuk 251ADB011
// https://github.com/doppIer/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: AI: Claude wrote the first version of this program from the lab text,
// conversation: https://claude.ai/share/22e92a07-2539-433a-b48f-484cc1396b82 ; lab description and calc-tests.zip (course material). Details in the comments marked "AI (Claude)".
//Other resources 

// https://www.youtube.com/watch?v=iu7V8gzGmT0 
// https://www.youtube.com/watch?v=0c8b7YfsBKs
// https://en.wikipedia.org/wiki/Operator-precedence_parser. 
// https://github.com/ricardofares/rdp-calc 
// https://stackoverflow.com/questions/380455/looking-for-a-clear-definition-of-what-a-tokenizer-parser-and-lexers-are
// https://stackoverflow.com/questions/40539418/what-is-the-best-way-to-write-a-syntax-tokenizer-parser-in-c
// https://gist.github.com/etscrivner/b4499524aaed5fccc1a9ce3635df14f0


// AI (Claude): I gave Claude the lab text and calc-tests.zip and asked it to tutorme for writiting the calc.c
// I define this because -std=c17 hides the POSIX functions I need (mkdir, stat, realpath, strndup).
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STUDENT_NAME "bedirhan"
#define STUDENT_LASTNAME "hocuk"
#define STUDENT_ID "251ADB011"

// One struct holds either an integer or a float, and is_float is my whole type system. I follow the
// same rule as C: if both operands are integers I compute in long long, and if either one is a float
// I convert both to double. That gives 7 / 2 == 3 and 7 / 2.0 == 3.5 without any special cases.
typedef struct {
    int is_float;
    long long i;
    double d;
} Value;

static double as_double(Value v) {
    return v.is_float ? v.d : (double)v.i;
}

// ---------------------------------------------------------------------------
// Tokenizer
//
// Positions: I keep the whole file in one buffer and the tokenizer reads straight from it, so a
// token's position is just (pointer - base) + 1. Every character counts exactly once, including
// each '\n' and the comment lines, and there is no separate counter that could drift out of sync.
//
// The tokenizer never reports an error. It returns kind '?' and lets the parser decide, because the
// rule says the error is at the token the parser was looking at when it got stuck. The tokenizer is
// always one token ahead, so in "10 / 0 &" it has already seen the '&' when the parser finds the
// zero, and the zero is the first error in reading order.
//
// kind is 'i' for an int, 'f' for a float, 'P' for **, 'e' for the end of the line, '?' for a bad
// character, and otherwise the operator or parenthesis character itself.
// ---------------------------------------------------------------------------
static const char *base, *p, *end;
static char kind;
static size_t tpos, err_pos;
static long long tint;
static double tfloat;

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void advance(void) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    tpos = (size_t)(p - base) + 1;
    // At the end of a line tpos is the '\n' itself (or file length + 1 for a last line without one).
    // That is exactly where "missing operand" and "missing )" have to be reported.
    if (p >= end) {
        kind = 'e';
    } else if (is_digit(*p)) {
        const char *s = p;
        // Unsigned, so a ridiculously long digit string wraps around instead of being signed overflow.
        unsigned long long v = 0;
        for (; p < end && is_digit(*p); p++) v = v * 10 + (unsigned long long)(*p - '0');
        kind = 'i';
        tint = (long long)v;
        if (p < end && *p == '.') {
            if (p + 1 < end && is_digit(p[1])) {
                for (p++; p < end && is_digit(*p); p++) {}
                // strtod would happily read on into "e3", which is invalid here, so I give it a
                // private copy of just the digits.digits part.
                char *copy = strndup(s, (size_t)(p - s));
                tfloat = strtod(copy, NULL);
                free(copy);
                kind = 'f';
            } else {
                // For "5." the error is at the '.', not at the number in front of it.
                kind = '?';
                tpos = (size_t)(p++ - base) + 1;
            }
        }
    } else {
        char c = *p++;
        // A '-' is a normal token here. There are no unary operators, so the parser rejects it
        // when it needs an operand, and the error lands on the '-' as the rules want.
        kind = strchr("+-/()", c) ? c : c == '*' ? '*' : '?';
        if (c == '*' && p < end && *p == '*') kind = 'P', p++;
    }
}

static int fail(size_t pos) {
    err_pos = pos;
    return -1;
}

// opos is the operator, which is where an inf/nan float result is reported. rpos is where the right
// operand starts, which is where division by zero and a negative integer exponent are reported.
// op is + - * / or 'P' for power.
static int calc(char op, Value a, Value b, size_t opos, size_t rpos, Value *out) {
    if (!a.is_float && !b.is_float) {
        // I do the integer math unsigned because it wraps, while signed overflow is undefined
        // behaviour. The tests never overflow, but this keeps the sanitizer quiet on any input.
        unsigned long long x = (unsigned long long)a.i, y = (unsigned long long)b.i, r = 1;
        switch (op) {
        case '+': r = x + y; break;
        case '-': r = x - y; break;
        case '*': r = x * y; break;
        case '/':
            if (b.i == 0) return fail(rpos);
            // LLONG_MIN / -1 is the one division that overflows, so I negate instead.
            r = b.i == -1 ? 0ULL - x : (unsigned long long)(a.i / b.i);
            break;
        default:
            // A negative exponent would give a fraction, which an integer cannot hold.
            if (b.i < 0) return fail(rpos);
            // Square and multiply needs about 20 steps for 2 ** 1000000 instead of a million.
            for (; y; y >>= 1, x *= x)
                if (y & 1) r *= x;
        }
        *out = (Value){0, (long long)r, 0.0};
        return 0;
    }
    double x = as_double(a), y = as_double(b), r;
    // Float division by 0.0 would quietly give inf, but the rules call it division by zero.
    if (op == '/' && y == 0.0) return fail(rpos);
    r = op == '+' ? x + y : op == '-' ? x - y : op == '*' ? x * y : op == '/' ? x / y : pow(x, y);
    // This catches overflow to inf and things like (0 - 8) ** 0.5, which gives nan.
    if (!isfinite(r)) return fail(opos);
    *out = (Value){1, 0, r};
    return 0;
}

// ---------------------------------------------------------------------------
// Parser
//
// It is recursive descent and evaluates while it parses, so I never build a tree and have nothing
// to allocate or free per expression.
//
//   expr    := term  { ('+' | '-') term }       level 0
//   term    := power { ('*' | '/') power }      level 1
//   power   := primary [ '**' power ]
//   primary := NUMBER | '(' expr ')'
//
// Precedence works because a lower-precedence operator sits further up the call chain. In
// 3 + 2 * 5 the term level swallows "2 * 5" completely before the expr level ever sees a '+',
// so the multiplication is done first.
//
// The + - and * / levels are loops that fold into the left operand as they go. That makes them
// left-associative: 10 - 3 - 2 is (10 - 3) - 2. Recursing on the right side would give 10 - (3 - 2).
// ---------------------------------------------------------------------------
static int parse(int level, Value *out);

static int primary(Value *out) {
    if (kind == 'i' || kind == 'f') {
        *out = kind == 'i' ? (Value){0, tint, 0.0} : (Value){1, 0, tfloat};
        advance();
        return 0;
    }
    // If it is not a number or '(' the parser needed an operand and found something else. This one
    // check covers a second operator in a row, a unary '-', "()", an operator at the end of the line
    // and a bad character. In every case the error is the token I am looking at.
    if (kind != '(') return fail(tpos);
    advance();
    if (parse(0, out) < 0) return -1;
    // Anything but ')' means it is missing. At the end of a line that token is the '\n', otherwise
    // it is the unexpected token itself.
    if (kind != ')') return fail(tpos);
    advance();
    return 0;
}

// ** is right-associative because the exponent of the first ** is everything after it. power()
// calls itself for the exponent, so 2 ** 3 ** 2 groups as 2 ** (3 ** 2) = 512. I only calculate after
// the whole exponent is parsed, so a syntax error further right is reported before an evaluation
// error on the left.
static int power(Value *out) {
    if (primary(out) < 0) return -1;
    if (kind != 'P') return 0;
    size_t opos = tpos;
    advance();
    size_t rpos = tpos;
    Value ex;
    if (power(&ex) < 0) return -1;
    return calc('P', *out, ex, opos, rpos, out);
}

// One function handles both the + - level and the * / level. Only the set of operators differs.
static int parse(int level, Value *out) {
    if (level == 2) return power(out);
    const char *ops = level ? "*/" : "+-";
    if (parse(level + 1, out) < 0) return -1;
    while (kind != 'e' && strchr(ops, kind)) {
        char op = kind;
        size_t opos = tpos;
        advance();
        // I save this before parsing the right side, because division by zero is reported where the
        // divisor starts, even when it is a long expression like (4 - 2 * 2).
        size_t rpos = tpos;
        Value rhs;
        if (parse(level + 1, &rhs) < 0 || calc(op, *out, rhs, opos, rpos, out) < 0) return -1;
    }
    return 0;
}

// I write results straight to f as I go, and the caller throws the file away and writes only the
// error if this fails. That way a later line can still turn the whole output into one ERROR.
static int evaluate(const char *text, size_t len, FILE *f) {
    base = text;
    // I find each '\n' with memchr and never copy a line, so a multi-megabyte file is read once.
    for (const char *s = text, *file_end = text + len; s < file_end;) {
        const char *nl = memchr(s, '\n', (size_t)(file_end - s));
        end = nl ? nl : file_end;
        const char *q = s;
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        // Blank lines and lines starting with '#' print nothing. Their characters are still counted,
        // because positions come from the buffer offset and not from a counter I would have to update.
        if (q < end && *q != '#') {
            Value v;
            p = s;
            advance();
            if (parse(0, &v) < 0) return -1;
            // After a complete expression the line must be over. Anything left is an extra ')',
            // two numbers in a row or a bad character, and it is reported at that token.
            if (kind != 'e') return fail(tpos);
            if (v.is_float) fprintf(f, "%.15g\n", v.d);
            else fprintf(f, "%lld\n", v.i);
        }
        s = nl ? nl + 1 : file_end;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command line, files and folders
// ---------------------------------------------------------------------------

// A small printf that returns a malloc'd string. I use it so I never have to guess buffer sizes for
// paths. Running out of memory is the one failure I do not report nicely: I just stop.
static char *fmt(const char *format, ...) {
    va_list ap, ap2;
    va_start(ap, format);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, format, ap);
    char *s = malloc((size_t)n + 1);
    if (!s) exit(2);
    vsnprintf(s, (size_t)n + 1, format, ap2);
    va_end(ap);
    va_end(ap2);
    return s;
}

// Last path component, ignoring trailing '/', and optionally without ".txt".
static char *last_component(const char *path, int strip_txt) {
    size_t len = strlen(path), start = len;
    while (len > 1 && path[len - 1] == '/') len--;
    for (start = len; start > 0 && path[start - 1] != '/'; start--) {}
    if (strip_txt && len - start > 4 && memcmp(path + len - 4, ".txt", 4) == 0) len -= 4;
    return fmt("%.*s", (int)(len - start), path + start);
}

// I read the whole file in one go instead of line by line. The error position is an offset from the
// start of the FILE, so I want one block where "pointer - start" is that offset directly.
static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *data = NULL;
    long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (size >= 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)size + 1))) {
        *len = fread(data, 1, (size_t)size, f);
        if (ferror(f)) free(data), data = NULL;
        else data[*len] = '\0';
    }
    fclose(f);
    return data;
}

static int run_file(const char *path, const char *outdir) {
    size_t len;
    char *text = read_file(path, &len);
    if (!text) {
        fprintf(stderr, "calc: cannot read '%s': %s\n", path, strerror(errno));
        return -1;
    }
    char *name = last_component(path, 1);
    char *outpath = fmt("%s/%s_" STUDENT_NAME "_" STUDENT_LASTNAME "_" STUDENT_ID ".txt", outdir, name);
    // I read the input before opening the output, so an unreadable input leaves no empty result file.
    FILE *f = fopen(outpath, "wb");
    int bad = !f;
    if (!f) fprintf(stderr, "calc: cannot write '%s': %s\n", outpath, strerror(errno));
    else {
        if (evaluate(text, len, f) < 0) {
            // "If any error occurs the file contains ONLY the error", so the partial results go.
            f = freopen(outpath, "wb", f);
            bad = !f || fprintf(f, "ERROR:%zu\n", err_pos) < 0;
        }
        // fclose can fail too (for example disk full), and that would lose output silently.
        if (f && fclose(f) != 0) bad = 1;
        if (bad) fprintf(stderr, "calc: error while writing '%s'\n", outpath);
    }
    free(outpath);
    free(name);
    free(text);
    return bad ? -1 : 0;
}

int main(int argc, char **argv) {
    const char *dir = NULL, *outdir = NULL, *input = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int is_d = !strcmp(a, "-d") || !strcmp(a, "--dir");
        int is_o = !strcmp(a, "-o") || !strcmp(a, "--output-dir");
        if (is_d || is_o) {
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: option '%s' needs a value\n", a);
                return 2;
            }
            if (is_d) dir = argv[++i];
            else outdir = argv[++i];
        } else if ((a[0] == '-' && a[1] != '\0') || input) {
            fprintf(stderr, "calc: %s '%s'\n", input && a[0] != '-' ? "more than one input file:" : "unknown option", a);
            return 2;
        } else {
            input = a;
        }
    }
    // Exactly one of -d and an input file must be given, so both missing or both present is a usage error.
    if (!dir == !input) {
        fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n"
                        "calc: give either one input file or -d DIR\n");
        return 2;
    }

    // I check the input before creating the output folder, so a typo in the input name does not
    // leave an empty folder behind.
    struct stat st;
    if (stat(dir ? dir : input, &st) != 0 || S_ISDIR(st.st_mode) != (dir != NULL)) {
        fprintf(stderr, "calc: cannot read %s '%s'\n", dir ? "folder" : "input file", dir ? dir : input);
        return 1;
    }

    char *default_out = NULL;
    if (!outdir) {
        char *b = last_component(dir ? dir : input, !dir);
        // "." and ".." are not real names, so I ask for the real path of the folder instead.
        if (dir && (!strcmp(b, ".") || !strcmp(b, ".."))) {
            char *real = realpath(dir, NULL);
            if (real) free(b), b = last_component(real, 0), free(real);
        }
        outdir = default_out = fmt("%s_" STUDENT_NAME "_" STUDENT_ID, b);
        free(b);
    }

    int status = 0;
    if (stat(outdir, &st) == 0 ? !S_ISDIR(st.st_mode) : mkdir(outdir, 0777) != 0) {
        fprintf(stderr, "calc: cannot use '%s' as output folder\n", outdir);
        status = 1;
    } else if (input) {
        status = run_file(input, outdir) < 0;
    } else {
        DIR *d = opendir(dir);
        for (struct dirent *e; d && (e = readdir(d));) {
            size_t n = strlen(e->d_name);
            // Only "*.txt" files directly inside the folder. The S_ISREG check also skips a
            // subfolder that happens to be called something.txt.
            if (n <= 4 || strcmp(e->d_name + n - 4, ".txt") != 0) continue;
            char *full = fmt("%s/%s", dir, e->d_name);
            if (stat(full, &st) == 0 && S_ISREG(st.st_mode) && run_file(full, outdir) < 0) status = 1;
            free(full);
        }
        if (d) closedir(d);
        else status = 1;
    }
    free(default_out);
    return status;
}
