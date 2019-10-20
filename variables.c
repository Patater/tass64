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
#include "variables.h"
#include <string.h>
#include <errno.h>
#include "unicode.h"
#include "64tass.h"
#include "file.h"
#include "obj.h"
#include "error.h"
#include "values.h"
#include "arguments.h"
#include "eval.h"

#include "boolobj.h"
#include "floatobj.h"
#include "namespaceobj.h"
#include "strobj.h"
#include "codeobj.h"
#include "registerobj.h"
#include "functionobj.h"
#include "listobj.h"
#include "intobj.h"
#include "bytesobj.h"
#include "bitsobj.h"
#include "dictobj.h"
#include "addressobj.h"
#include "gapobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "labelobj.h"
#include "errorobj.h"
#include "mfuncobj.h"

static struct namespacekey_s *lastlb2 = NULL;
static Label *lastlb = NULL;

#define EQUAL_COLUMN 16

Namespace *root_namespace;
static Namespace *builtin_namespace;
Namespace *current_context;
Namespace *cheap_context;

struct cstack_s {
    Namespace *normal;
    Namespace *cheap;
};

struct context_stack_s {
    struct cstack_s *stack;
    size_t len, p, bottom;
};

static struct context_stack_s context_stack;

void push_context(Namespace *name) {
    if (context_stack.p >= context_stack.len) {
        context_stack.len += 8;
        if (/*context_stack.len < 8 ||*/ context_stack.len > SIZE_MAX / sizeof *context_stack.stack) err_msg_out_of_memory(); /* overflow */
        context_stack.stack = (struct cstack_s *)reallocx(context_stack.stack, context_stack.len * sizeof *context_stack.stack);
    }
    context_stack.stack[context_stack.p].normal = ref_namespace(name);
    current_context = name;
    context_stack.stack[context_stack.p].cheap = cheap_context;
    cheap_context = ref_namespace(name);
    context_stack.p++;
}

void push_dummy_context(void) {
    push_context(builtin_namespace);
}

bool pop_context(void) {
    if (context_stack.p > 1 + context_stack.bottom) {
        struct cstack_s *c = &context_stack.stack[--context_stack.p];
        val_destroy(&c->normal->v);
        val_destroy(&cheap_context->v);
        cheap_context = c->cheap;
        c = &context_stack.stack[context_stack.p - 1];
        current_context = c->normal;
        return false;
    }
    return true;
}

void reset_context(void) {
    context_stack.bottom = 0;
    while (context_stack.p != 0) {
        struct cstack_s *c = &context_stack.stack[--context_stack.p];
        val_destroy(&c->normal->v);
        val_destroy(&c->cheap->v);
    }
    val_destroy(&cheap_context->v);
    cheap_context = ref_namespace(root_namespace);
    push_context(root_namespace);
    root_namespace->backr = root_namespace->forwr = 0;
}

struct label_stack_s {
    Label **stack;
    size_t len, p;
};

static struct label_stack_s label_stack;

static void push_label(Label *name) {
    if (label_stack.p >= label_stack.len) {
        label_stack.len += 8;
        if (/*label_stack.len < 8 ||*/ label_stack.len > SIZE_MAX / sizeof(Label *)) err_msg_out_of_memory(); /* overflow */
        label_stack.stack = (Label **)reallocx(label_stack.stack, label_stack.len * sizeof(Label *));
    }
    label_stack.stack[label_stack.p] = name;
    label_stack.p++;
}

static void pop_label(void) {
    label_stack.p--;
}

void get_namespaces(Mfunc *mfunc) {
    size_t i, len = context_stack.p - context_stack.bottom;
    if (len > SIZE_MAX / sizeof *mfunc->namespaces) err_msg_out_of_memory(); /* overflow */
    mfunc->nslen = len;
    mfunc->namespaces = (Namespace **)mallocx(len * sizeof *mfunc->namespaces);
    for (i = 0; i < len; i++) {
        mfunc->namespaces[i] = ref_namespace(context_stack.stack[context_stack.bottom + i].normal);
    }
}

void context_set_bottom(size_t n) {
    context_stack.bottom = n;
}

size_t context_get_bottom(void) {
    size_t old = context_stack.bottom;
    context_stack.bottom = context_stack.p;
    return old;
}

