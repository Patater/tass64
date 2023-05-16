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
#include "encoding.h"
#include "error.h"
#include "string.h"
#include "ternary.h"
#include "values.h"
#include "64tass.h"
#include "str.h"

#include "encobj.h"

const char *identmap;

struct translate_table_s {
    uint16_t start;
    uint8_t length;
    uint8_t offset;
};

static const struct translate_table_s no_trans[] = {
    {0x0000, 255, 0x00},
};

static const struct translate_table_s petscii_trans[] = {
    {0x0020,  32, 0x20}, /*  -@ */
    {0x0041,  25, 0xc1}, /* A-Z */
    {0x005b,   0, 0x5b}, /* [ */
    {0x005d,   0, 0x5d}, /* ] */
    {0x0061,  25, 0x41}, /* a-z */
    {0x00a3,   0, 0x5c}, /* £ */
    {0x03c0,   0, 0xff}, /* π */
    {0x2190,   0, 0x5f}, /* ← */
    {0x2191,   0, 0x5e}, /* ↑ */
    {0x2500,   0, 0xc0}, /* ─ */
    {0x2502,   0, 0xdd}, /* │ */
    {0x250c,   0, 0xb0}, /* ┌ */
    {0x2510,   0, 0xae}, /* ┐ */
    {0x2514,   0, 0xad}, /* └ */
    {0x2518,   0, 0xbd}, /* ┘ */
    {0x251c,   0, 0xab}, /* ├ */
    {0x2524,   0, 0xb3}, /* ┤ */
    {0x252c,   0, 0xb2}, /* ┬ */
    {0x2534,   0, 0xb1}, /* ┴ */
    {0x253c,   0, 0xdb}, /* ┼ */
    {0x256d,   0, 0xd5}, /* ╭ */
    {0x256e,   0, 0xc9}, /* ╮ */
    {0x256f,   0, 0xcb}, /* ╯ */
    {0x2570,   0, 0xca}, /* ╰ */
    {0x2571,   0, 0xce}, /* ╱ */
    {0x2572,   0, 0xcd}, /* ╲ */
    {0x2573,   0, 0xd6}, /* ╳ */
    {0x2581,   0, 0xa4}, /* ▁ */
    {0x2582,   0, 0xaf}, /* ▂ */
    {0x2583,   0, 0xb9}, /* ▃ */
    {0x2584,   0, 0xa2}, /* ▄ */
    {0x258c,   0, 0xa1}, /* ▌ */
    {0x258d,   0, 0xb5}, /* ▍ */
    {0x258e,   0, 0xb4}, /* ▎ */
    {0x258f,   0, 0xa5}, /* ▏ */
    {0x2592,   0, 0xa6}, /* ▒ */
    {0x2594,   0, 0xa3}, /* ▔ */
    {0x2595,   0, 0xa7}, /* ▕ */
    {0x2596,   0, 0xbb}, /* ▖ */
    {0x2597,   0, 0xac}, /* ▗ */
    {0x2598,   0, 0xbe}, /* ▘ */
    {0x259a,   0, 0xbf}, /* ▚ */
    {0x259d,   0, 0xbc}, /* ▝ */
    {0x25cb,   0, 0xd7}, /* ○ */
    {0x25cf,   0, 0xd1}, /* ● */
    {0x25e4,   0, 0xa9}, /* ◤ */
    {0x25e5,   0, 0xdf}, /* ◥ */
    {0x2660,   0, 0xc1}, /* ♠ */
    {0x2663,   0, 0xd8}, /* ♣ */
    {0x2665,   0, 0xd3}, /* ♥ */
    {0x2666,   0, 0xda}, /* ♦ */
    {0x2713,   0, 0xba}, /* ✓ */
    {0xfb70,   0, 0xd4}, /* 🭰 */
    {0xfb71,   0, 0xc7}, /* 🭱 */
    {0xfb72,   0, 0xc2}, /* 🭲 */
    {0xfb73,   0, 0xdd}, /* 🭳 */
    {0xfb74,   0, 0xc8}, /* 🭴 */
    {0xfb75,   0, 0xd9}, /* 🭵 */
    {0xfb76,   0, 0xc5}, /* 🭶 */
    {0xfb77,   0, 0xc4}, /* 🭷 */
    {0xfb78,   0, 0xc3}, /* 🭸 */
    {0xfb79,   0, 0xc0}, /* 🭹 */
    {0xfb7a,   0, 0xc6}, /* 🭺 */
    {0xfb7b,   0, 0xd2}, /* 🭻 */
    {0xfb7c,   0, 0xcc}, /* 🭼 */
    {0xfb7d,   0, 0xcf}, /* 🭽 */
    {0xfb7e,   0, 0xd0}, /* 🭾 */
    {0xfb7f,   0, 0xba}, /* 🭿 */
    {0xfb82,   0, 0xb7}, /* 🮂 */
    {0xfb83,   0, 0xb8}, /* 🮃 */
    {0xfb87,   0, 0xaa}, /* 🮇 */
    {0xfb88,   0, 0xb6}, /* 🮈 */
    {0xfb8c,   0, 0xdc}, /* 🮌 */
    {0xfb8f,   0, 0xa8}, /* 🮏 */
    {0xfb95,   0, 0xff}, /* 🮕 */
    {0xfb98,   0, 0xdf}, /* 🮘 */
    {0xfb99,   0, 0xa9}, /* 🮙 */
};

