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
#ifndef _INSTRUCTION_H_
#define _INSTRUCTION_H_
#include "inttypes.h"
struct cpu_s;

struct Error;
struct Obj;

extern MUST_CHECK struct Error *instruction(int, int, struct Obj *, linepos_t, struct linepos_s *);
extern void select_opcodes(const struct cpu_s *);
extern int lookup_opcode(const char *);
extern MUST_CHECK int touval(struct Obj *, uval_t *, int, linepos_t);
extern MUST_CHECK int toaddress(struct Obj *, uval_t *, int, uint32_t *, linepos_t);
extern MUST_CHECK struct Error *err_addressing(uint32_t, linepos_t);

extern int longaccu, longindex, autosize;
extern unsigned int dpage;
extern unsigned int databank;
extern int longbranchasjmp;
extern int allowslowbranch;
#endif
