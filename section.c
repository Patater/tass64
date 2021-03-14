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
#include "section.h"
#include "unicode.h"
#include "error.h"
#include "64tass.h"
#include "values.h"
#include "intobj.h"
#include "longjump.h"
#include "optimizer.h"
#include "eval.h"
#include "mem.h"

#include "memblocksobj.h"

struct section_s root_section;
struct section_s *current_section = &root_section;
struct section_address_s *current_address = &root_section.address;
static struct section_s *prev_section = &root_section;

static FAST_CALL int section_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct section_s *a = cavltree_container_of(aa, struct section_s, node);
    const struct section_s *b = cavltree_container_of(bb, struct section_s, node);
    int h = a->name_hash - b->name_hash;
    if (h != 0) return h;
    return str_cmp(&a->cfname, &b->cfname);
}

static void section_free(struct avltree_node *aa)
{
    struct section_s *a = avltree_container_of(aa, struct section_s, node);
    free((uint8_t *)a->name.data);
    if (a->name.data != a->cfname.data) free((uint8_t *)a->cfname.data);
    avltree_destroy(&a->members, section_free);
    longjump_destroy(&a->longjump);
    val_destroy(Obj(a->address.mem));
    val_destroy(a->address.l_address_val);
    cpu_opt_destroy(a->optimizer);
    free(a);
}

static struct section_s *find_section(const str_t *name, struct section_s *context) {
    struct avltree_node *b;
    struct section_s tmp, *tmp2 = NULL;

    if (name->len > 1 && name->data[1] == 0) tmp.cfname = *name;
    else str_cfcpy(&tmp.cfname, name);
    tmp.name_hash = str_hash(&tmp.cfname);

    while (context != NULL) {
        b = avltree_lookup(&tmp.node, &context->members, section_compare);
        if (b != NULL) {
            tmp2 = avltree_container_of(b, struct section_s, node);
            if (tmp2->defpass >= pass - 1) {
                return tmp2;
            }
        }
        context = context->parent;
    }
    return tmp2;
}

struct section_s *find_new_section(const str_t *name) {
    struct section_s *tmp2 = find_section(name, current_section);
    return (tmp2 != NULL) ? tmp2 : new_section(name);
}

static struct section_s *lastsc = NULL;
struct section_s *new_section(const str_t *name) {
    struct avltree_node *b;
    struct section_s *tmp;

    if (lastsc == NULL) {
        lastsc = (struct section_s *)mallocx(sizeof *lastsc);
    }
    str_cfcpy(&lastsc->cfname, name);
    lastsc->name_hash = str_hash(&lastsc->cfname);
    b = avltree_insert(&lastsc->node, &current_section->members, section_compare);
    if (b == NULL) { /* new section */
        str_cpy(&lastsc->name, name);
        if (lastsc->cfname.data == name->data) lastsc->cfname = lastsc->name;
        else str_cfcpy(&lastsc->cfname, NULL);
        lastsc->parent = current_section;
        lastsc->provides = ~(uval_t)0;lastsc->requires = lastsc->conflicts = 0;
        lastsc->address.end = lastsc->address.address = lastsc->address.l_address = lastsc->address.l_start = lastsc->address.l_union = lastsc->size = 0;
        lastsc->address.l_address_val = val_reference(int_value[0]);
        lastsc->defpass = 0;
        lastsc->usepass = 0;
        lastsc->address.unionmode = false;
        lastsc->logicalrecursion = 0;
        lastsc->address.moved = false;
        lastsc->address.wrapwarn = false;
        lastsc->address.bankwarn = false;
        lastsc->next = NULL;
        lastsc->optimizer = NULL;
        prev_section->next = lastsc;
        prev_section = lastsc;
        lastsc->address.mem = new_memblocks(0, 0);
        avltree_init(&lastsc->members);
        avltree_init(&lastsc->longjump);
        tmp = lastsc;
        lastsc = NULL;
        return tmp;
    }
    return avltree_container_of(b, struct section_s, node);            /* already exists */
}

