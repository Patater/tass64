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
#ifndef IDENTOBJ_H
#define IDENTOBJ_H
#include "obj.h"
#include "str.h"

extern struct Type *const IDENT_OBJ;

struct file_list_s;

typedef struct Ident {
    Obj v;
    str_t name;
    str_t cfname;
    int hash;
    const struct file_list_s *file_list;
} Ident;

extern void identobj_init(void);

extern Ident *new_ident(const str_t *name);

static inline Ident *ref_ident(Ident *v1) {
    v1->v.refcount++; return v1;
}
#endif
