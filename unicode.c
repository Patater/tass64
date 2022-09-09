/*
    $Id$

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#include "unicode.h"
#include "wchar.h"
#include "wctype.h"
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "unicodedata.h"
#include "str.h"
#include "console.h"
#include "error.h"

enum { U_CASEFOLD = 1, U_COMPAT = 2 };

FAST_CALL unsigned int utf8in(const uint8_t *c, unichar_t *out) { /* only for internal use with validated utf-8! */
    unsigned int i, j;
    unichar_t ch = c[0];

    if (ch < 0xe0) {
        *out = (ch << 6) ^ c[1] ^ 0x3080;
        return 2;
    }
    if (ch < 0xf0) {
        ch ^= 0xe0;i = 3;
    } else if (ch < 0xf8) {
        ch ^= 0xf0;i = 4;
    } else if (ch < 0xfc) {
        ch ^= 0xf8;i = 5;
    } else {
        ch ^= 0xfc;i = 6;
    }

    for (j = 1;j < i; j++) {
        ch = (ch << 6) ^ c[j] ^ 0x80;
    }
    *out = ch;
    return i;
}

FAST_CALL unsigned int utf8out(unichar_t i, uint8_t *c) {
    if (i < 0x800) {
        c[0] = (uint8_t)(0xc0 | (i >> 6));
        c[1] = (uint8_t)(0x80 | (i & 0x3f));
        return 2;
    }
    if (i < 0x10000) {
        c[0] = (uint8_t)(0xe0 | (i >> 12));
        c[1] = (uint8_t)(0x80 | ((i >> 6) & 0x3f));
        c[2] = (uint8_t)(0x80 | (i & 0x3f));
        return 3;
    }
    if (i < 0x200000) {
        c[0] = (uint8_t)(0xf0 | (i >> 18));
        c[1] = (uint8_t)(0x80 | ((i >> 12) & 0x3f));
        c[2] = (uint8_t)(0x80 | ((i >> 6) & 0x3f));
        c[3] = (uint8_t)(0x80 | (i & 0x3f));
        return 4;
    }
    if (i < 0x4000000) {
        c[0] = (uint8_t)(0xf8 | (i >> 24));
        c[1] = (uint8_t)(0x80 | ((i >> 18) & 0x3f));
        c[2] = (uint8_t)(0x80 | ((i >> 12) & 0x3f));
        c[3] = (uint8_t)(0x80 | ((i >> 6) & 0x3f));
        c[4] = (uint8_t)(0x80 | (i & 0x3f));
        return 5;
    }
    if ((i & ~(unichar_t)0x7fffffff) != 0) return 0;
    c[0] = (uint8_t)(0xfc | (i >> 30));
    c[1] = (uint8_t)(0x80 | ((i >> 24) & 0x3f));
    c[2] = (uint8_t)(0x80 | ((i >> 18) & 0x3f));
    c[3] = (uint8_t)(0x80 | ((i >> 12) & 0x3f));
    c[4] = (uint8_t)(0x80 | ((i >> 6) & 0x3f));
    c[5] = (uint8_t)(0x80 | (i & 0x3f));
    return 6;
}

static inline unsigned int utf8outlen(unichar_t i) {
    if (i < 0x800) return 2;
    if (i < 0x10000) return 3;
    if (i < 0x200000) return 4;
    if (i < 0x4000000) return 5;
    return 6;
}

MUST_CHECK bool extend_ubuff(struct ubuff_s *d) {
    uint32_t len;
    unichar_t *data;
    if (add_overflow(d->len, 16, &len)) return true;
    data = reallocate_array(d->data, len);
    if (data == NULL) return true;
    d->data = data;
    d->len = len;
    return false;
}