struct section_s *find_this_section(const char *here) {
    struct section_s *space;
    str_t labelname;

    space = &root_section;
    if (here == NULL) return space;

    pline = (const uint8_t *)here;
    lpoint.pos = 0;
    do {
        labelname.data = pline + lpoint.pos; labelname.len = get_label(labelname.data);
        if (labelname.len == 0) return NULL;
        lpoint.pos += labelname.len;
        space = find_section(&labelname, space);
        if (space == NULL) return NULL;
        lpoint.pos++;
    } while (labelname.data[labelname.len] == '.');

    return labelname.data[labelname.len] != 0 ? NULL : space;
}

void reset_section(struct section_s *section) {
    section->provides = ~(uval_t)0; section->requires = section->conflicts = 0;
    section->address.end = section->address.start = section->restart = section->l_restart = section->address.address = section->address.l_address = section->address.l_start = section->address.l_union = 0;
    val_destroy(section->address.l_address_val);
    section->address.l_address_val = val_reference(int_value[0]);
    section->logicalrecursion = 0;
    section->address.moved = false;
    section->address.wrapwarn = false;
    section->address.bankwarn = false;
    section->address.unionmode = false;
}

void init_section(void) {
    root_section.parent = NULL;
    root_section.name.data = NULL;
    root_section.name.len = 0;
    root_section.cfname.data = NULL;
    root_section.cfname.len = 0;
    root_section.next = NULL;
    root_section.optimizer = NULL;
    root_section.address.mem = new_memblocks(0, 0);
    root_section.address.l_address_val = val_reference(int_value[0]);
    avltree_init(&root_section.members);
    avltree_init(&root_section.longjump);
    prev_section = &root_section;
}

void destroy_section(void) {
    free(lastsc);
    avltree_destroy(&root_section.members, section_free);
    longjump_destroy(&root_section.longjump);
    val_destroy(Obj(root_section.address.mem));
    val_destroy(root_section.address.l_address_val);
    root_section.address.l_address_val = NULL;
    cpu_opt_destroy(root_section.optimizer);
    root_section.optimizer = NULL;
}

static void sectionprint2(const struct section_s *l, FILE *f) {
    if (l->name.data != NULL) {
        sectionprint2(l->parent, f);
        printable_print2(l->name.data, f, l->name.len);
        putc('.', f);
    }
}

static void printrange(const struct section_s *l, FILE *f) {
    char temp[10], temp2[10], temp3[10];
    sprintf(temp, "$%04" PRIaddress, l->address.start);
    temp2[0] = 0;
    if (l->size != 0) {
        sprintf(temp2, "-$%04" PRIaddress, l->address.start + l->size - 1U);
    }
    sprintf(temp3, "$%04" PRIaddress, l->size);
    fprintf(f, "Section: %15s%-8s %-7s", temp, temp2, temp3);
}

void sectionprint(FILE *f) {
    struct section_s *l = &root_section;

    if (l->size != 0) {
        printrange(l, f);
        putc('\n', f);
    }
    memprint(l->address.mem, f);
    l = root_section.next;
    while (l != NULL) {
        if (l->defpass == pass) {
            printrange(l, f);
            putc(' ', f);
            sectionprint2(l->parent, f);
            printable_print2(l->name.data, f, l->name.len);
            putc('\n', f);
            memprint(l->address.mem, f);
        }
        l = l->next;
    }
}

void section_sizecheck(void) {
    struct section_s *l = root_section.next;
    while (l != NULL) {
        if (l->defpass == pass) {
            if (l->size != ((!l->address.moved && l->address.end < l->address.address) ? l->address.address : l->address.end) - l->address.start) {
                if (pass > max_pass) err_msg_cant_calculate2(&l->name, l->file_list, &l->epoint);
                fixeddig = false;
                return;
            }
        }
        l = l->next;
    }
}
