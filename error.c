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
#include "error.h"
#include <string.h>
#include <errno.h>
#include "wchar.h"
#ifdef _WIN32
#include <locale.h>
#endif
#include "file.h"
#include "64tass.h"
#include "unicode.h"
#include "eval.h"
#include "arguments.h"
#include "opcodes.h"
#include "section.h"
#include "macro.h"

#include "strobj.h"
#include "addressobj.h"
#include "registerobj.h"
#include "namespaceobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "labelobj.h"
#include "errorobj.h"
#include "noneobj.h"
#include "identobj.h"

#ifdef COLOR_OUTPUT
bool print_use_color = false;
bool print_use_bold = false;
#endif

struct file_list_s *current_file_list;

#define ALIGN(v) (((v) + (sizeof(int *) - 1)) & ~(sizeof(int *) - 1))

static unsigned int errors = 0, warnings = 0;

static struct file_list_s file_list;
static struct file_s file_list_file;
static const struct file_list_s *included_from = &file_list;
static const char *prgname;

struct errorbuffer_s {
    size_t max;
    size_t len;
    size_t header_pos;
    uint8_t *data;
    struct avltree members;
};

static struct errorbuffer_s error_list;
static struct avltree notdefines;

typedef enum Severity_types {
    SV_NOTE, SV_WARNING, SV_NONEERROR, SV_ERROR, SV_FATAL
} Severity_types;

struct errorentry_s {
    Severity_types severity;
    size_t error_len;
    size_t line_len;
    const struct file_list_s *file_list;
    struct linepos_s epoint;
    linecpos_t caret;
    struct avltree_node node;
};

struct notdefines_s {
    str_t cfname;
    const struct file_list_s *file_list;
    struct linepos_s epoint;
    uint8_t pass;
    struct avltree_node node;
};

static FAST_CALL int duplicate_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct errorentry_s *a = cavltree_container_of(aa, struct errorentry_s, node);
    const struct errorentry_s *b = cavltree_container_of(bb, struct errorentry_s, node);
    const uint8_t *aerr, *berr;

    if (a->severity != b->severity) return a->severity - b->severity;
    if (a->file_list != b->file_list) return a->file_list > b->file_list ? 1 : -1;
    if (a->error_len != b->error_len) return a->error_len > b->error_len ? 1 : -1;
    if (a->epoint.line != b->epoint.line) return a->epoint.line > b->epoint.line ? 1 : -1;
    if (a->epoint.pos != b->epoint.pos) return a->epoint.pos > b->epoint.pos ? 1 : -1;

    aerr = (const uint8_t *)(a + 1);
    berr = (const uint8_t *)(b + 1);

    if ((a->line_len | b->line_len) != 0) {
        int i;
        const uint8_t *aline, *bline;

        if (a->line_len != 0) {
            aline = aerr;
            aerr += a->line_len;
        } else if (a->epoint.line == 0) {
            aline = (const uint8_t *)"";
        } else {
            aline = &a->file_list->file->data[a->file_list->file->line[a->epoint.line - 1]];
        }

        if (b->line_len != 0) {
            bline = berr;
            berr += b->line_len;
        } else if (a->epoint.line == 0) {
            bline = (const uint8_t *)"";
        } else {
            bline = &a->file_list->file->data[a->file_list->file->line[a->epoint.line - 1]];
        }

        i = strcmp((const char *)aline, (const char *)bline);
        if (i != 0) return i;
    }
    return memcmp(aerr, berr, a->error_len);
}

static void close_error(void) {
    static bool duplicate;
    if (error_list.header_pos < error_list.len) {
        struct errorentry_s *err = (struct errorentry_s *)&error_list.data[error_list.header_pos];
        err->error_len = error_list.len - error_list.header_pos - (sizeof *err) - err->line_len;
        switch (err->severity) {
        case SV_NOTE:
            if (!duplicate) memset(&err->node, 0, sizeof err->node);
            break;
        default:
            duplicate = avltree_insert(&err->node, &error_list.members, duplicate_compare) != NULL;
        }
        if (duplicate) {
            error_list.len = error_list.header_pos;
        }
        error_list.header_pos = ALIGN(error_list.len);
    }
}

static void error_extend(void) {
    struct errorentry_s *err;
    uint8_t *data = (uint8_t *)realloc(error_list.data, error_list.max);
    size_t diff, pos;
    if (data == NULL) err_msg_out_of_memory2();
    diff = data - error_list.data;
    error_list.data = data;
    if (diff == 0) return;
    for (pos = 0; pos < error_list.header_pos; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (struct errorentry_s *)&data[pos];
        if (err->node.left != NULL) err->node.left = (struct avltree_node *)((uint8_t *)err->node.left + diff);
        if (err->node.right != NULL) err->node.right = (struct avltree_node *)((uint8_t *)err->node.right + diff);
        if (err->node.parent != NULL) err->node.parent = (struct avltree_node *)((uint8_t *)err->node.parent + diff);
    }
    if (error_list.members.root != NULL) error_list.members.root = (struct avltree_node *)((uint8_t *)error_list.members.root + diff);
    if (error_list.members.first != NULL) error_list.members.first = (struct avltree_node *)((uint8_t *)error_list.members.first + diff);
}

static void new_error_msg_common(Severity_types severity, const struct file_list_s *flist, linepos_t epoint, size_t line_len, size_t pos) {
    struct errorentry_s *err;
    close_error();
    error_list.len = error_list.header_pos + sizeof *err;
    if (error_list.len < sizeof *err) err_msg_out_of_memory2(); /* overflow */
    error_list.len += line_len;
    if (error_list.len < line_len) err_msg_out_of_memory2(); /* overflow */
    if (error_list.len > error_list.max) {
        error_list.max = error_list.len + 0x200;
        if (error_list.max < 0x200) err_msg_out_of_memory2(); /* overflow */
        error_extend();
    }
    err = (struct errorentry_s *)&error_list.data[error_list.header_pos];
    err->severity = severity;
    err->error_len = 0;
    err->line_len = line_len;
    err->file_list = flist;
    err->epoint.line = epoint->line;
    err->epoint.pos = pos;
    err->caret = epoint->pos;
}

static struct {
    Severity_types severity;
    const struct file_list_s *flist;
    linepos_t epoint;
} new_error_msg_more_param;

static void new_error_msg_more(void);
static bool new_error_msg(Severity_types severity, const struct file_list_s *flist, linepos_t epoint) {
    size_t line_len;
    if (in_macro && flist == current_file_list && epoint->line == lpoint.line) {
        struct linepos_s opoint;
        const struct file_list_s *eflist = macro_error_translate(&opoint, epoint->pos);
        if (eflist != NULL) {
            new_error_msg_common(severity, eflist, &opoint, 0, opoint.pos);
            new_error_msg_more_param.severity = severity;
            new_error_msg_more_param.flist = flist;
            new_error_msg_more_param.epoint = epoint;
            return true;
        }
    }
    switch (severity) {
    case SV_NOTE: line_len = 0; break;
    default: line_len = ((epoint->line == lpoint.line) && (size_t)(pline - flist->file->data) >= flist->file->len) ? (strlen((const char *)pline) + 1) : 0; break;
    }
    new_error_msg_common(severity, flist, epoint, line_len, macro_error_translate2(epoint->pos));
    if (line_len != 0) memcpy(&error_list.data[error_list.header_pos + sizeof(struct errorentry_s)], pline, line_len);
    return false;
}