/* PETSCII codes, must be sorted */
static const char *const petscii_esc =
    "\x07" "{bell}"
    "\x90" "{black}"
    "\x90" "{blk}"
    "\x1f" "{blue}"
    "\x1f" "{blu}"
    "\x95" "{brn}"
    "\x95" "{brown}"
    "\xdf" "{cbm-*}"
    "\xa6" "{cbm-+}"
    "\xdc" "{cbm--}"
    "\x30" "{cbm-0}"
    "\x81" "{cbm-1}"
    "\x95" "{cbm-2}"
    "\x96" "{cbm-3}"
    "\x97" "{cbm-4}"
    "\x98" "{cbm-5}"
    "\x99" "{cbm-6}"
    "\x9a" "{cbm-7}"
    "\x9b" "{cbm-8}"
    "\x29" "{cbm-9}"
    "\xa4" "{cbm-@}"
    "\xde" "{cbm-^}"
    "\xb0" "{cbm-a}"
    "\xbf" "{cbm-b}"
    "\xbc" "{cbm-c}"
    "\xac" "{cbm-d}"
    "\xb1" "{cbm-e}"
    "\xbb" "{cbm-f}"
    "\xa5" "{cbm-g}"
    "\xb4" "{cbm-h}"
    "\xa2" "{cbm-i}"
    "\xb5" "{cbm-j}"
    "\xa1" "{cbm-k}"
    "\xb6" "{cbm-l}"
    "\xa7" "{cbm-m}"
    "\xaa" "{cbm-n}"
    "\xb9" "{cbm-o}"
    "\xa8" "{cbm-pound}"
    "\xaf" "{cbm-p}"
    "\xab" "{cbm-q}"
    "\xb2" "{cbm-r}"
    "\xae" "{cbm-s}"
    "\xa3" "{cbm-t}"
    "\xde" "{cbm-up arrow}"
    "\xb8" "{cbm-u}"
    "\xbe" "{cbm-v}"
    "\xb3" "{cbm-w}"
    "\xbd" "{cbm-x}"
    "\xb7" "{cbm-y}"
    "\xad" "{cbm-z}"
    "\x93" "{clear}"
    "\x93" "{clr}"
    "\x92" "{control-0}"
    "\x90" "{control-1}"
    "\x05" "{control-2}"
    "\x1c" "{control-3}"
    "\x9f" "{control-4}"
    "\x9c" "{control-5}"
    "\x1e" "{control-6}"
    "\x1f" "{control-7}"
    "\x9e" "{control-8}"
    "\x12" "{control-9}"
    "\x1b" "{control-:}"
    "\x1d" "{control-;}"
    "\x1f" "{control-=}"
    "\x00" "{control-@}"
    "\x01" "{control-a}"
    "\x02" "{control-b}"
    "\x03" "{control-c}"
    "\x04" "{control-d}"
    "\x05" "{control-e}"
    "\x06" "{control-f}"
    "\x07" "{control-g}"
    "\x08" "{control-h}"
    "\x09" "{control-i}"
    "\x0a" "{control-j}"
    "\x0b" "{control-k}"
    "\x06" "{control-left arrow}"
    "\x0c" "{control-l}"
    "\x0d" "{control-m}"
    "\x0e" "{control-n}"
    "\x0f" "{control-o}"
    "\x1c" "{control-pound}"
    "\x10" "{control-p}"
    "\x11" "{control-q}"
    "\x12" "{control-r}"
    "\x13" "{control-s}"
    "\x14" "{control-t}"
    "\x1e" "{control-up arrow}"
    "\x15" "{control-u}"
    "\x16" "{control-v}"
    "\x17" "{control-w}"
    "\x18" "{control-x}"
    "\x19" "{control-y}"
    "\x1a" "{control-z}"
    "\x0d" "{cr}"
    "\x9f" "{cyan}"
    "\x9f" "{cyn}"
    "\x14" "{delete}"
    "\x14" "{del}"
    "\x08" "{dish}"
    "\x11" "{down}"
    "\x09" "{ensh}"
    "\x1b" "{esc}"
    "\x82" "{f10}"
    "\x84" "{f11}"
    "\x8f" "{f12}"
    "\x85" "{f1}"
    "\x89" "{f2}"
    "\x86" "{f3}"
    "\x8a" "{f4}"
    "\x87" "{f5}"
    "\x8b" "{f6}"
    "\x88" "{f7}"
    "\x8c" "{f8}"
    "\x80" "{f9}"
    "\x97" "{gray1}"
    "\x98" "{gray2}"
    "\x9b" "{gray3}"
    "\x1e" "{green}"
    "\x97" "{grey1}"
    "\x98" "{grey2}"
    "\x9b" "{grey3}"
    "\x1e" "{grn}"
    "\x97" "{gry1}"
    "\x98" "{gry2}"
    "\x9b" "{gry3}"
    "\x84" "{help}"
    "\x13" "{home}"
    "\x94" "{insert}"
    "\x94" "{inst}"
    "\x9a" "{lblu}"
    "\x5f" "{left arrow}"
    "\x9d" "{left}"
    "\x0a" "{lf}"
    "\x99" "{lgrn}"
    "\x0e" "{lower case}"
    "\x96" "{lred}"
    "\x9a" "{lt blue}"
    "\x99" "{lt green}"
    "\x96" "{lt red}"
    "\x81" "{orange}"
    "\x81" "{orng}"
    "\xff" "{pi}"
    "\x5c" "{pound}"
    "\x9c" "{purple}"
    "\x9c" "{pur}"
    "\x1c" "{red}"
    "\x0d" "{return}"
    "\x92" "{reverse off}"
    "\x12" "{reverse on}"
    "\x1d" "{rght}"
    "\x1d" "{right}"
    "\x83" "{run}"
    "\x92" "{rvof}"
    "\x12" "{rvon}"
    "\x92" "{rvs off}"
    "\x12" "{rvs on}"
    "\x8d" "{shift return}"
    "\xc0" "{shift-*}"
    "\xdb" "{shift-+}"
    "\x3c" "{shift-,}"
    "\xdd" "{shift--}"
    "\x3e" "{shift-.}"
    "\x3f" "{shift-/}"
    "\x30" "{shift-0}"
    "\x21" "{shift-1}"
    "\x22" "{shift-2}"
    "\x23" "{shift-3}"
    "\x24" "{shift-4}"
    "\x25" "{shift-5}"
    "\x26" "{shift-6}"
    "\x27" "{shift-7}"
    "\x28" "{shift-8}"
    "\x29" "{shift-9}"
    "\x5b" "{shift-:}"
    "\x5d" "{shift-;}"
    "\xba" "{shift-@}"
    "\xde" "{shift-^}"
    "\xc1" "{shift-a}"
    "\xc2" "{shift-b}"
    "\xc3" "{shift-c}"
    "\xc4" "{shift-d}"
    "\xc5" "{shift-e}"
    "\xc6" "{shift-f}"
    "\xc7" "{shift-g}"
    "\xc8" "{shift-h}"
    "\xc9" "{shift-i}"
    "\xca" "{shift-j}"
    "\xcb" "{shift-k}"
    "\xcc" "{shift-l}"
    "\xcd" "{shift-m}"
    "\xce" "{shift-n}"
    "\xcf" "{shift-o}"
    "\xa9" "{shift-pound}"
    "\xd0" "{shift-p}"
    "\xd1" "{shift-q}"
    "\xd2" "{shift-r}"
    "\xa0" "{shift-space}"
    "\xd3" "{shift-s}"
    "\xd4" "{shift-t}"
    "\xde" "{shift-up arrow}"
    "\xd5" "{shift-u}"
    "\xd6" "{shift-v}"
    "\xd7" "{shift-w}"
    "\xd8" "{shift-x}"
    "\xd9" "{shift-y}"
    "\xda" "{shift-z}"
    "\x20" "{space}"
    "\x8d" "{sret}"
    "\x03" "{stop}"
    "\x0e" "{swlc}"
    "\x8e" "{swuc}"
    "\x09" "{tab}"
    "\x5e" "{up arrow}"
    "\x09" "{up/lo lock off}"
    "\x08" "{up/lo lock on}"
    "\x8e" "{upper case}"
    "\x91" "{up}"
    "\x05" "{white}"
    "\x05" "{wht}"
    "\x9e" "{yellow}"
    "\x9e" "{yel}"
    "\x00";