/* --------------------------------------------------------------------------- */

static FAST_CALL NO_INLINE int str_compare(const str_t *s1, const str_t *s2) {
    return memcmp(s1->data, s2->data, s1->len);
}

static FAST_CALL NO_INLINE int label_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct namespacekey_s *a = cavltree_container_of(aa, struct namespacekey_s, node);
    const struct namespacekey_s *b = cavltree_container_of(bb, struct namespacekey_s, node);
    const str_t *s1, *s2;
    int h = a->hash - b->hash;
    if (h != 0) return h;
    s1 = &a->key->cfname;
    s2 = &b->key->cfname;
    if (s1->len != s2->len) return s1->len > s2->len ? 1 : -1;
    if (s1->data == s2->data) return 0;
    return str_compare(s1, s2);
}

static FAST_CALL int label_compare2(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct namespacekey_s *a = cavltree_container_of(aa, struct namespacekey_s, node);
    const struct namespacekey_s *b = cavltree_container_of(bb, struct namespacekey_s, node);
    int h = a->hash - b->hash;
    if (h != 0) return h;
    h = str_cmp(&a->key->cfname, &b->key->cfname);
    if (h != 0) return h;
    return b->key->strength - a->key->strength;
}

static struct namespacekey_s *strongest_label(struct avltree_node *b) {
    struct namespacekey_s *a = NULL, *c;
    struct avltree_node *n = b;

    do {
        c = avltree_container_of(n, struct namespacekey_s, node);
        if (c->key->defpass == pass || (c->key->constant && (!fixeddig || c->key->defpass == pass - 1))) {
            if (c->key->strength == 0) return c;
            a = c;
        }
        n = avltree_next(n);
    } while (n != NULL && label_compare(n, b) == 0);
    if (a != NULL) return a;
    n = avltree_prev(b);
    while (n != NULL && label_compare(n, b) == 0) {
        c = avltree_container_of(n, struct namespacekey_s, node);
        if (c->key->defpass == pass || (c->key->constant && (!fixeddig || c->key->defpass == pass - 1))) {
            return c;
        }
        n = avltree_prev(n);
    }
    return NULL;
}

Label *find_label(const str_t *name, Namespace **here) {
    struct avltree_node *b;
    struct namespacekey_s tmp, *c;
    size_t p = context_stack.p;
    Label label;

    str_cfcpy(&label.cfname, name);
    tmp.hash = str_hash(&label.cfname);
    tmp.key = &label;

    while (context_stack.bottom < p) {
        Namespace *context = context_stack.stack[--p].normal;
        b = avltree_lookup(&tmp.node, &context->members, label_compare);
        if (b != NULL) {
            c = strongest_label(b);
            if (c != NULL) {
                Label *key2 = c->key;
                if (!diagnostics.shadow || !fixeddig || constcreated || (here != NULL && *here == context)) {
                    if (here != NULL) *here = context;
                    return key2;
                }
                if (here != NULL) *here = context;
                while (context_stack.bottom < p) {
                    b = avltree_lookup(&tmp.node, &context_stack.stack[--p].normal->members, label_compare);
                    if (b != NULL) {
                        const struct namespacekey_s *l2 = strongest_label(b);
                        if (l2 != NULL) {
                            Label *key1 = l2->key;
                            Obj *o1 = key1->value;
                            Obj *o2 = key2->value;
                            if (o1 != o2 && !o1->obj->same(o1, o2)) {
                                err_msg_shadow_defined(key1, key2);
                                return key2;
                            }
                        }
                    }
                }
                b = avltree_lookup(&tmp.node, &builtin_namespace->members, label_compare);
                if (b != NULL) {
                    const struct namespacekey_s *l2 = cavltree_container_of(b, struct namespacekey_s, node);
                    Label *key1 = l2->key;
                    Obj *o1 = key1->value;
                    Obj *o2 = key2->value;
                    if (o1 != o2 && !o1->obj->same(o1, o2)) {
                        err_msg_shadow_defined2(key2);
                    }
                }
                return key2;
            }
        }
    }
    b = avltree_lookup(&tmp.node, &builtin_namespace->members, label_compare);
    if (b != NULL) {
        if (here != NULL) *here = builtin_namespace;
        return avltree_container_of(b, struct namespacekey_s, node)->key;
    }
    if (here != NULL) *here = NULL;
    return NULL;
}
Label *find_label2(const str_t *name, Namespace *context) {
    struct avltree_node *b;
    struct namespacekey_s tmp, *c;
    Label label;

    str_cfcpy(&label.cfname, name);
    tmp.hash = str_hash(&label.cfname);
    tmp.key = &label;

    b = avltree_lookup(&tmp.node, &context->members, label_compare);
    if (b == NULL) return NULL;
    c = strongest_label(b);
    return (c != NULL) ? c->key : NULL;
}