static bool new_error_msg_err(const Error *err) {
    struct linepos_s opoint;
    size_t line_len;
    if (in_macro && err->file_list == current_file_list && err->epoint.line == lpoint.line) {
        const struct file_list_s *eflist = macro_error_translate(&opoint, err->caret);
        if (eflist != NULL) {
            new_error_msg_common(SV_ERROR, eflist, &opoint, 0, opoint.pos);
            return true;
        }
    }
    if (err->line == NULL) {
        opoint = err->epoint;
        line_len = 0;
    } else {
        opoint.line = err->epoint.line;
        opoint.pos = err->caret;
        line_len = strlen((const char *)err->line) + 1;
    }
    new_error_msg_common(SV_ERROR, err->file_list, &opoint, line_len, err->epoint.pos);
    if (line_len != 0) memcpy(&error_list.data[error_list.header_pos + sizeof(struct errorentry_s)], err->line, line_len);
    return false;
}

static void new_error_msg2(bool type, linepos_t epoint) {
    Severity_types severity = type ? SV_ERROR : SV_WARNING;
    bool more = new_error_msg(severity, current_file_list, epoint);
    if (more) new_error_msg_more();
}

static FAST_CALL int file_list_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct file_list_s *a = cavltree_container_of(aa, struct file_list_s, node);
    const struct file_list_s *b = cavltree_container_of(bb, struct file_list_s, node);
    if (a->epoint.line != b->epoint.line) return a->epoint.line > b->epoint.line ? 1 : -1;
    if (a->epoint.pos != b->epoint.pos) return a->epoint.pos > b->epoint.pos ? 1 : -1;
    return a->file->uid - b->file->uid;
}

static struct file_lists_s {
    struct file_list_s file_lists[90];
    struct file_lists_s *next;
} *file_lists = NULL;

static struct file_list_s *lastfl;
static int file_listsp;
void enterfile(struct file_s *file, linepos_t epoint) {
    struct avltree_node *b;
    lastfl->file = file;
    lastfl->epoint = *epoint;
    b = avltree_insert(&lastfl->node, &current_file_list->members, file_list_compare);
    if (b == NULL) {
        lastfl->parent = current_file_list;
        avltree_init(&lastfl->members);
        current_file_list = lastfl;
        if (file_listsp == 89) {
            struct file_lists_s *old = file_lists;
            file_lists = (struct file_lists_s *)mallocx(sizeof *file_lists);
            file_lists->next = old;
            file_listsp = 0;
        } else file_listsp++;
        lastfl = &file_lists->file_lists[file_listsp];
    } else {
        current_file_list = avltree_container_of(b, struct file_list_s, node);
    }
    current_file_list->pass = pass;
}

void exitfile(void) {
    if (current_file_list->parent != NULL) current_file_list = current_file_list->parent;
}

static void adderror2(const uint8_t *s, size_t len) {
    if (len + error_list.len > error_list.max) {
        error_list.max += (len > 0x200) ? len : 0x200;
        error_extend();
    }
    memcpy(error_list.data + error_list.len, s, len);
    error_list.len += len;
}

static void adderror(const char *s) {
    adderror2((const uint8_t *)s, strlen(s));
}

static const char * const terr_warning[] = {
    "deprecated modulo operator, use '%' instead",
    "deprecated not equal operator, use '!=' instead",
    "deprecated directive, only for TASM compatible mode",
    "please use format(\"%d\", ...) as '^' will change it's meaning",
    "please use quotes now to allow expressions in future",
    "constant result, possibly changeable to 'lda'",
    "independent result, possibly changeable to 'lda'",
#ifdef _WIN32
    "the file's real name is not '",
#endif
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    "use '/' as path separation '",
#else
    "this name uses reserved characters '",
#endif
    "use relative path for '"
};

static const char * const terr_error[] = {
    "double defined range",
    "double defined escape",
    "extra characters on line",
    "more than two characters",
    "floating point overflow",
    "general syntax",
    "expression syntax",
    "label required",
    "division by zero",
    "zero value not allowed",
    "most significiant bit must be clear in byte",
    "at least one byte is needed",
    "last byte must not be gap",
    "address in different program bank ",
    "address out of section",
    "negative number raised on fractional power",
    "square root of negative number ",
    "logarithm of non-positive number ",
    "not in range -1.0 to 1.0 ",
    "empty range not allowed",
    "empty string not allowed",
    "empty list not allowed",
    "more than a single character",
    "requirements not met",
    "conflict",
    "index out of range ",
    "key error ",
    "offset out of range",
    "not hashable ",
    "not a key and value pair ",
    "too large for a %u bit signed integer ",
    "too large for a %u bit unsigned integer ",
    "value needs to be non-negative ",
    "operands could not be broadcast together with shapes %" PRIuSIZE " and %" PRIuSIZE,
    "can't get sign of ",
    "can't get absolute value of ",
    "can't get integer value of ",
    "can't get length of ",
    "can't get size of ",
    "can't get boolean value of ",
    "not iterable ",
    "no byte sized",
    "no word sized",
    "no long sized",
    "not a direct page address ",
    "not a data bank address ",
    "not a bank 0 address ",
    "out of memory",
    "addressing mode too complex",
    "empty encoding, add something or correct name",
    "closing directive '",
    "opening directive '",
    "must be used within a loop"
};

static const char * const terr_fatal[] = {
    "can't open file",
    "error reading file",
    "can't write object file",
    "can't write listing file",
    "can't write label file",
    "can't write make file",
    "can't write error file",
    "file recursion",
    "macro recursion too deep",
    "function recursion too deep",
    "weak recursion too deep",
    "too many passes"
};

static void err_msg_variable(Obj *val) {
    Obj *err;
    adderror(val->obj->name);
    err = val->obj->str(val, NULL, 40);
    if (err != NULL) {
        if (err->obj == STR_OBJ) {
            Str *str = (Str *)err;
            adderror(" '");
            adderror2(str->data, str->len);
            adderror("'");
        }
        val_destroy(err);
    }
}

static void str_name(const uint8_t *data, size_t len) {
    adderror(" '");
    if (len != 0) {
        if (data[0] == '-') {
            adderror("-");
        } else if (data[0] == '+') {
            adderror("+");
        } else if (data[0] == '.' || data[0] == '#') {
            adderror("<anonymous>");
        } else adderror2(data, len);
    }
    adderror("'");
}