static const struct translate_table_s petscii_screen_trans[] = {
    {0x0020,  31, 0x20}, /*  -? */
    {0x0040,   0, 0x00}, /* @ */
    {0x0041,  25, 0x41}, /* A-Z */
    {0x005b,   0, 0x1b}, /* [ */
    {0x005d,   0, 0x1d}, /* ] */
    {0x0061,  25, 0x01}, /* a-z */
    {0x00a3,   0, 0x1c}, /* £ */
    {0x03c0,   0, 0x5e}, /* π */
    {0x2190,   0, 0x1f}, /* ← */
    {0x2191,   0, 0x1e}, /* ↑ */
    {0x2500,   0, 0x40}, /* ─ */
    {0x2502,   0, 0x5d}, /* │ */
    {0x250c,   0, 0x70}, /* ┌ */
    {0x2510,   0, 0x6e}, /* ┐ */
    {0x2514,   0, 0x6d}, /* └ */
    {0x2518,   0, 0x7d}, /* ┘ */
    {0x251c,   0, 0x6b}, /* ├ */
    {0x2524,   0, 0x73}, /* ┤ */
    {0x252c,   0, 0x72}, /* ┬ */
    {0x2534,   0, 0x71}, /* ┴ */
    {0x253c,   0, 0x5b}, /* ┼ */
    {0x256d,   0, 0x55}, /* ╭ */
    {0x256e,   0, 0x49}, /* ╮ */
    {0x256f,   0, 0x4b}, /* ╯ */
    {0x2570,   0, 0x4a}, /* ╰ */
    {0x2571,   0, 0x4e}, /* ╱ */
    {0x2572,   0, 0x4d}, /* ╲ */
    {0x2573,   0, 0x56}, /* ╳ */
    {0x2581,   0, 0x64}, /* ▁ */
    {0x2582,   0, 0x6f}, /* ▂ */
    {0x2583,   0, 0x79}, /* ▃ */
    {0x2584,   0, 0x62}, /* ▄ */
    {0x258c,   0, 0x61}, /* ▌ */
    {0x258d,   0, 0x75}, /* ▍ */
    {0x258e,   0, 0x74}, /* ▎ */
    {0x258f,   0, 0x65}, /* ▏ */
    {0x2592,   0, 0x66}, /* ▒ */
    {0x2594,   0, 0x63}, /* ▔ */
    {0x2595,   0, 0x67}, /* ▕ */
    {0x2596,   0, 0x7b}, /* ▖ */
    {0x2597,   0, 0x6c}, /* ▗ */
    {0x2598,   0, 0x7e}, /* ▘ */
    {0x259a,   0, 0x7f}, /* ▚ */
    {0x259d,   0, 0x7c}, /* ▝ */
    {0x25cb,   0, 0x57}, /* ○ */
    {0x25cf,   0, 0x51}, /* ● */
    {0x25e4,   0, 0x69}, /* ◤ */
    {0x25e5,   0, 0x5f}, /* ◥ */
    {0x2660,   0, 0x41}, /* ♠ */
    {0x2663,   0, 0x58}, /* ♣ */
    {0x2665,   0, 0x53}, /* ♥ */
    {0x2666,   0, 0x5a}, /* ♦ */
    {0x2713,   0, 0x7a}, /* ✓ */
    {0xfb70,   0, 0x54}, /* 🭰 */
    {0xfb71,   0, 0x47}, /* 🭱 */
    {0xfb72,   0, 0x42}, /* 🭲 */
    {0xfb73,   0, 0x5d}, /* 🭳 */
    {0xfb74,   0, 0x48}, /* 🭴 */
    {0xfb75,   0, 0x59}, /* 🭵 */
    {0xfb76,   0, 0x45}, /* 🭶 */
    {0xfb77,   0, 0x44}, /* 🭷 */
    {0xfb78,   0, 0x43}, /* 🭸 */
    {0xfb79,   0, 0x40}, /* 🭹 */
    {0xfb7a,   0, 0x46}, /* 🭺 */
    {0xfb7b,   0, 0x52}, /* 🭻 */
    {0xfb7c,   0, 0x4c}, /* 🭼 */
    {0xfb7d,   0, 0x4f}, /* 🭽 */
    {0xfb7e,   0, 0x50}, /* 🭾 */
    {0xfb7f,   0, 0x7a}, /* 🭿 */
    {0xfb82,   0, 0x77}, /* 🮂 */
    {0xfb83,   0, 0x78}, /* 🮃 */
    {0xfb87,   0, 0x6a}, /* 🮇 */
    {0xfb88,   0, 0x76}, /* 🮈 */
    {0xfb8c,   0, 0x5c}, /* 🮌 */
    {0xfb8f,   0, 0x68}, /* 🮏 */
    {0xfb95,   0, 0x5e}, /* 🮕 */
    {0xfb98,   0, 0x5f}, /* 🮘 */
    {0xfb99,   0, 0x69}, /* 🮙 */
};