static MUST_CHECK bool udecompose(unichar_t ch, struct ubuff_s *d, int options) {
    const struct properties_s *prop;
    if (ch >= 0xac00 && ch <= 0xd7a3) {
        unichar_t ht, hs = ch - 0xac00;
        if (d->p + 3 > d->len && extend_ubuff(d)) return true;
        d->data[d->p++] = 0x1100 + hs / 588;
        d->data[d->p++] = 0x1161 + (hs % 588) / 28;
        ht = hs % 28;
        if (ht != 0) {
            d->data[d->p++] = 0x11a7 + ht;
        }
        return false;
    }
    prop = uget_property(ch);
    if ((options & U_CASEFOLD) != 0 && prop->casefold != 0) {
        if (prop->casefold > 0) {
            if (d->p >= d->len && extend_ubuff(d)) return true;
            d->data[d->p++] = (uint16_t)prop->casefold;
            return false;
        }
        if (prop->casefold > -16384) {
            const int16_t *p;
            for (p = &usequences[-prop->casefold];; p++) {
                if (d->p >= d->len && extend_ubuff(d)) return true;
                d->data[d->p++] = (uint16_t)abs(*p);
                if (*p < 0) return false;
            }
        } else {
            const int32_t *p;
            for (p = &usequences2[-prop->casefold - 16384];; p++) {
                if (d->p >= d->len && extend_ubuff(d)) return true;
                d->data[d->p++] = (uint32_t)abs(*p);
                if (*p < 0) return false;
            }
        }
    }
    if (prop->decompose != 0) {
        if ((prop->property & pr_compat) == 0 || (options & U_COMPAT) != 0) {
            if (prop->decompose > 0) {
                return udecompose((uint16_t)prop->decompose, d, options);
            }
            if (prop->decompose > -16384) {
                const int16_t *p;
                for (p = &usequences[-prop->decompose];; p++) {
                    unichar_t ch2 = (uint16_t)abs(*p);
                    if (ch2 < 0x80 || (uint16_t)(ch2 - 0x300) < 0x40U) {
                        if (d->p >= d->len && extend_ubuff(d)) return true;
                        d->data[d->p++] = ch2;
                    } else if (udecompose(ch2, d, options)) return true;
                    if (*p < 0) return false;
                }
            } else {
                const int32_t *p;
                for (p = &usequences2[-prop->decompose - 16384];; p++) {
                    if (udecompose((uint32_t)abs(*p), d, options)) return true;
                    if (*p < 0) return false;
                }
            }
        }
    }
    if (d->p >= d->len && extend_ubuff(d)) return true;
    d->data[d->p++] = ch;
    return false;
}

static void unormalize(struct ubuff_s *d) {
    uint32_t pos, max;
    if (d->p < 2) return;
    pos = 0;
    max = d->p - 1;
    while (pos < max) {
        unichar_t ch2 = d->data[pos + 1];
        if (ch2 >= 0x300) {
            uint8_t cc2 = uget_property(ch2)->combclass;
            if (cc2 != 0) {
                unichar_t ch1 = d->data[pos];
                uint8_t cc1 = uget_property(ch1)->combclass;
                if (cc1 > cc2) {
                    d->data[pos] = ch2;
                    d->data[pos + 1] = ch1;
                    if (pos != 0) {
                        pos--;
                        continue;
                    }
                }
            }
        }
        pos++;
    }
}

static MUST_CHECK bool ucompose(const struct ubuff_s *buff, struct ubuff_s *d) {
    const struct properties_s *prop, *sprop = NULL;
    unichar_t ch;
    int mclass = -1;
    uint32_t i, sp = ~(uint32_t)0;
    d->p = 0;
    for (i = 0; i < buff->p; i++) {
        ch = buff->data[i];
        prop = uget_property(ch);
        if (sp != ~(uint32_t)0 && prop->combclass > mclass) {
            unichar_t sc = d->data[sp];
            if (sc >= 0xac00) {
                unichar_t hs = sc - 0xac00;
                if (hs < 588*19 && (hs % 28) == 0) {
                    if (ch >= 0x11a7 && ch < 0x11a7 + 28) {
                        d->data[sp] = sc + ch - 0x11a7;
                        sprop = NULL;
                        continue;
                    }
                }
            } else if (sc >= 0x1100 && sc < 0x1100 + 19 && ch >= 0x1161 && ch < 0x1161 + 21) {
                d->data[sp] = 0xac00 + (ch - 0x1161 + (sc - 0x1100) * 21) * 28;
                sprop = NULL;
                continue;
            }
            if (sprop == NULL) sprop = uget_property(sc);
            if (sprop->base >= 0 && prop->diar >= 0) {
                int16_t comp = ucomposing[sprop->base + prop->diar];
                if (comp != 0) {
                    d->data[sp] = (comp > 0) ? (uint16_t)comp : ucomposed[-comp];
                    sprop = NULL;
                    continue;
                }
            }
        }
        if (prop->combclass != 0) {
            if (prop->combclass > mclass) {
                mclass = prop->combclass;
            }
        } else {
            sp = d->p;
            sprop = prop;
            mclass = -1;
        }
        if (d->p >= d->len && extend_ubuff(d)) return true;
        d->data[d->p++] = ch;
    }
    return false;
}