void err_msg2(Error_types no, const void *prm, linepos_t epoint) {
    bool more;
    if (no < 0x40) {
        switch (no) {
        case ERROR___OPTIMIZABLE:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror("could be shorter by using '");
            adderror((const char *)prm);
            adderror("' instead");
            adderror(" [-Woptimize]");
            break;
        case ERROR______SIMPLIFY:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror("could be simpler by using '");
            adderror((const char *)prm);
            adderror("' instead");
            adderror(" [-Woptimize]");
            break;
        case ERROR_____REDUNDANT:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror("possibly redundant ");
            adderror((const char *)prm);
            adderror(" [-Woptimize]");
            break;
        case ERROR__CONST_RESULT:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror(terr_warning[no]);
            adderror(" [-Woptimize]");
            break;
        case ERROR_______OLD_NEQ:
        case ERROR____OLD_MODULO:
        case ERROR____OLD_STRING:
        case ERROR_______OLD_ENC:
            new_error_msg2(diagnostic_errors.deprecated, epoint);
            adderror(terr_warning[no]);
            adderror(" [-Wdeprecated]");
            break;
        case ERROR_____OLD_EQUAL:
            new_error_msg2(diagnostic_errors.old_equal, epoint);
            adderror("deprecated equal operator, use '==' instead [-Wold-equal]");
            break;
        case ERROR_DUPLICATECASE:
            new_error_msg2(diagnostic_errors.switch_case, epoint);
            adderror("case ignored, value already handled [-Wswitch-case]");
            break;
        case ERROR_NONIMMEDCONST:
            new_error_msg2(diagnostic_errors.immediate, epoint);
            adderror("immediate addressing mode suggested [-Wimmediate]");
            break;
        case ERROR_LEADING_ZEROS:
            new_error_msg2(diagnostic_errors.leading_zeros, epoint);
            adderror("leading zeros ignored [-Wleading-zeros]");
            break;
        case ERROR_DIRECTIVE_IGN:
            new_error_msg2(diagnostic_errors.ignored, epoint);
            adderror("directive ignored [-Wignored]");
            break;
        case ERROR_FLOAT_COMPARE:
            new_error_msg2(diagnostic_errors.float_compare, epoint);
            adderror("approximate floating point ");
            adderror((const char *)prm);
            adderror("' [-Wfloat-compare]");
            break;
        case ERROR___FLOAT_ROUND:
            new_error_msg2(diagnostic_errors.float_round, epoint);
            adderror("implicit floating point rounding [-Wfloat-round]");
            break;
        case ERROR___LONG_BRANCH:
            new_error_msg2(diagnostic_errors.long_branch, epoint);
            adderror("long branch used [-Wlong-branch]");
            break;
        case ERROR_WUSER_DEFINED:
            more = new_error_msg(SV_WARNING, current_file_list, epoint);
            adderror2(((const Str *)prm)->data, ((const Str *)prm)->len);
            if (more) new_error_msg_more();
            break;
#ifdef _WIN32
        case ERROR___INSENSITIVE:
#endif
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
        case ERROR_____BACKSLASH:
#else
        case ERROR__RESERVED_CHR:
#endif
        case ERROR_ABSOLUTE_PATH:
            new_error_msg2(diagnostic_errors.portable, epoint);
            adderror(terr_warning[no]);
            adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
            adderror("' [-Wportable]");
            break;
        default:
            more = new_error_msg(SV_WARNING, current_file_list, epoint);
            adderror(terr_warning[no]);
            if (more) new_error_msg_more();
            break;
        }
        return;
    }

    if (no < 0xc0) {
        char line[1024];
        more = new_error_msg(SV_ERROR, current_file_list, epoint);
        switch (no) {
        case ERROR_BRANCH_TOOFAR:
            sprintf(line,"branch too far by %+d bytes", *(const int *)prm); adderror(line);
            break;
        case ERROR____PTEXT_LONG:
            sprintf(line,"ptext too long by %" PRIuSIZE " bytes", *(const size_t *)prm - 0x100); adderror(line);
            break;
        case ERROR__BRANCH_CROSS:
            sprintf(line,"branch crosses page by %+d bytes", *(const int *)prm); adderror(line);
            break;
        case ERROR__USER_DEFINED:
            adderror2(((const Str *)prm)->data, ((const Str *)prm)->len);
            break;
        case ERROR______EXPECTED:
            adderror((const char *)prm);
            adderror(" expected");
            break;
        case ERROR__MISSING_OPEN:
        case ERROR_MISSING_CLOSE:
            adderror(terr_error[no - 0x40]);
            adderror((const char *)prm);
            adderror("' not found");
            break;
        case ERROR___NOT_ALLOWED:
            adderror("not allowed here: ");
            adderror((const char *)prm);
            break;
        case ERROR_RESERVED_LABL:
            adderror("reserved symbol name '");
            adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
            adderror("'");
            break;
        case ERROR_____NOT_BANK0:
        case ERROR____NOT_DIRECT:
        case ERROR__NOT_DATABANK:
        case ERROR_CANT_CROSS_BA:
            adderror(terr_error[no - 0x40]);
            if (prm != NULL) err_msg_variable((Obj *)prm);
            break;
        case ERROR___UNKNOWN_CPU:
            adderror("unknown processor '");
            adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
            adderror("'");
            break;
        default:
            adderror(terr_error[no - 0x40]);
        }
        if (more) new_error_msg_more();
        return;
    }

    more = new_error_msg(SV_FATAL, current_file_list, epoint);
    switch (no) {
    case ERROR_UNKNOWN_OPTIO:
        adderror("unknown option '");
        adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
        adderror("'");
        break;
    case ERROR____LABEL_ROOT:
        adderror("scope '");
        adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
        adderror("' for label listing not found");
        break;
    case ERROR__SECTION_ROOT:
        adderror("section '");
        adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
        adderror("' for output not found");
        break;
    default:
        adderror(terr_fatal[no - 0xc0]);
    }
    if (more) new_error_msg_more();
}

void err_msg(Error_types no, const void* prm) {
    err_msg2(no, prm, &lpoint);
}

static void err_msg_str_name(const char *msg, const str_t *name, linepos_t epoint) {
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror(msg);
    if (name != NULL) str_name(name->data, name->len);
    if (more) new_error_msg_more();
}

void err_msg_big_address(linepos_t epoint) {
    address_t addr = (current_address->l_address.address & 0xffff) | current_address->l_address.bank;
    Obj *val = get_star_value(addr, current_address->l_address_val);
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("address not in processor address space ");
    err_msg_variable(val);
    val_destroy(val);
    if (more) new_error_msg_more();
}

static void err_msg_big_integer(const char *msg, unsigned int bits, Obj *val) {
    char msg2[256];
    sprintf(msg2, msg, bits);
    adderror(msg2);
    err_msg_variable(val);
}

static void new_error_msg_err_more(const Error *err) {
    size_t line_len;
    struct linepos_s opoint;
    if (err->line == NULL) {
        opoint = err->epoint;
        line_len = 0;
    } else {
        opoint.line = err->epoint.line;
        opoint.pos = err->caret;
        line_len = strlen((const char *)err->line) + 1;
    }
    new_error_msg_common(SV_NOTE, err->file_list, &opoint, line_len, err->epoint.pos);
    if (line_len != 0) memcpy(&error_list.data[error_list.header_pos + sizeof(struct errorentry_s)], err->line, line_len);
    adderror("original location in an expanded macro was here");
}

static void new_error_msg_more() {
    const struct file_list_s *flist = new_error_msg_more_param.flist;
    linepos_t epoint = new_error_msg_more_param.epoint;
    size_t line_len;
    switch (new_error_msg_more_param.severity) {
    case SV_NOTE: line_len = 0; break;
    default: line_len = ((epoint->line == lpoint.line) && (size_t)(pline - flist->file->data) >= flist->file->len) ? (strlen((const char *)pline) + 1) : 0; break;
    }
    new_error_msg_common(SV_NOTE, flist, epoint, line_len, macro_error_translate2(epoint->pos));
    if (line_len != 0) memcpy(&error_list.data[error_list.header_pos + sizeof(struct errorentry_s)], pline, line_len);
    adderror("original location in an expanded macro was here");
}

static bool err_msg_invalid_conv(const Error *err) {
    bool more;
    Obj *v1 = err->u.conv.val;
    if (v1->obj == ERROR_OBJ) {
        err_msg_output((const Error *)v1);
        return false;
    }
    more = new_error_msg_err(err);
    adderror("conversion of ");
    err_msg_variable(v1);
    adderror(" to ");
    adderror(err->u.conv.t->name);
    adderror(" is not possible");
    return more;
}