static struct {
    uint8_t dir;
    uint8_t padding[3];
    int32_t count;
} anon_idents;

Label *find_label3(const str_t *name, Namespace *context, uint8_t strength) {
    struct avltree_node *b;
    struct namespacekey_s tmp, *c;
    Label label;

    label.strength = strength;
    if (name->len == sizeof anon_idents && name->data[1] == 0) label.cfname = *name;
    else str_cfcpy(&label.cfname, name);
    tmp.hash = str_hash(&label.cfname);
    tmp.key = &label;

    b = avltree_lookup(&tmp.node, &context->members, label_compare2);
    if (b == NULL) return NULL;
    c = avltree_container_of(b, struct namespacekey_s, node);
    return (c != NULL) ? c->key : NULL;
}

Label *find_anonlabel(int32_t count) {
    struct avltree_node *b;
    struct namespacekey_s tmp, *c;
    size_t p = context_stack.p;
    Namespace *context;
    Label label;

    anon_idents.dir = (count >= 0) ? '+' : '-';

    label.cfname.data = (const uint8_t *)&anon_idents;
    label.cfname.len = sizeof anon_idents;
    tmp.key = &label;

    while (context_stack.bottom < p) {
        context = context_stack.stack[--p].normal;
        anon_idents.count = (int32_t)((count >= 0) ? context->forwr : context->backr) + count;
        tmp.hash = str_hash(&label.cfname);
        b = avltree_lookup(&tmp.node, &context->members, label_compare);
        if (b != NULL) {
            c = strongest_label(b);
            if (c != NULL) return c->key;
        }
    }
    return NULL;
}

Label *find_anonlabel2(int32_t count, Namespace *context) {
    struct avltree_node *b;
    struct namespacekey_s tmp, *c;
    Label label;

    anon_idents.dir = (count >= 0) ? '+' : '-';
    anon_idents.count = (int32_t)((count >= 0) ? 0 : context->backr) + count;

    label.cfname.data = (const uint8_t *)&anon_idents;
    label.cfname.len = sizeof anon_idents;
    tmp.hash = str_hash(&label.cfname);
    tmp.key = &label;

    b = avltree_lookup(&tmp.node, &context->members, label_compare);
    if (b == NULL) return NULL;
    c = strongest_label(b);
    return (c != NULL) ? c->key : NULL;
}

/* --------------------------------------------------------------------------- */
Label *new_label(const str_t *name, Namespace *context, uint8_t strength, bool *exists, const struct file_list_s *cflist) {
    struct avltree_node *b;
    Label *tmp;
    if (lastlb2 == NULL) {
        lastlb2 = namespacekey_alloc();
    }
    if (lastlb == NULL) lastlb = (Label *)val_alloc(LABEL_OBJ);

    if (name->len > 1 && name->data[1] == 0) lastlb->cfname = *name;
    else str_cfcpy(&lastlb->cfname, name);
    lastlb2->hash = str_hash(&lastlb->cfname);
    lastlb2->key = lastlb;
    lastlb->strength = strength;

    b = avltree_insert(&lastlb2->node, &context->members, label_compare2);

    if (b == NULL) { /* new label */
        if ((size_t)(name->data - cflist->file->data) < cflist->file->len) lastlb->name = *name;
        else str_cpy(&lastlb->name, name);
        if (lastlb->cfname.data != name->data) str_cfcpy(&lastlb->cfname, NULL);
        else lastlb->cfname = lastlb->name;
        lastlb->file_list = cflist;
        lastlb->ref = false;
        lastlb->update_after = false;
        lastlb->usepass = 0;
        lastlb->defpass = pass;
        *exists = false;
        tmp = lastlb;
        lastlb = NULL;
        lastlb2 = NULL;
        context->len++;
        return tmp;
    }
    *exists = true;
    return avltree_container_of(b, struct namespacekey_s, node)->key;            /* already exists */
}