/* petscii screen codes, must be sorted */
static const char petscii_screen_esc[] =
    "\x5f" "{cbm-*}"
    "\x66" "{cbm-+}"
    "\x5c" "{cbm--}"
    "\x30" "{cbm-0}"
    "\x29" "{cbm-9}"
    "\x64" "{cbm-@}"
    "\x5e" "{cbm-^}"
    "\x70" "{cbm-a}"
    "\x7f" "{cbm-b}"
    "\x7c" "{cbm-c}"
    "\x6c" "{cbm-d}"
    "\x71" "{cbm-e}"
    "\x7b" "{cbm-f}"
    "\x65" "{cbm-g}"
    "\x74" "{cbm-h}"
    "\x62" "{cbm-i}"
    "\x75" "{cbm-j}"
    "\x61" "{cbm-k}"
    "\x76" "{cbm-l}"
    "\x67" "{cbm-m}"
    "\x6a" "{cbm-n}"
    "\x79" "{cbm-o}"
    "\x68" "{cbm-pound}"
    "\x6f" "{cbm-p}"
    "\x6b" "{cbm-q}"
    "\x72" "{cbm-r}"
    "\x6e" "{cbm-s}"
    "\x63" "{cbm-t}"
    "\x5e" "{cbm-up arrow}"
    "\x78" "{cbm-u}"
    "\x7e" "{cbm-v}"
    "\x73" "{cbm-w}"
    "\x7d" "{cbm-x}"
    "\x77" "{cbm-y}"
    "\x6d" "{cbm-z}"
    "\x1f" "{left arrow}"
    "\x5e" "{pi}"
    "\x1c" "{pound}"
    "\x40" "{shift-*}"
    "\x5b" "{shift-+}"
    "\x3c" "{shift-,}"
    "\x5d" "{shift--}"
    "\x3e" "{shift-.}"
    "\x3f" "{shift-/}"
    "\x30" "{shift-0}"
    "\x21" "{shift-1}"
    "\x22" "{shift-2}"
    "\x23" "{shift-3}"
    "\x24" "{shift-4}"
    "\x25" "{shift-5}"
    "\x26" "{shift-6}"
    "\x27" "{shift-7}"
    "\x28" "{shift-8}"
    "\x29" "{shift-9}"
    "\x1b" "{shift-:}"
    "\x1d" "{shift-;}"
    "\x7a" "{shift-@}"
    "\x5e" "{shift-^}"
    "\x41" "{shift-a}"
    "\x42" "{shift-b}"
    "\x43" "{shift-c}"
    "\x44" "{shift-d}"
    "\x45" "{shift-e}"
    "\x46" "{shift-f}"
    "\x47" "{shift-g}"
    "\x48" "{shift-h}"
    "\x49" "{shift-i}"
    "\x4a" "{shift-j}"
    "\x4b" "{shift-k}"
    "\x4c" "{shift-l}"
    "\x4d" "{shift-m}"
    "\x4e" "{shift-n}"
    "\x4f" "{shift-o}"
    "\x69" "{shift-pound}"
    "\x50" "{shift-p}"
    "\x51" "{shift-q}"
    "\x52" "{shift-r}"
    "\x60" "{shift-space}"
    "\x53" "{shift-s}"
    "\x54" "{shift-t}"
    "\x5e" "{shift-up arrow}"
    "\x55" "{shift-u}"
    "\x56" "{shift-v}"
    "\x57" "{shift-w}"
    "\x58" "{shift-x}"
    "\x59" "{shift-y}"
    "\x5a" "{shift-z}"
    "\x20" "{space}"
    "\x1e" "{up arrow}"
    "\x00";