static FAST_CALL int notdefines_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct notdefines_s *a = cavltree_container_of(aa, struct notdefines_s, node);
    const struct notdefines_s *b = cavltree_container_of(bb, struct notdefines_s, node);
    if (a->file_list != b->file_list) return a->file_list > b->file_list ? 1 : -1;
    if (a->epoint.line != b->epoint.line) return a->epoint.line > b->epoint.line ? 1 : -1;
    if (a->epoint.pos != b->epoint.pos) return a->epoint.pos > b->epoint.pos ? 1 : -1;
    return str_cmp(&a->cfname, &b->cfname);
}

static void notdefines_free(struct avltree_node *aa) {
    struct notdefines_s *a = avltree_container_of(aa, struct notdefines_s, node);
    free((uint8_t *)a->cfname.data);
    free(a);
}

static struct notdefines_s *lastnd = NULL;
static void err_msg_not_defined3(const Error *err) {
    Namespace *l = err->u.notdef.names;
    struct notdefines_s *tmp2;
    struct avltree_node *b;
    bool more;

    if (constcreated && pass < max_pass) return;

    if (lastnd == NULL) {
        lastnd = (struct notdefines_s *)mallocx(sizeof *lastnd);
    }

    if (err->u.notdef.ident->obj == IDENT_OBJ) {
        const str_t *name = &((Ident *)err->u.notdef.ident)->name;
        str_cfcpy(&lastnd->cfname, name);
        lastnd->file_list = l->file_list;
        lastnd->epoint = l->epoint;
        lastnd->pass = pass;
        b = avltree_insert(&lastnd->node, &notdefines, notdefines_compare);
        if (b != NULL) {
            tmp2 = avltree_container_of(b, struct notdefines_s, node);
            if (tmp2->pass == pass) {
                return;
            }
            tmp2->pass = pass;
        } else {
            if (lastnd->cfname.data == name->data) str_cpy(&lastnd->cfname, name);
            else str_cfcpy(&lastnd->cfname, NULL);
            lastnd = NULL;
        }
    }

    more = new_error_msg_err(err);
    adderror("not defined ");
    err_msg_variable(err->u.notdef.ident);
    if (more) new_error_msg_err_more(err);

    if (l->file_list == NULL) {
        struct linepos_s nopoint = {0, 0};
        new_error_msg(SV_NOTE, err->file_list, &nopoint);
        adderror("searched in the global scope");
    } else {
        new_error_msg(SV_NOTE, l->file_list, &l->epoint);
        if (err->u.notdef.down) adderror("searched in this scope and in all it's parents");
        else adderror("searched in this object only");
    }
}

void err_msg_not_defined2(const str_t *name, Namespace *l, bool down, linepos_t epoint) {
    Error *err = new_error(ERROR___NOT_DEFINED, epoint);
    err->u.notdef.down = down;
    err->u.notdef.names = ref_namespace(l);
    err->u.notdef.ident = (Obj *)new_ident(name);
    err_msg_not_defined3(err);
    val_destroy(&err->v);
}

static void err_opcode(uint32_t cod) {
    adderror(" addressing mode ");
    if (cod != 0) {
        char tmp[17];
        memcpy(tmp, "for opcode 'xxx'", sizeof tmp);
        tmp[12] = cod >> 16;
        tmp[13] = cod >> 8;
        tmp[14] = cod;
        adderror(tmp);
    } else adderror("accepted");
}

static void err_msg_no_addressing(atype_t addrtype, uint32_t cod) {
    adderror("no");
    if (addrtype == A_NONE) adderror(" implied");
    for (; (addrtype & MAX_ADDRESS_MASK) != 0; addrtype <<= 4) {
        const char *txt = "?";
        switch ((Address_types)((addrtype & 0xf000) >> 12)) {
        case A_NONE: continue;
        case A_IMMEDIATE: txt = " immediate"; break;
        case A_IMMEDIATE_SIGNED: txt = " signed immediate"; break;
        case A_XR: txt = " x indexed"; break;
        case A_YR: txt = " y indexed"; break;
        case A_ZR: txt = " z indexed"; break;
        case A_SR: txt = " stack"; break;
        case A_RR: txt = " data stack"; break;
        case A_DR: txt = " direct page"; break;
        case A_BR: txt = " data bank"; break;
        case A_KR: txt = " program bank"; break;
        case A_I: txt = " indirect"; break;
        case A_LI: txt = " long indirect"; break;
        }
        adderror(txt);
    }
    err_opcode(cod);
}

static void err_msg_no_register(Register *val, uint32_t cod) {
    adderror("no register '");
    adderror2(val->data, val->len);
    adderror("'");
    err_opcode(cod);
}

static void err_msg_no_lot_operand(size_t opers, uint32_t cod) {
    char msg2[256];
    sprintf(msg2, "no %" PRIuSIZE " operand", opers);
    adderror(msg2);
    err_opcode(cod);
}

static void err_msg_cant_broadcast(const char *msg, size_t v1, size_t v2) {
    char msg2[256];
    sprintf(msg2, msg, v1, v2);
    adderror(msg2);
}

static void err_msg_invalid_oper2(const Oper *op, Obj *v1, Obj *v2) {
    bool in = (op == &o_IN);
    adderror(op->name);
    adderror(in ? "' on " : "' of ");
    err_msg_variable(in ? v2 : v1);
    if (v2 != NULL && op != &o_MEMBER && op != &o_X && !in) {
        adderror(" and ");
        err_msg_variable(v2);
    }
    adderror(" not possible");
}

static void err_msg_invalid_oper3(const Error *err) {
    Obj *v1 = err->u.invoper.v1, *v2;
    bool more;
    if (v1->obj == ERROR_OBJ) {
        err_msg_output((const Error *)v1);
        return;
    }
    v2 = err->u.invoper.v2;
    if (v2 != NULL && v2->obj == ERROR_OBJ) {
        err_msg_output((const Error *)v2);
        return;
    }

    more = new_error_msg_err(err);
    err_msg_invalid_oper2(err->u.invoper.op, v1, v2);
    if (more) new_error_msg_err_more(err);
}

static bool err_msg_still_none2(const Error *err) {
    struct errorentry_s *e;
    bool more;
    if ((constcreated || !fixeddig) && pass < max_pass) return false;
    more = new_error_msg_err(err);
    e = (struct errorentry_s *)&error_list.data[error_list.header_pos];
    e->severity = SV_NONEERROR;
    adderror("can't calculate this");
    return more;
}