void label_move(Label *label, const str_t *name, const struct file_list_s *cflist) {
    bool cfsame = (label->cfname.data == label->name.data);
    if ((size_t)(label->name.data - label->file_list->file->data) < label->file_list->file->len) {
        if ((size_t)(name->data - cflist->file->data) < cflist->file->len) label->name = *name;
        else str_cpy(&label->name, name);
    }
    if (cfsame) {
        label->cfname = label->name;
    }
    label->file_list = cflist;
}

void unused_check(Namespace *members) {
    const struct avltree_node *n;

    for (n = avltree_first(&members->members); n != NULL; n = avltree_next(n)) {
        const struct namespacekey_s *l = cavltree_container_of(n, struct namespacekey_s, node);
        Label *key2 = l->key;
        Obj *o;
        Namespace *ns;

        if (key2->defpass != pass) continue;

        o  = key2->value;
        switch (o->obj->type) {
        case T_CODE:
            ns = ((Code *)o)->names;
            break;
        case T_NAMESPACE:
            ns = (Namespace *)o;
            break;
        case T_MFUNC:
            ns = ((Mfunc *)o)->names;
            break;
        default:
            ns = NULL;
            break;
        }
        if (key2->usepass != pass && (key2->name.data[0] != '.' && key2->name.data[0] != '#')) {
            if (!key2->constant) {
                if (diagnostics.unused.variable) err_msg_unused_variable(key2);
                continue;
            }
            if (!key2->owner) {
                if (diagnostics.unused.consts) err_msg_unused_const(key2);
                continue;
            }
            if (o->obj == CODE_OBJ) {
                if (diagnostics.unused.label) err_msg_unused_label(key2);
                continue;
            }
            if (diagnostics.unused.macro) {
                err_msg_unused_macro(key2);
                continue;
            }
        }
        if (ns != NULL && ns->len != 0 && key2->owner) {
            size_t ln = ns->len;
            ns->len = 0;
            push_context(ns);
            unused_check(ns);
            pop_context();
            ns->len = ln;
        }
    }
}

static Label *find_strongest_label(struct avltree_node **x, avltree_cmp_fn_t cmp) {
    struct namespacekey_s *a = NULL, *c;
    struct avltree_node *b = *x, *n = b;
    do {
        c = avltree_container_of(n, struct namespacekey_s, node);
        if (c->key->defpass == pass) a = c;
        n = avltree_next(n);
    } while (n != NULL && cmp(n, b) == 0);
    *x = n;
    if (a != NULL) return a->key;
    n = avltree_prev(b);
    while (n != NULL && cmp(n, b) == 0) {
        c = avltree_container_of(n, struct namespacekey_s, node);
        if (c->key->defpass == pass) return c->key;
        n = avltree_prev(n);
    }
    return NULL;
}

static inline void padding(size_t l, size_t t, FILE *f) {
    if (arguments.tab_size > 1) {
        size_t l2 = l - l % arguments.tab_size;
        while (l2 + arguments.tab_size <= t) { l2 += arguments.tab_size; l = l2; putc('\t', f);}
    }
    while (l < t) { l++; putc(' ', f);}
}

static void labelname_print(Label *l, FILE *flab, char d) {
    size_t p;
    for (p = 0; p < label_stack.p; p++) {
        printable_print2(label_stack.stack[p]->name.data, flab, label_stack.stack[p]->name.len);
        putc(d, flab);
    }
    printable_print2(l->name.data, flab, l->name.len);
}