MUST_CHECK bool unfc(struct ubuff_s *b) {
    uint32_t i;
    static struct ubuff_s dbuf;
    if (b == NULL) {
        free(dbuf.data);
        return false;
    }
    for (dbuf.p = i = 0; i < b->p; i++) {
        if (udecompose(b->data[i], &dbuf, 0)) return true;
    }
    unormalize(&dbuf);
    return ucompose(&dbuf, b);
}

MUST_CHECK bool unfkc(str_t *s1, const str_t *s2, int mode) {
    const uint8_t *d;
    uint8_t *s;
    size_t i, l;
    uint32_t j;
    static struct ubuff_s dbuf, dbuf2;
    if (s2 == NULL) {
        free(dbuf.data);
        free(dbuf2.data);
        return false;
    }
    mode = ((mode != 0) ? U_CASEFOLD : 0) | U_COMPAT;
    d = s2->data;
    dbuf.p = 0;
    for (i = 0; i < s2->len;) {
        unichar_t ch = d[i];
        if ((ch & 0x80) != 0) {
            i += utf8in(d + i, &ch);
            if (udecompose(ch, &dbuf, mode)) return true;
            continue;
        }
        if ((mode & U_CASEFOLD) != 0 && ch >= 'A' && ch <= 'Z') ch |= 0x20;
        if (dbuf.p >= dbuf.len && extend_ubuff(&dbuf)) return true;
        dbuf.data[dbuf.p++] = ch;
        i++;
    }
    unormalize(&dbuf);
    if (ucompose(&dbuf, &dbuf2)) return true;
    l = 0;
    for (j = 0; j < dbuf2.p; j++) {
        unichar_t ch = dbuf2.data[j];
        l += (ch != 0 && ch < 0x80) ? 1 : utf8outlen(ch);
    }
    s = (uint8_t *)s1->data;
    if (l > s1->len) {
        s = reallocate_array(s, l);
        if (s == NULL) return true;
        s1->data = s;
    }
    s1->len = l;
    for (j = 0; j < dbuf2.p; j++) {
        unichar_t ch = dbuf2.data[j];
        if (ch != 0 && ch < 0x80) {
            *s++ = (uint8_t)ch;
            continue;
        }
        s += utf8out(ch, s);
    }
    return false;
}

size_t argv_print(const char *line, FILE *f) {
    size_t len = 0;
    const uint8_t *i = (const uint8_t *)line;
#ifdef _WIN32
    size_t back;
    bool quote = false, space = false;

    for (;;i++) {
        switch (*i) {
        case '%':
        case '"': quote = true; if (!space) continue; break;
        case ' ': space = true; if (!quote) continue; break;
        case 0: break;
        default: continue;
        }
        break;
    }

    if (space) {
        if (quote) {len++;putc('^', f);}
        len++;putc('"', f);
    }
    i = (const uint8_t *)line; back = 0;
    for (;;) {
        int ch = *i;
        if ((ch & 0x80) != 0) {
            unichar_t ch2 = (uint8_t)ch;
            unsigned int ln = utf8in(i, &ch2);
            if (iswprint((wint_t)ch2) != 0) {
                int ln2;
                char tmp[64];
                memcpy(tmp, i, ln);
                tmp[ln] = 0;
                ln2 = fwprintf(f, L"%S", tmp);
                if (ln2 > 0) {
                    i += ln;
                    len += (unsigned int)ln2;
                    continue;
                }
            }
            i += ln;
            len++;putc('?', f);
            continue;
        }
        if (ch == 0) break;

        if (ch == '\\') {
            back++;
            i++;
            len++;putc('\\', f);
            continue;
        }
        if (!space || quote) {
            if (strchr("()%!^<>&|\"", ch) != NULL) {
                if (ch == '"') {
                    while ((back--) != 0) {len++;putc('\\', f);}
                    len++;putc('\\', f);
                }
                len++;putc('^', f);
            }
        } else {
            if (ch == '%') {
                len++;putc('^', f);
            }
        }
        back = 0;

        i++;
        if (isprint(ch) == 0) ch = '?';
        len++;putc(ch, f);
    }
    if (space) {
        while ((back--) != 0) {len++;putc('\\', f);}
        if (quote) {len++;putc('^', f);}
        len++;putc('"', f);
    }
#else
    bool quote = strchr(line, '!') == NULL && strpbrk(line, " \"$&()*;<>'?[\\]`{|}") != NULL;

    if (quote) {len++;putc('"', f);}
    else {
        switch (line[0]) {
        case '~':
        case '#': len++;putc('\\', f); break;
        }
    }
    for (;;) {
        int ch = *i;
        if ((ch & 0x80) != 0) {
            unichar_t ch2 = (uint8_t)ch;
            int ln2;
            i += utf8in(i, &ch2);
            if (iswprint((wint_t)ch2) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch2, &ps);
                if (ln != (size_t)-1) {
                    len += fwrite(temp, ln, 1, f);
                    continue;
                }
            }
            ln2 = fprintf(f, ch2 < 0x10000 ? "$'\\u%" PRIx32 "'" : "$'\\U%" PRIx32 "'", ch2);
            if (ln2 > 0) len += (unsigned int)ln2;
            continue;
        }
        if (ch == 0) break;

        if (quote) {
            if (strchr("$`\"\\", ch) != NULL) {len++;putc('\\', f);}
        } else {
            if (strchr(" !\"$&()*;<>'?[\\]`{|}", ch) != NULL) {
                len++;putc('\\', f);
            }
        }

        i++;
        if (isprint(ch) == 0) {
            int ln = fprintf(f, "$'\\x%x'", ch);
            if (ln > 0) len += (unsigned int)ln;
            continue;
        }
        len++;putc(ch, f);
    }
    if (quote) {len++;putc('"', f);}