void err_msg_output(const Error *val) {
    bool more = false;
    switch (val->num) {
    case ERROR___NOT_DEFINED: err_msg_not_defined3(val);break;
    case ERROR__INVALID_CONV: more = err_msg_invalid_conv(val);break;
    case ERROR__INVALID_OPER: err_msg_invalid_oper3(val);break;
    case ERROR____STILL_NONE: more = err_msg_still_none2(val); break;
    case ERROR_____CANT_IVAL:
    case ERROR_____CANT_UVAL:
    case ERROR______NOT_UVAL: more = new_error_msg_err(val); err_msg_big_integer(terr_error[val->num - 0x40], val->u.intconv.bits, val->u.intconv.val); break;
    case ERROR_REQUIREMENTS_:
    case ERROR______CONFLICT:
    case ERROR_NOT_TWO_CHARS:
    case ERROR_NUMERIC_OVERF:
    case ERROR_NEGFRAC_POWER:
    case ERROR___EMPTY_RANGE:
    case ERROR__EMPTY_STRING:
    case ERROR____EMPTY_LIST:
    case ERROR__BYTES_NEEDED:
    case ERROR___NO_LAST_GAP:
    case ERROR__NOT_ONE_CHAR:
    case ERROR_NO_ZERO_VALUE:
    case ERROR_OUT_OF_MEMORY:
    case ERROR__ADDR_COMPLEX:
    case ERROR_DIVISION_BY_Z: more = new_error_msg_err(val); adderror(terr_error[val->num - 0x40]); break;
    case ERROR_NO_ADDRESSING: more = new_error_msg_err(val); err_msg_no_addressing(val->u.addressing.am, val->u.addressing.cod);break;
    case ERROR___NO_REGISTER: more = new_error_msg_err(val); err_msg_no_register(val->u.reg.reg, val->u.reg.cod);break;
    case ERROR___NO_LOT_OPER: more = new_error_msg_err(val); err_msg_no_lot_operand(val->u.opers.num, val->u.opers.cod);break;
    case ERROR_CANT_BROADCAS: more = new_error_msg_err(val); err_msg_cant_broadcast(terr_error[val->num - 0x40], val->u.broadcast.v1, val->u.broadcast.v2);break;
    case ERROR__NO_BYTE_ADDR:
    case ERROR__NO_WORD_ADDR:
    case ERROR__NO_LONG_ADDR: more = new_error_msg_err(val); adderror(terr_error[val->num - 0x40]); err_opcode(val->u.addresssize.cod); break;
    case ERROR__NOT_KEYVALUE:
    case ERROR__NOT_HASHABLE:
    case ERROR_____CANT_SIGN:
    case ERROR______CANT_ABS:
    case ERROR______CANT_INT:
    case ERROR______CANT_LEN:
    case ERROR_____CANT_SIZE:
    case ERROR_____CANT_BOOL:
    case ERROR______NOT_ITER:
    case ERROR___MATH_DOMAIN:
    case ERROR_LOG_NON_POSIT:
    case ERROR_SQUARE_ROOT_N:
    case ERROR___INDEX_RANGE:
    case ERROR_____KEY_ERROR: more = new_error_msg_err(val); adderror(terr_error[val->num - 0x40]); err_msg_variable(val->u.obj);break;
    default: break;
    }
    if (more) new_error_msg_err_more(val);
}

void err_msg_output_and_destroy(Error *val) {
    err_msg_output(val);
    val_destroy(&val->v);
}

void err_msg_wrong_type(const Obj *val, Type *expected, linepos_t epoint) {
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("wrong type '");
    adderror(val->obj->name);
    if (expected != NULL) {
        adderror("', expected '");
        adderror(expected->name);
    }
    adderror("'");
    if (more) new_error_msg_more();
}

void err_msg_wrong_type2(const Obj *val, Type *expected, linepos_t epoint) {
    if (val->obj == ADDRESS_OBJ) {
        const Obj *val2 = ((Address *)val)->val;
        if (val2 == &none_value->v || val2->obj == ERROR_OBJ) val = val2;
    }
    if (val->obj == ERROR_OBJ) err_msg_output((Error *)val);
    else if (val->obj == NONE_OBJ) err_msg_still_none(NULL, epoint);
    else err_msg_wrong_type(val, expected, epoint);
}

void err_msg_cant_unpack(size_t expect, size_t got, linepos_t epoint) {
    char line[1024];
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    sprintf(line, "expected %" PRIuSIZE " values but got %" PRIuSIZE " to unpack", expect, got);
    adderror(line);
    if (more) new_error_msg_more();
}

void err_msg_cant_calculate(const str_t *name, linepos_t epoint) {
    err_msg_str_name("can't calculate stable value", name, epoint);
}

void err_msg_cant_calculate2(const str_t *name, const struct file_list_s *flist, linepos_t epoint) {
    bool more = new_error_msg(SV_ERROR, flist, epoint);
    adderror("can't calculate stable value");
    str_name(name->data, name->len);
    if (more) new_error_msg_more();
}

void err_msg_still_none(const str_t *name, linepos_t epoint) {
    if ((constcreated || !fixeddig) && pass < max_pass) return;
    new_error_msg(SV_NONEERROR, current_file_list, epoint);
    adderror("can't calculate this");
    if (name != NULL) str_name(name->data, name->len);
}

void err_msg_not_defined(const str_t *name, linepos_t epoint) {
    err_msg_str_name("not defined", name, epoint);
}

static void err_msg_double_note(const struct file_list_s *cflist, linepos_t epoint, const str_t *labelname2) {
    new_error_msg(SV_NOTE, cflist, epoint);
    adderror("original definition of");
    str_name(labelname2->data, labelname2->len);
    adderror(" was here");
}

void err_msg_star_assign(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.star_assign, epoint);
    adderror("label defined instead of variable multiplication for compatibility [-Wstar-assign]");
}

void err_msg_compound_note(linepos_t epoint) {
    static unsigned once;
    if (once != pass) {
        new_error_msg(SV_NOTE, current_file_list, epoint);
        adderror("for reserving space use '.fill x' or '.byte ?' [-Wpitfalls]");
        once = pass;
    }
}

void err_msg_byte_note(linepos_t epoint) {
    static unsigned int once;
    if (once != pass) {
        new_error_msg(SV_NOTE, current_file_list, epoint);
        adderror("for long strings mixed with bytes please use the '.text' directive [-Wpitfalls]");
        once = pass;
    }
}

void err_msg_char_note(const char *directive, linepos_t epoint) {
    new_error_msg(SV_NOTE, current_file_list, epoint);
    adderror("for signed values '");
    adderror(directive);
    adderror("' is a better fit [-Wpitfalls]");
}

void err_msg_immediate_note(linepos_t epoint) {
    new_error_msg(SV_NOTE, current_file_list, epoint);
    adderror("to accept signed values use the '#+' operator [-Wpitfalls]");
}

void err_msg_symbol_case(const str_t *labelname1, Label *l, linepos_t epoint) {
    new_error_msg2(diagnostic_errors.case_symbol, epoint);
    adderror("symbol case mismatch");
    str_name(labelname1->data, labelname1->len);
    adderror(" [-Wcase-symbol]");
    if (l != NULL) err_msg_double_note(l->file_list, &l->epoint, &l->name);
}

void err_msg_macro_prefix(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.macro_prefix, epoint);
    adderror("macro call without prefix [-Wmacro-prefix]");
}

static const char * const opr_names[ADR_LEN] = {
    "", /* ADR_REG */
    "", /* ADR_IMPLIED */
    "", /* ADR_IMMEDIATE */
    "long", /* ADR_LONG */
    "data bank", /* ADR_ADDR */
    "direct page", /* ADR_ZP */
    "long x indexed", /* ADR_LONG_X */
    "data bank x indexed", /* ADR_ADDR_X */
    "direct page x indexed", /* ADR_ZP_X */
    "", /* ADR_ADDR_X_I */
    "", /* ADR_ZP_X_I */
    "", /* ADR_ZP_S */
    "long y indexed", /* ADR_ZP_S_I_Y */
    "data bank y indexed", /* ADR_ADDR_Y */
    "direct page y indexed", /* ADR_ZP_Y */
    "", /* ADR_ZP_LI_Y */
    "", /* ADR_ZP_I_Y */
    "", /* ADR_ZP_I_Z */
    "", /* ADR_ADDR_LI */
    "", /* ADR_ZP_LI */
    "", /* ADR_ADDR_I */
    "", /* ADR_ZP_I */
    "", /* ADR_REL_L */
    "", /* ADR_REL */
    "", /* ADR_MOVE */
    "", /* ADR_ZP_R */
    "", /* ADR_ZP_R_I_Y */
    "", /* ADR_BIT_ZP */
    "", /* ADR_BIT_ZP_REL */
};

