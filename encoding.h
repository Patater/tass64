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
#ifndef ENCODING_H
#define ENCODING_H
#include "avl.h"
#include "stdbool.h"
#include "inttypes.h"
#include "errors_e.h"

struct encoding_s;
struct Obj;

struct trans_s {
    uint32_t start;
    uint32_t end : 24;
    uint32_t offset : 8;
    struct avltree_node node;
};

extern struct encoding_s *actual_encoding;

struct str_t;
struct Str;
struct encoder_s;

extern struct encoding_s *new_encoding(const struct str_t *, linepos_t);
extern struct trans_s *new_trans(struct encoding_s *, const struct trans_s *, linepos_t);
extern bool new_escape(struct encoding_s *, const struct str_t *, struct Obj *, linepos_t);
extern struct encoder_s *encode_string_init(const struct Str *, linepos_t);
extern int encode_string(struct encoder_s *);
extern void encode_error(struct encoder_s *, Error_types);
extern void init_encoding(bool);
extern void destroy_encoding(void);
#endif