#endif
    return len;
}

size_t makefile_print(const char *line, FILE *f) {
    const uint8_t *i = (const uint8_t *)line;
    size_t len = 0, bl = 0;

    for (;;) {
        int ch = *i;
        if ((ch & 0x80) != 0) {
            unichar_t ch2 = (uint8_t)ch;
#ifdef _WIN32
            unsigned int ln = utf8in(i, &ch2);
            if (iswprint((wint_t)ch2) != 0) {
                int ln2;
                char tmp[64];
                memcpy(tmp, i, ln);
                tmp[ln] = 0;
                ln2 = fwprintf(f, L"%S", tmp);
                if (ln2 > 0) {
                    i += ln;
                    len += (unsigned int)ln2;
                    bl = 0;
                    continue;
                }
            }
            i += ln;
#else
            i += utf8in(i, &ch2);
            if (iswprint((wint_t)ch2) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch2, &ps);
                if (ln != (size_t)-1) {
                    len += fwrite(temp, ln, 1, f);
                    bl = 0;
                    continue;
                }
            }
#endif
            len++;putc('?', f);
            bl = 0;
            continue;
        }
        if (ch == 0) break;

        switch (ch) {
        case '\\':
            bl++;
            break;
        case ' ':
        case '#':
            while (bl > 0) {
                len++; putc('\\', f);
                bl--;
            }
            putc('\\', f);
            break;
        case '$':
            len++; putc('$', f);
            FALL_THROUGH; /* fall through */
        default:
            bl = 0;
            break;
        }

        i++;
        if (isprint(ch) == 0) ch = '?';
        len++; putc(ch, f);
    }
    return len;
}

static int unknown_print(FILE *f, unichar_t ch) {
    char temp[64];
    const char *format = (ch >= 256) ? "<U+%" PRIX32 ">" : "<%02" PRIX32 ">";
    if (f != NULL) {
        int ln;
        if (console_use_color) console_reverse(f);
        ln = fprintf(f, format, ch);
        if (console_use_color) {
            if (console_use_bold) console_defaultbold(f); else console_default(f);
        }
        return ln;
    }
    return sprintf(temp, format, ch);
}

void printable_print(const uint8_t *l, FILE *f) {
#ifdef _WIN32
    const uint8_t *i = l;
    for (;;) {
        unichar_t ch;
        if ((*i >= 0x20 && *i <= 0x7e) || *i == 0x09) {
            i++;
            continue;
        }
        if (l != i) fwrite(l, 1, (size_t)(i - l), f);
        if (*i == 0) break;
        ch = *i;
        if ((ch & 0x80) != 0) {
            unsigned int ln = utf8in(i, &ch);
            if (iswprint((wint_t)ch) != 0) {
                char tmp[64];
                memcpy(tmp, i, ln);
                tmp[ln] = 0;
                if (fwprintf(f, L"%S", tmp) > 0) {
                    i += ln;
                    l = i;
                    continue;
                }
            }
            i += ln;
        } else i++;
        l = i;
        unknown_print(f, ch);
    }
#else
    const uint8_t *i = l;
    for (;;) {
        unichar_t ch;
        if likely((*i >= 0x20 && *i <= 0x7e) || *i == 0x09) {
            i++;
            continue;
        }
        if (l != i) fwrite(l, 1, (size_t)(i - l), f);
        if (*i == 0) break;
        ch = *i;
        if ((ch & 0x80) != 0) {
            i += utf8in(i, &ch);
            if (iswprint((wint_t)ch) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch, &ps);
                if (ln != (size_t)-1) {
                    fwrite(temp, ln, 1, f);
                    l = i;
                    continue;
                }
            }
        } else i++;
        unknown_print(f, ch);
        l = i;
    }