void err_msg_address_mismatch(int a, int b, linepos_t epoint) {
    new_error_msg2(diagnostic_errors.altmode, epoint);
    adderror("using ");
    adderror(opr_names[a]);
    adderror(" addressing instead of ");
    adderror(opr_names[b]);
    adderror(" [-Waltmode]");
}

static void err_msg_double_defined2(const char *msg, Severity_types severity, const struct file_list_s *cflist, const str_t *labelname2, linepos_t epoint2) {
    bool more = new_error_msg(severity, cflist, epoint2);
    adderror(msg);
    str_name(labelname2->data, labelname2->len);
    if (more) new_error_msg_more();
}

void err_msg_not_variable(Label *l, const str_t *labelname2, linepos_t epoint2) {
    err_msg_double_defined2("not a variable", SV_ERROR, current_file_list, labelname2, epoint2);
    err_msg_double_note(l->file_list, &l->epoint, labelname2);
}

void err_msg_double_defined(Label *l, const str_t *labelname2, linepos_t epoint2) {
    err_msg_double_defined2("duplicate definition", SV_ERROR, current_file_list, labelname2, epoint2);
    err_msg_double_note(l->file_list, &l->epoint, labelname2);
}

void err_msg_double_definedo(const struct file_list_s *cflist, linepos_t epoint, const str_t *labelname2, linepos_t epoint2) {
    err_msg_double_defined2("duplicate definition", SV_ERROR, current_file_list, labelname2, epoint2);
    err_msg_double_note(cflist, epoint, labelname2);
}

void err_msg_shadow_defined(Label *l, Label *l2) {
    err_msg_double_defined2("shadow definition", diagnostic_errors.shadow ? SV_ERROR : SV_WARNING, l2->file_list, &l2->name, &l2->epoint);
    adderror(" [-Wshadow]");
    err_msg_double_note(l->file_list, &l->epoint, &l2->name);
}

void err_msg_shadow_defined2(Label *l) {
    err_msg_double_defined2("shadow definition of built-in", diagnostic_errors.shadow ? SV_ERROR : SV_WARNING, l->file_list, &l->name, &l->epoint);
    adderror(" [-Wshadow]");
}

static const char * const order_suffix[4] = {
    "st", "nd", "rd", "th"
};

void err_msg_missing_argument(linepos_t epoint, size_t n) {
    char msg2[4];
    int i = n % 10;
    bool more = new_error_msg(SV_ERROR, current_file_list->parent, &current_file_list->epoint);
    msg2[0] = n + '1';
    memcpy(msg2 + 1, order_suffix[i < 4 ? i : 3], 3);
    adderror(msg2);
    adderror(" argument is missing");
    if (more) new_error_msg_more();
    new_error_msg(SV_NOTE, current_file_list, epoint);
    adderror("argument reference was here");
}

void err_msg_unknown_argument(const str_t *labelname, linepos_t epoint) {
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("unknown argument name");
    str_name(labelname->data, labelname->len);
    if (more) new_error_msg_more();
}

static void err_msg_unused(const char *msg, bool error, Label *l) {
    Severity_types severity = error ? SV_ERROR : SV_WARNING;
    bool more = new_error_msg(severity, l->file_list, &l->epoint);
    adderror(msg);
    str_name(l->name.data, l->name.len);
    if (more) new_error_msg_more();
}

void err_msg_unused_macro(Label *l) {
    err_msg_unused("unused macro", diagnostic_errors.unused.macro, l);
    adderror(" [-Wunused-macro]");
}

void err_msg_unused_label(Label *l) {
    err_msg_unused("unused label", diagnostic_errors.unused.label, l);
    adderror(" [-Wunused-label]");
}

void err_msg_unused_const(Label *l) {
    err_msg_unused("unused const", diagnostic_errors.unused.consts, l);
    adderror(" [-Wunused-const]");
}

void err_msg_unused_variable(Label *l) {
    err_msg_unused("unused variable", diagnostic_errors.unused.variable, l);
    adderror(" [-Wunused-variable]");
}

void err_msg_invalid_oper(const Oper *op, Obj *v1, Obj *v2, linepos_t epoint) {
    Error *err = new_error(ERROR__INVALID_OPER, epoint);
    err->u.invoper.op = op;
    err->u.invoper.v1 = (v1 != NULL) ? ((v1->refcount != 0) ? val_reference(v1) : v1) : NULL;
    err->u.invoper.v2 = (v2 != NULL) ? ((v2->refcount != 0) ? val_reference(v2) : v2) : NULL;
    err_msg_invalid_oper3(err);
    val_destroy(&err->v);
}

void err_msg_argnum(size_t num, size_t min, size_t max, linepos_t epoint) {
    size_t n;
    char line[1024];
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("expected ");
    n = min;
    if (min == max) adderror("exactly ");
    else if (num < min) adderror("at least ");
    else {n = max; adderror("at most "); }
    switch (n) {
    case 0: adderror("no arguments"); break;
    case 1: adderror("one argument"); break;
    default: sprintf(line, "%" PRIuSIZE " arguments", n); adderror(line); break;
    }
    if (num != 0) {
        sprintf(line, ", got %" PRIuSIZE, num);
        adderror(line);
    }
    if (more) new_error_msg_more();
}

void err_msg_bool(Error_types no, Obj *o, linepos_t epoint) {
    new_error_msg2(diagnostic_errors.strict_bool, epoint);
    adderror(terr_error[no - 0x40]);
    err_msg_variable(o);
    adderror(" [-Wstrict-bool]");
}

void err_msg_bool_val(Error_types no, unsigned int bits, Obj *o, linepos_t epoint) {
    new_error_msg2(diagnostic_errors.strict_bool, epoint);
    err_msg_big_integer(terr_error[no - 0x40], bits, o);
    adderror(" [-Wstrict-bool]");
}

void err_msg_bool_oper(oper_t op) {
    new_error_msg2(diagnostic_errors.strict_bool, op->epoint3);
    err_msg_invalid_oper2(op->op, op->v1, op->v2);
    adderror(" [-Wstrict-bool]");
}

void err_msg_implied_reg(linepos_t epoint, uint32_t cod) {
    Severity_types severity = diagnostic_errors.implied_reg ? SV_ERROR : SV_WARNING;
    bool more = new_error_msg(severity, current_file_list, epoint);
    err_msg_no_addressing(A_NONE, cod);
    adderror(" [-Wimplied-reg]");
    if (more) new_error_msg_more();
}

void err_msg_jmp_bug(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.jmp_bug, epoint);
    adderror( "possible jmp ($xxff) bug [-Wjmp-bug]");
}

void err_msg_pc_wrap(linepos_t epoint) {
    if (!diagnostics.wrap.pc) return;
    new_error_msg2(diagnostic_errors.wrap.pc, epoint);
    adderror("processor program counter overflow [-Wwrap-pc]");
}

void err_msg_mem_wrap(linepos_t epoint) {
    if (!diagnostics.wrap.mem) return;
    new_error_msg2(diagnostic_errors.wrap.mem, epoint);
    adderror("compile offset overflow [-Wwrap-mem]");
}