static void labelprint2(const struct avltree *members, FILE *flab, int labelmode) {
    struct avltree_node *n;
    Label *l;

    n = avltree_first(members);
    while (n != NULL) {
        l = find_strongest_label(&n, label_compare);            /* already exists */
        if (l == NULL || l->name.data == NULL) continue;
        if (l->name.len > 1 && l->name.data[1] == 0) continue;
        switch (l->value->obj->type) {
        case T_LBL:
        case T_MACRO:
        case T_SEGMENT:
        case T_UNION:
        case T_STRUCT: continue;
        default:break;
        }
        if (labelmode == LABEL_VICE) {
            Obj *val;
            size_t i, j = l->name.len;
            const uint8_t *d = l->name.data;

            for (i = 0; i < j; i++) {
                uint8_t c = d[i];
                if (c < '0') break;
                if (c <= '9') continue;
                if (c == '_') continue;
                c |= 0x20;
                if (c < 'a') break;
                if (c <= 'z') continue;
                break;
            }
            if (i != j) continue;

            val = l->value;
            if (val->obj == ADDRESS_OBJ || val->obj == CODE_OBJ) {
                struct linepos_s epoint;
                uval_t uv;
                Error *err = val->obj->uval(val, &uv, 24, &epoint);
                if (err == NULL) {
                    fprintf(flab, "al %" PRIx32 " .", uv & 0xffffff);
                    labelname_print(l, flab, ':');
                    putc('\n', flab);
                } else val_destroy(&err->v);
            }
            if (l->owner) {
                Namespace *ns = get_namespace(val);
                if (ns != NULL && ns->len != 0) {
                    size_t ln = ns->len;
                    ns->len = 0;
                    push_label(l);
                    labelprint2(&ns->members, flab, labelmode);
                    pop_label();
                    ns->len = ln;
                }
            }
        } else {
            Obj *val = l->value;
            Str *str = (Str *)val->obj->repr(val, NULL, SIZE_MAX);
            if (str == NULL) continue;
            if (str->v.obj == STR_OBJ) {
                size_t len = printable_print2(l->name.data, flab, l->name.len);
                padding(len, EQUAL_COLUMN, flab);
                if (l->constant) fputs("= ", flab);
                else fputs(&" := "[len < EQUAL_COLUMN], flab);
                printable_print2(str->data, flab, str->len);
                putc('\n', flab);
            }
            val_destroy(&str->v);
        }
    }
}

static inline const uint8_t *get_line(const struct file_s *file, size_t line) {
    return &file->data[file->line[line - 1]];
}

static void labeldump(Namespace *members, FILE *flab) {
    const struct avltree_node *n;

    for (n = avltree_first(&members->members); n != NULL; n = avltree_next(n)) {
        const struct namespacekey_s *l = cavltree_container_of(n, struct namespacekey_s, node);
        Label *l2 = l->key;
        Namespace *ns;

        if (l2->name.len < 2 || l2->name.data[1] != 0) {
            Str *val = (Str *)l2->value->obj->repr(l2->value, NULL, SIZE_MAX);
            if (val != NULL) {
                if (val->v.obj == STR_OBJ) {
                    const struct file_s *file = l2->file_list->file;
                    linepos_t epoint = &l2->epoint;
                    printable_print((const uint8_t *)file->realname, flab);
                    fprintf(flab, ":%" PRIuline ":%" PRIlinepos ": ", epoint->line, ((file->encoding == E_UTF8) ? (linecpos_t)calcpos(get_line(file, epoint->line), epoint->pos) : epoint->pos) + 1);
                    labelname_print(l2, flab, '.');
                    fputs(l2->constant ? " = " : " := ", flab);
                    printable_print2(val->data, flab, val->len);
                    putc('\n', flab);
                }
                val_destroy(&val->v);
            }
        }
        if (!l2->owner) continue;

        ns = get_namespace(l2->value);

        if (ns != NULL && ns->len != 0) {
            if (l2->name.len < 2 || l2->name.data[1] != 0) {
                size_t ln = ns->len;
                ns->len = 0;
                push_label(l2);
                labeldump(ns, flab);
                pop_label();
                ns->len = ln;
            }
        }
    }
}

static Namespace *find_space(const char *here, bool use) {
    Namespace *space;
    str_t labelname;
    Label *l;

    space = root_namespace;
    if (here == NULL) return space;

    pline = (const uint8_t *)here;
    lpoint.pos = 0;
    do {
        labelname.data = pline + lpoint.pos; labelname.len = get_label(labelname.data);
        if (labelname.len == 0) return NULL;
        lpoint.pos += labelname.len;
        l = find_label2(&labelname, space);
        if (l == NULL) return NULL;
        space = get_namespace(l->value);
        if (space == NULL) return NULL;
        lpoint.pos++;
    } while (labelname.data[labelname.len] == '.');

    if (labelname.data[labelname.len] != 0) return NULL;

    if (use) {
        l->usepass = pass;
        l->ref = true;
    }
    return space;
}