#endif
}

size_t printable_print2(const uint8_t *line, FILE *f, size_t max) {
#ifdef _WIN32
    size_t i, l = 0, len = 0;
    int err;
    for (i = 0; i < max;) {
        unichar_t ch = line[i];
        if ((ch & 0x80) != 0) {
            unsigned int ln;
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            ln = utf8in(line + i, &ch);
            if (iswprint((wint_t)ch) != 0) {
                char tmp[64];
                memcpy(tmp, line + i, ln);
                tmp[ln] = 0;
                if (fwprintf(f, L"%S", tmp) >= 0) {
                    i += ln;
                    l = i;
                    len++;
                    continue;
                }
            }
            i += ln;
            l = i;
            err = unknown_print(f, ch);
            if (err > 0) len += (unsigned int)err;
            continue;
        }
        if ((ch < 0x20 && ch != 0x09) || ch > 0x7e) {
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            i++;
            l = i;
            err = unknown_print(f, ch);
            if (err > 0) len += (unsigned int)err;
            continue;
        }
        i++;
    }
    if (i != l) len += fwrite(line + l, 1, i - l, f);
    return len;
#else
    size_t i, l = 0, len = 0;
    int err;
    for (i = 0; i < max;) {
        unichar_t ch = line[i];
        if ((ch & 0x80) != 0) {
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            i += utf8in(line + i, &ch);
            l = i;
            if (iswprint((wint_t)ch) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch, &ps);
                if (ln != (size_t)-1) {
                    len += fwrite(temp, ln, 1, f); /* 1 character */
                    continue;
                }
            }
            err = unknown_print(f, ch);
            if (err > 0) len += (unsigned int)err;
            continue;
        }
        if ((ch < 0x20 && ch != 0x09) || ch > 0x7e) {
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            i++;
            l = i;
            err = unknown_print(f, ch);
            if (err > 0) len += (unsigned int)err;
            continue;
        }
        i++;
    }
    if (i != l) len += fwrite(line + l, 1, i - l, f);
    return len;
#endif
}

void caret_print(const uint8_t *line, FILE *f, size_t max) {
    size_t i, l = 0;
    int err;
    for (i = 0; i < max;) {
        unichar_t ch = line[i];
        if ((ch & 0x80) != 0) {
#ifdef _WIN32
            unsigned int ln = utf8in(line + i, &ch);
            if (iswprint((wint_t)ch) != 0) {
                char tmp[64];
                wchar_t tmp2[64];
                memcpy(tmp, line + i, ln);
                tmp[ln] = 0;
                if (swprintf(tmp2, lenof(tmp2), L"%S", tmp) > 0) {
                    int width = wcwidth_v9(ch);
                    if (width > 0) l += (unsigned int)width;
                    i += ln;
                    continue;
                }
            }
            i += ln;
#else
            i += utf8in(line + i, &ch);
            if (iswprint((wint_t)ch) != 0) {
                char temp[64];
                mbstate_t ps;
                memset(&ps, 0, sizeof ps);
                if (wcrtomb(temp, (wchar_t)ch, &ps) != (size_t)-1) {
                    int width = wcwidth_v9(ch);
                    if (width > 0) l += (unsigned int)width;
                    continue;
                }
            }
#endif
            err = unknown_print(NULL, ch);
            if (err > 0) l += (unsigned int)err;
            continue;
        }
        if (ch == 0) break;
        if (ch == '\t') {
            while (l != 0) {
                putc(' ', f);
                l--;
            }
            putc('\t', f);
            i++;
            continue;
        }
        if (ch < 0x20 || ch > 0x7e) {
            err = unknown_print(NULL, ch);
            if (err > 0) l += (unsigned int)err;
        } else l++;
        i++;
    }
    while (l != 0) {
        putc(' ', f);
        l--;
    }
}

size_t calcpos(const uint8_t *line, size_t pos) {
    size_t s, l;
    s = l = 0;
    while (s < pos) {
        if (line[s] == 0) return l;
        s += utf8len(line[s]);
        l++;
    }
    return l;
}