void err_msg_addr_wrap(linepos_t epoint) {
    if (!diagnostics.wrap.addr) return;
    new_error_msg2(diagnostic_errors.wrap.addr, epoint);
    adderror("memory location address overflow [-Wwrap-addr]");
}

void err_msg_dpage_wrap(linepos_t epoint) {
    if (!diagnostics.wrap.dpage) return;
    new_error_msg2(diagnostic_errors.wrap.dpage, epoint);
    adderror("direct page address overflow [-Wwrap-dpage]");
}

void err_msg_bank0_wrap(linepos_t epoint) {
    if (!diagnostics.wrap.bank0) return;
    new_error_msg2(diagnostic_errors.wrap.bank0, epoint);
    adderror("bank 0 address overflow [-Wwrap-bank0]");
}

void err_msg_pbank_wrap(linepos_t epoint) {
    if (!diagnostics.wrap.pbank) return;
    new_error_msg2(diagnostic_errors.wrap.pbank, epoint);
    adderror("program bank address overflow [-Wwrap-pbank]");
}

void err_msg_label_left(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.label_left, epoint);
    adderror("label not on left side [-Wlabel-left]");
}

void err_msg_branch_page(int by, linepos_t epoint) {
    char msg2[256];
    new_error_msg2(diagnostic_errors.branch_page, epoint);
    sprintf(msg2, "branch crosses page by %+d bytes [-Wbranch-page]", by);
    adderror(msg2);
}

void err_msg_page(address_t adr, address_t adr2, linepos_t epoint) {
    char line[256];
    new_error_msg2(diagnostic_errors.page, epoint);
    sprintf(line,"different start and end page $%04" PRIaddress " and $%04" PRIaddress " [-Wpage]", adr, adr2);
    adderror(line);
}

void err_msg_alias(uint32_t a, uint32_t b, linepos_t epoint) {
    char name[4];
    new_error_msg2(diagnostic_errors.alias, epoint);
    adderror("instruction '");
    name[0] = (char)(a >> 16);
    name[1] = (char)(a >> 8);
    name[2] = (char)a;
    name[3] = '\0';
    adderror(name);
    adderror("' is alias of '");
    name[0] = (char)(b >> 16);
    name[1] = (char)(b >> 8);
    name[2] = (char)b;
    adderror(name);
    adderror("' [-Walias]");
}

void err_msg_unknown_char(uchar_t ch, const str_t *name, linepos_t epoint) {
    uint8_t line[256], *s = line;
    bool more = new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("can't encode character '");
    if (ch != 0 && ch < 0x80) *s++ = (uint8_t)ch; else s = utf8out(ch, s);
    sprintf((char *)s, "' ($%02" PRIx32 ") in encoding '", ch); adderror((char *)line);
    adderror2(name->data, name->len);
    adderror("'");
    if (more) new_error_msg_more();
}

static const uint8_t *printline(const struct file_list_s *cfile, linepos_t epoint, const uint8_t *line, FILE *f) {
    const struct file_s *file;
    if (epoint->line == 0) return NULL;
    file = cfile->file;
    if (line == NULL) line = &file->data[file->line[epoint->line - 1]];
    fprintf(f, ":%" PRIuline ":%" PRIlinepos, epoint->line, ((file->encoding == E_UTF8) ? (linecpos_t)calcpos(line, epoint->pos) : epoint->pos) + 1);
    return line;
}

static void print_error(FILE *f, const struct errorentry_s *err, bool caret) {
    const struct file_list_s *cflist = err->file_list;
    linepos_t epoint = &err->epoint;
    const uint8_t *line = NULL;
    bool bold;

    if (cflist != &file_list) {
        if (cflist != included_from) {
            included_from = cflist;
            while (included_from->parent != &file_list) {
                if (included_from->file->entercount != 1) break;
                included_from = included_from->parent;
            }
            if (included_from->parent != &file_list) included_from = cflist;
            while (included_from->parent != &file_list) {
                fputs((included_from == cflist) ? "In file included from " : "                      ", f);
                if (print_use_color) fputs("\33[01m", f);
                printable_print((const uint8_t *)included_from->parent->file->realname, f);
                printline(included_from->parent, &included_from->epoint, NULL, f);
                included_from = included_from->parent;
                if (print_use_color) fputs("\33[m\33[K", f);
                fputs((included_from->parent != &file_list) ? ",\n" : ":\n", f);
            }
            included_from = cflist;
        }
        if (print_use_color) fputs("\33[01m", f);
        printable_print((const uint8_t *)cflist->file->realname, f);
        line = printline(cflist, epoint, (err->line_len != 0) ? (const uint8_t *)(err + 1) : NULL, f);
        fputs(": ", f);
    } else {
        if (print_use_color) fputs("\33[01m", f);
        printable_print((const uint8_t *)prgname, f);
        fputs(": ", f);
    }
    switch (err->severity) {
    case SV_NOTE: if (print_use_color) fputs("\33[30m", f); fputs("note: ", f); bold = false; break;
    case SV_WARNING: if (print_use_color) fputs("\33[35m", f); fputs("warning: ", f); bold = true; break;
    case SV_NONEERROR:
    case SV_ERROR: if (print_use_color) fputs("\33[31m", f); fputs("error: ", f); bold = true; break;
    case SV_FATAL: if (print_use_color) fputs("\33[31m", f); fputs("fatal error: ", f); bold = true; break;
    default: bold = false;
    }
    if (print_use_color) fputs(bold ? "\33[m\33[01m" : "\33[m\33[K", f);
#ifdef COLOR_OUTPUT
    print_use_bold = print_use_color && bold;
#endif
    printable_print2(((const uint8_t *)(err + 1)) + err->line_len, f, err->error_len);
    if (print_use_color && bold) fputs("\33[m\33[K", f);
#ifdef COLOR_OUTPUT
    print_use_bold = false;
#endif
    putc('\n', f);
    if (arguments.caret != CARET_NEVER && caret && line != NULL) {
        putc(' ', f);
        printable_print(line, f);
        fputs("\n ", f);
        caret_print(line, f, err->caret);
        fputs(print_use_color ? "\33[01;32m^\33[m\33[K\n" : "^\n", f);
    }
}

#ifdef COLOR_OUTPUT
static void color_detect(FILE *f) {
    char const *term = getenv ("TERM");
    print_use_color = term != NULL && strcmp(term, "dumb") != 0 && isatty(fileno(f)) == 1;
}
#else
#define color_detect(f) {}
#endif

static bool different_line(const struct errorentry_s *err, const struct errorentry_s *err2) {
    if (err->file_list->file != err2->file_list->file || err->line_len != err2->line_len ||
            err->epoint.line != err2->epoint.line || err->epoint.pos != err2->epoint.pos) return true;
    if (err->line_len == 0) return false;
    return memcmp(err + 1, err2 + 1, err->line_len) != 0;
}

static void walkfilelist(struct file_list_s *cflist) {
    struct avltree_node *n;

    for (n = avltree_first(&cflist->members); n != NULL; n = avltree_next(n)) {
        struct file_list_s *l = avltree_container_of(n, struct file_list_s, node);
        if (l->file->entercount > 1 || l->pass != pass) continue;
        l->file->entercount++;
        walkfilelist(l);
    }
}