static const struct translate_table_s no_screen_trans[] = {
    {0x0000,  31, 0x80},
    {0x0020,  31, 0x20},
    {0x0040,  31, 0x00},
    {0x0060,  31, 0x40},
    {0x0080,  31, 0x80},
    {0x00A0,  31, 0x60},
    {0x00C0,  62, 0x40},
    {0x00FF,   0, 0x5E},
};

struct encoding_s {
    str_t name;
    str_t cfname;
    Enc *enc;
    uint8_t pass;
    struct avltree_node node;
};

static struct avltree encoding_tree;

static FAST_CALL int encoding_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const str_t *a = &cavltree_container_of(aa, struct encoding_s, node)->cfname;
    const str_t *b = &cavltree_container_of(bb, struct encoding_s, node)->cfname;

    return str_cmp(a, b);
}

static void encoding_free(struct avltree_node *aa)
{
    struct encoding_s *a = avltree_container_of(aa, struct encoding_s, node);

    free((char *)a->name.data);
    if (a->name.data != a->cfname.data) free((uint8_t *)a->cfname.data);
    val_destroy(Obj(a->enc));
    free(a);
}

static Enc *enc_from_name(str_t *, linepos_t);

static struct encoding_s *lasten;
Enc *new_encoding(const str_t *name, linepos_t epoint)
{
    struct avltree_node *b;
    struct encoding_s *tmp;

    if (lasten == NULL) new_instance(&lasten);
    str_cfcpy(&lasten->cfname, name);
    b = avltree_insert(&lasten->node, &encoding_tree, encoding_compare);
    if (b == NULL) { /* new encoding */
        str_cpy(&lasten->name, name);
        if (lasten->cfname.data == name->data) lasten->cfname = lasten->name;
        else str_cfcpy(&lasten->cfname, NULL);
        tmp = lasten;
        lasten = NULL;
        tmp->enc = enc_from_name(&tmp->cfname, epoint);
        tmp->pass = pass;
    } else {
        tmp = avltree_container_of(b, struct encoding_s, node);
        if (tmp->pass != pass) {
            tmp->enc->file_list = current_file_list;
            tmp->enc->epoint = *epoint;
        }
    }
    return tmp->enc;
}