#ifdef _WIN32
MUST_CHECK wchar_t *utf8_to_wchar(const char *name, size_t max) {
    enum { REPLACEMENT_CHARACTER = 0xfffd };
    wchar_t *wname;
    unichar_t ch;
    size_t i = 0, j = 0, len = ((max != SIZE_MAX) ? max : strlen(name)) + 2;
    wname = allocate_array(wchar_t, len);
    if (wname == NULL) return NULL;
    while (name[i] != 0 && i < max) {
        ch = (uint8_t)name[i];
        if ((ch & 0x80) != 0) {
            i += utf8in((const uint8_t *)name + i, &ch);
            if (ch == 0) ch = REPLACEMENT_CHARACTER;
        } else i++;
        if (j + 3 > len) {
            wchar_t *d;
            if (inc_overflow(&len, 64)) goto failed;
            d = reallocate_array(wname, len);
            if (d == NULL) goto failed;
            wname = d;
        }
        if (ch < 0x10000) {
        } else if (ch < 0x110000) {
            wname[j++] = (wchar_t)((ch >> 10) + 0xd7c0);
            ch = (ch & 0x3ff) | 0xdc00;
        } else ch = REPLACEMENT_CHARACTER;
        wname[j++] = (wchar_t)ch;
    }
    wname[j] = 0;
    return wname;
failed:
    free(wname);
    return NULL;
}
#endif

FILE *fopen_utf8(const char *name, const char *mode) {
    FILE *f;
#ifdef _WIN32
    wchar_t *wname, *c2, wmode[3];
    const uint8_t *c;
    wname = utf8_to_wchar(name, SIZE_MAX);
    if (wname == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    c2 = wmode; c = (uint8_t *)mode;
    while ((*c2++=(wchar_t)*c++) != 0);
    f = _wfopen(wname, wmode);
    free(wname);
#else
    size_t len = 0, max = strlen(name) + 1;
    char *newname = allocate_array(char, max);
    const uint8_t *c = (const uint8_t *)name;
    unichar_t ch;
    mbstate_t ps;
    errno = ENOMEM;
    f = NULL;
    if (newname == NULL || max < 1) goto failed;
    memset(&ps, 0, sizeof ps);
    do {
        char temp[64];
        ssize_t l;
        ch = *c;
        if ((ch & 0x80) != 0) {
            c += utf8in(c, &ch);
            if (ch == 0) {errno = ENOENT; goto failed;}
        } else c++;
        l = (ssize_t)wcrtomb(temp, (wchar_t)ch, &ps);
        if (l <= 0) goto failed;
        len += (size_t)l;
        if (len < (size_t)l) goto failed;
        if (len > max) {
            char *d;
            if (add_overflow(len, 64, &max)) goto failed;
            d = reallocate_array(newname, max);
            if (d == NULL) goto failed;
            newname = d;
        }
        memcpy(newname + len - l, temp, (size_t)l);
    } while (ch != 0);
    errno = 0;
    f = fopen(newname, mode);
    if (f == NULL && errno == 0) errno = (mode[0] == 'r') ? ENOENT : EINVAL;
failed:
    free(newname);
#endif
    return f;
}

const char *unicode_character_name(unichar_t ch) {
    const char *txt;
    switch (ch) {
    case 0x20: txt = " space"; break;
    case 0x22: txt = " quotation mark"; break;
    case 0x27: txt = " apostrophe"; break;
    case 0x2d: txt = " hyphen-minus"; break;
    case 0xa0: txt = " no-break space"; break;
    case 0x2010: txt = " hyphen"; break;
    case 0x2011: txt = " non-breaking hyphen"; break;
    case 0x2012: txt = " figure dash"; break;
    case 0x2013: txt = " en dash"; break;
    case 0x2014: txt = " em dash"; break;
    case 0x2018: txt = " left single quotation mark"; break;
    case 0x2019: txt = " right single quotation mark"; break;
    case 0x201A: txt = " single low-9 quotation mark"; break;
    case 0x201B: txt = " single high-reversed-9 quotation mark"; break;
    case 0x201C: txt = " left double quotation mark"; break;
    case 0x201D: txt = " right double quotation mark"; break;
    case 0x201E: txt = " double low-9 quotation mark"; break;
    case 0x201F: txt = " double high-reversed-9 quotation mark"; break;
    case 0x2212: txt = " minus sign"; break;
    case 0xFEFF: txt = " zero width no-break space"; break;
    default: txt = NULL;
    }
    return txt;
}