bool error_print(void) {
    const struct errorentry_s *err, *err2, *err3;
    size_t pos;
    bool noneerr = false, anyerr = false, usenote;
    FILE *ferr;
    struct linepos_s nopoint = {0, 0};

    if (error_list.header_pos != 0) {
        struct avltree_node *n;
        for (n = avltree_first(&file_list.members); n != NULL; n = avltree_next(n)) {
            struct file_list_s *l = avltree_container_of(n, struct file_list_s, node);
            if (l->file->entercount > 1 || l->pass != pass) continue;
            walkfilelist(l);
        }
    }

    if (arguments.error != NULL) {
        ferr = dash_name(arguments.error) ? stdout : file_open(arguments.error, "wt");
        if (ferr == NULL) {
            err_msg_file(ERROR_CANT_WRTE_ERR, arguments.error, &nopoint);
            ferr = stderr;
        }
    } else ferr = stderr;

    color_detect(ferr);

    warnings = errors = 0;
    close_error();

    for (pos = 0; pos < error_list.header_pos; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        switch (err->severity) {
        case SV_NONEERROR: anyerr = true; break;
        case SV_NOTE:
        case SV_WARNING:
            break;
        default: noneerr = true; anyerr = true; break;
        }
    }

    err2 = err3 = NULL;
    usenote = false;
    for (pos = 0; pos < error_list.header_pos; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        switch (err->severity) {
        case SV_NOTE:
            if (!usenote) continue;
            if (err3 != NULL) {
                if (err->severity != err3->severity || err->file_list != err3->file_list ||
                        err->line_len != err3->line_len || err->error_len != err3->error_len ||
                        err->epoint.line != err3->epoint.line || err->epoint.pos != err3->epoint.pos ||
                        memcmp(err + 1, err3 + 1, err->line_len + err->error_len) != 0) {
                    print_error(ferr, err3, (arguments.caret == CARET_ALWAYS || err3->line_len != 0) && different_line(err, err3));
                }
            }
            err3 = err2;
            err2 = err;
            continue;
        case SV_WARNING:
            if (!arguments.warning) {
                usenote = false;
                continue;
            }
            warnings++;
            if (anyerr) {
                usenote = false;
                continue;
            }
            break;
        case SV_NONEERROR:
            if (noneerr) {
                usenote = false;
                continue;
            }
            /* fall through */
        default:
            errors++;
            break;
        }
        if (err3 != NULL) print_error(ferr, err3, (arguments.caret == CARET_ALWAYS || err3->line_len != 0) && different_line(err2, err3));
        err3 = err2;
        err2 = err;
        usenote = true;
    }
    if (err3 != NULL) print_error(ferr, err3, (arguments.caret == CARET_ALWAYS || err3->line_len != 0) && different_line(err2, err3));
    if (err2 != NULL) print_error(ferr, err2, (arguments.caret == CARET_ALWAYS || err2->line_len != 0));
    color_detect(stderr);
    if (ferr != stderr && ferr != stdout) fclose(ferr); else fflush(ferr);
    return errors != 0;
}

void error_reset(void) {
    error_list.len = error_list.header_pos = 0;
    avltree_init(&error_list.members);
    current_file_list = &file_list;
    included_from = &file_list;
}

void err_init(const char *name) {
    prgname = name;
    setvbuf(stderr, NULL, _IOLBF, 1024);
    color_detect(stderr);
    file_lists = (struct file_lists_s *)mallocx(sizeof *file_lists);
    file_lists->next = NULL;
    file_listsp = 0;
    lastfl = &file_lists->file_lists[file_listsp];
    file_list_file.name = "";
    file_list_file.realname = file_list_file.name;
    file_list.file = &file_list_file;
    avltree_init(&file_list.members);
    error_list.len = error_list.max = error_list.header_pos = 0;
    error_list.data = NULL;
    avltree_init(&error_list.members);
    current_file_list = &file_list;
    included_from = &file_list;
    avltree_init(&notdefines);
}

void err_destroy(void) {
    while (file_lists != NULL) {
        struct file_lists_s *old = file_lists;
        file_lists = file_lists->next;
        free(old);
    }
    free(error_list.data);
    avltree_destroy(&notdefines, notdefines_free);
    free(lastnd);
}

void fatal_error(const char *txt) {
    if (txt != NULL) {
        if (print_use_color) fputs("\33[01m", stderr);
        printable_print((const uint8_t *)((prgname != NULL) ? prgname : "64tass"), stderr);
        fputs(print_use_color ? ": \33[31m" : ": ", stderr);
        fputs("fatal error: ", stderr);
        if (print_use_color) fputs("\33[m\33[01m", stderr);
        fputs(txt, stderr);
        return;
    }
    if (print_use_color) fputs("\33[m\33[K", stderr);
    putc('\n', stderr);
}

NO_RETURN void err_msg_out_of_memory2(void)
{
    fatal_error("out of memory");
    fatal_error(NULL);
    exit(EXIT_FAILURE);
}

NO_RETURN void err_msg_out_of_memory(void)
{
    error_print();
    err_msg_out_of_memory2();
}

void err_msg_file(Error_types no, const char *prm, linepos_t epoint) {
    mbstate_t ps;
    const char *s;
    wchar_t w;
    uint8_t s2[10];
    size_t n, i = 0;
    ssize_t l;
    int err = errno;
    bool more;

#ifdef _WIN32
    setlocale(LC_ALL, "");
#endif
    s = strerror(err);
    n = strlen(s);

    more = new_error_msg(SV_FATAL, current_file_list, epoint);
    adderror(terr_fatal[no - 0xc0]);
    adderror(" '");
    adderror(prm);
    adderror("': ");
    memset(&ps, 0, sizeof ps);
    while (i < n) {
        if (s[i] != 0 && (s[i] & 0x80) == 0) {
            adderror2((const uint8_t *)s + i, 1);
            i++;
            continue;
        }
        l = (ssize_t)mbrtowc(&w, s + i, n - i,  &ps);
        if (l < 1) {
            w = (uint8_t)s[i];
            if (w == 0 || l == 0) break;
            l = 1;
        }
        s2[utf8out((uchar_t)w, s2) - s2] = 0;
        adderror((char *)s2);
        i += (size_t)l;
    }
#ifdef _WIN32
    setlocale(LC_ALL, "C");
#endif
    if (more) new_error_msg_more();
}

void error_status(void) {
    printf("Error messages:    ");
    if (errors != 0) printf("%u\n", errors); else puts("None");
    printf("Warning messages:  ");
    if (warnings != 0) printf("%u\n", warnings); else puts("None");
}

linecpos_t interstring_position(linepos_t epoint, const uint8_t *data, size_t i) {
    if (epoint->line == lpoint.line && strlen((const char *)pline) > epoint->pos) {
        uint8_t q = pline[epoint->pos];
        if (q == '"' || q == '\'') {
            linecpos_t pos = epoint->pos + 1;
            size_t pp = 0;
            while (pp < i && pline[pos] != 0) {
                unsigned int ln2;
                if (pline[pos] == q) {
                    if (pline[pos + 1] != q) break;
                    pos++;
                }
                ln2 = utf8len(pline[pos]);
                if (memcmp(pline + pos, data + pp, ln2) != 0) break;
                pos += ln2; pp += ln2;
            }
            if (pp == i) {
                return pos;
            }
        }
    }
    return epoint->pos;
}

bool error_serious(void) {
    const struct errorentry_s *err;
    size_t pos;
    close_error();
    for (pos = 0; pos < error_list.header_pos; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        switch (err->severity) {
        case SV_NOTE:
        case SV_WARNING: break;
        default: return true;
        }
    }
    return false;
}
