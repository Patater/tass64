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
#ifndef ANONIDENTOBJ_H
#define ANONIDENTOBJ_H
#include "obj.h"

extern struct Type *const ANONIDENT_OBJ;

struct file_list_s;

typedef struct Anonident {
    Obj v;
    int32_t count;
} Anonident;

extern void anonidentobj_init(void);

extern Anonident *new_anonident(int32_t);

static inline Anonident *ref_anonident(Anonident *v1) {
    v1->v.refcount++; return v1;
}
#endif