bool labelprint(const struct symbol_output_s *output, bool append) {
    FILE *flab;
    struct linepos_s nopoint = {0, 0};
    int err;
    Namespace *space;

    flab = dash_name(output->name) ? stdout : file_open(output->name, append ? "at" : "wt");
    if (flab == NULL) {
        err_msg_file(ERROR_CANT_WRTE_LBL, output->name, &nopoint);
        return true;
    }
    clearerr(flab); errno = 0;
    label_stack.stack = NULL;
    label_stack.p = label_stack.len = 0;
    space = find_space(output->space, false);
    if (space == NULL) {
        str_t labelname;
        labelname.data = pline;
        labelname.len = lpoint.pos;
        err_msg2(ERROR____LABEL_ROOT, &labelname, &nopoint);
    } else if (output->mode == LABEL_DUMP) {
        labeldump(space, flab);
    } else {
        labelprint2(&space->members, flab, output->mode);
    }
    free(label_stack.stack);
    err = ferror(flab);
    err |= (flab != stdout) ? fclose(flab) : fflush(flab);
    if (err != 0 && errno != 0) {
        err_msg_file(ERROR_CANT_WRTE_LBL, output->name, &nopoint);
        return true;
    }
    return false;
}

void ref_labels(void) {
    size_t j;
    for (j = 0; j < arguments.symbol_output_len; j++) {
        const struct symbol_output_s *output = &arguments.symbol_output[j];
        Namespace *space;
        struct avltree_node *n;

        if (output->mode != LABEL_64TASS) continue;
        space = find_space(output->space, true);
        if (space == NULL) continue;

        n = avltree_first(&space->members);
        while (n != NULL) {
            Label *l = find_strongest_label(&n, label_compare);            /* already exists */
            if (l == NULL || l->name.data == NULL) continue;
            if (l->name.len > 1 && l->name.data[1] == 0) continue;
            switch (l->value->obj->type) {
            case T_LBL:
            case T_MACRO:
            case T_SEGMENT:
            case T_UNION:
            case T_STRUCT: continue;
            default:break;
            }
            l->ref = true;
            l->usepass = pass;
        }
    }
}

static struct file_list_s dummy_cflist;

void new_builtin(const char *ident, Obj *val) {
    struct linepos_s nopoint = {0, 0};
    str_t name;
    Label *label;
    bool label_exists;
    name.len = strlen(ident);
    name.data = (const uint8_t *)ident;
    label = new_label(&name, builtin_namespace, 0, &label_exists, &dummy_cflist);
    label->constant = true;
    label->owner = true;
    label->value = val;
    label->epoint = nopoint;
}

void init_variables(void)
{
    struct linepos_s nopoint = {0, 0};
    static struct file_s cfile;

    cfile.data = (uint8_t *)0;
    cfile.len = SIZE_MAX;
    dummy_cflist.file = &cfile;

    builtin_namespace = new_namespace(NULL, &nopoint);
    root_namespace = new_namespace(NULL, &nopoint);
    cheap_context = ref_namespace(root_namespace);

    context_stack.stack = NULL;
    context_stack.p = context_stack.len = context_stack.bottom = 0;

    boolobj_names();
    registerobj_names();
    functionobj_names();
    gapobj_names();
    listobj_names();
    strobj_names();
    intobj_names();
    floatobj_names();
    codeobj_names();
    addressobj_names();
    dictobj_names();
    bitsobj_names();
    bytesobj_names();
    typeobj_names();
    namespaceobj_names();
}

void destroy_lastlb(void) {
    if (lastlb != NULL) {
        lastlb->v.obj = NONE_OBJ;
        val_destroy(&lastlb->v);
        lastlb = NULL;
    }
}

void destroy_variables(void)
{
    val_destroy(&builtin_namespace->v);
    val_destroy(&root_namespace->v);
    val_destroy(&cheap_context->v);
    destroy_lastlb();
    if (lastlb2 != NULL) namespacekey_free(lastlb2);
    while (context_stack.p != 0) {
        struct cstack_s *c = &context_stack.stack[--context_stack.p];
        val_destroy(&c->normal->v);
        val_destroy(&c->cheap->v);
    }
    free(context_stack.stack);
}