static void add_trans(Enc *enc, const struct translate_table_s *table, size_t ln) {
    size_t i;
    struct linepos_s nopoint = {0, 0};
    for (i = 0; i < ln; i++) {
        struct character_range_s range;
        uint32_t start = table[i].start;
        if (start >= 0x8000) start += 0x10000;
        range.start = start;
        range.end = (start + table[i].length) & 0xffffff;
        range.offset = table[i].offset;
        enc_trans_add(enc, &range, &nopoint);
    }
}

static void add_esc(Enc *enc, const char *s) {
    enc->escape_char = '{';
    while (s[1] != '\0') {
        const char **b;
        unsigned int len = 1;
        while (s[len] != '}') len++;
        b = (const char **)ternary_insert(&enc->escapes, (const uint8_t*)s + 1, (const uint8_t*)s + 1 + len);
        if (b == NULL) err_msg_out_of_memory();
        *b = identmap + (uint8_t)s[0];
        if (enc->escape_length > len) enc->escape_length = len;
        s += len + 1;
    }
}

static bool ascii_mode;

static Enc *enc_from_name(str_t *name, linepos_t epoint) {
    Enc *enc = Enc(new_enc(current_file_list, epoint));
    bool oldfixeddig = fixeddig;
    uint8_t oldpass = pass;
    fixeddig = false;
    pass = 255;
    if (name->len == 4 && memcmp(name->data, "none", 4) == 0) {
        if (!ascii_mode) {
            add_trans(enc, no_trans, lenof(no_trans));
        } else {
            add_esc(enc, petscii_esc);
            add_trans(enc, petscii_trans, lenof(petscii_trans));
        }
    } else if (name->len == 6 && memcmp(name->data, "screen", 6) == 0) {
        if (!ascii_mode) {
            add_trans(enc, no_screen_trans, lenof(no_screen_trans));
        } else {
            add_esc(enc, petscii_screen_esc);
            add_trans(enc, petscii_screen_trans, lenof(petscii_screen_trans));
        }
    }
    pass = oldpass;
    fixeddig = oldfixeddig;
    return enc;
}

void init_encoding(bool toascii)
{
    identmap = petscii_esc;
    avltree_init(&encoding_tree);
    ascii_mode = toascii;
    lasten = NULL;
}

void destroy_encoding(void)
{
    avltree_destroy(&encoding_tree, encoding_free);
    free(lasten);
}
