/* dl_helper.c - relocation helper functions for modules and grub-mkimage */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2022 Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/dl.h>
#include <grub/elf.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/mm.h>
#include <grub/i18n.h>
#include <grub/loongarch64/reloc.h>

static void grub_loongarch64_stack_push (grub_loongarch64_stack_t stack, grub_uint64_t x);
static grub_uint64_t grub_loongarch64_stack_pop (grub_loongarch64_stack_t stack);

void
grub_loongarch64_stack_init (grub_loongarch64_stack_t stack)
{
  stack->top = -1;
  stack->count = LOONGARCH64_STACK_MAX;
}

static void
grub_loongarch64_stack_push (grub_loongarch64_stack_t stack, grub_uint64_t x)
{
  if (stack->top == stack->count)
    return;
  stack->data[++stack->top] = x;
}

static grub_uint64_t
grub_loongarch64_stack_pop (grub_loongarch64_stack_t stack)
{
  if (stack->top == -1)
    return -1;
  return stack->data[stack->top--];
}

void
grub_loongarch64_sop_push (grub_loongarch64_stack_t stack, grub_int64_t offset)
{
  grub_loongarch64_stack_push (stack, offset);
}

/* opr2 = pop (), opr1 = pop (), push (opr1 - opr2) */
void
grub_loongarch64_sop_sub (grub_loongarch64_stack_t stack)
{
  grub_uint64_t a, b;
  b = grub_loongarch64_stack_pop (stack);
  a = grub_loongarch64_stack_pop (stack);
  grub_loongarch64_stack_push (stack, a - b);
}

/* opr2 = pop (), opr1 = pop (), push (opr1 << opr2) */
void
grub_loongarch64_sop_sl (grub_loongarch64_stack_t stack)
{
  grub_uint64_t a, b;
  b = grub_loongarch64_stack_pop (stack);
  a = grub_loongarch64_stack_pop (stack);
  grub_loongarch64_stack_push (stack, a << b);
}

/* opr2 = pop (), opr1 = pop (), push (opr1 >> opr2) */
void
grub_loongarch64_sop_sr (grub_loongarch64_stack_t stack)
{
  grub_uint64_t a, b;
  b = grub_loongarch64_stack_pop (stack);
  a = grub_loongarch64_stack_pop (stack);
  grub_loongarch64_stack_push (stack, a >> b);
}

/* opr2 = pop (), opr1 = pop (), push (opr1 + opr2) */
void
grub_loongarch64_sop_add (grub_loongarch64_stack_t stack)
{
  grub_uint64_t a, b;
  b = grub_loongarch64_stack_pop (stack);
  a = grub_loongarch64_stack_pop (stack);
  grub_loongarch64_stack_push (stack, a + b);
}

/* opr2 = pop (), opr1 = pop (), push (opr1 & opr2) */
void
grub_loongarch64_sop_and (grub_loongarch64_stack_t stack)
{
  grub_uint64_t a, b;
  b = grub_loongarch64_stack_pop (stack);
  a = grub_loongarch64_stack_pop (stack);
  grub_loongarch64_stack_push (stack, a & b);
}

/* opr3 = pop (), opr2 = pop (), opr1 = pop (), push (opr1 ? opr2 : opr3) */
void
grub_loongarch64_sop_if_else (grub_loongarch64_stack_t stack)
{
  grub_uint64_t a, b, c;
  c = grub_loongarch64_stack_pop (stack);
  b = grub_loongarch64_stack_pop (stack);
  a = grub_loongarch64_stack_pop (stack);

  if (a) {
      grub_loongarch64_stack_push (stack, b);
  } else {
      grub_loongarch64_stack_push (stack, c);
  }
}

/* opr1 = pop (), (*(uint32_t *) PC) [14 ... 10] = opr1 [4 ... 0] */
void
grub_loongarch64_sop_32_s_10_5 (grub_loongarch64_stack_t stack,
				grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place |= ((a & 0x1f) << 10);
}

/* opr1 = pop (), (*(uint32_t *) PC) [21 ... 10] = opr1 [11 ... 0] */
void
grub_loongarch64_sop_32_u_10_12 (grub_loongarch64_stack_t stack,
				 grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place = *place | ((a & 0xfff) << 10);
}

/* opr1 = pop (), (*(uint32_t *) PC) [21 ... 10] = opr1 [11 ... 0] */
void
grub_loongarch64_sop_32_s_10_12 (grub_loongarch64_stack_t stack,
				 grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place = (*place) | ((a & 0xfff) << 10);
}

/* opr1 = pop (), (*(uint32_t *) PC) [25 ... 10] = opr1 [15 ... 0] */
void
grub_loongarch64_sop_32_s_10_16 (grub_loongarch64_stack_t stack,
				 grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place = (*place) | ((a & 0xffff) << 10);
}

/* opr1 = pop (), (*(uint32_t *) PC) [25 ... 10] = opr1 [17 ... 2] */
void
grub_loongarch64_sop_32_s_10_16_s2 (grub_loongarch64_stack_t stack,
				    grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place = (*place) | (((a >> 2) & 0xffff) << 10);
}

/* opr1 = pop (), (*(uint32_t *) PC) [24 ... 5] = opr1 [19 ... 0] */
void
grub_loongarch64_sop_32_s_5_20 (grub_loongarch64_stack_t stack, grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place = (*place) | ((a & 0xfffff)<<5);
}

/* opr1 = pop (), (*(uint32_t *) PC) [4 ... 0] = opr1 [22 ... 18] */
void
grub_loongarch64_sop_32_s_0_5_10_16_s2 (grub_loongarch64_stack_t stack,
					grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);

  *place =(*place) | (((a >> 2) & 0xffff) << 10);
  *place =(*place) | ((a >> 18) & 0x1f);
}

/*
   opr1 = pop ()
   (*(uint32_t *) PC) [9 ... 0] = opr1 [27 ... 18],
   (*(uint32_t *) PC) [25 ... 10] = opr1 [17 ... 2]
*/
void
grub_loongarch64_sop_32_s_0_10_10_16_s2 (grub_loongarch64_stack_t stack,
					 grub_uint64_t *place)
{
  grub_uint64_t a = grub_loongarch64_stack_pop (stack);
  *place =(*place) | (((a >> 2) & 0xffff) << 10);
  *place =(*place) | ((a >> 18) & 0x3ff);
}

/*
R_LARCH_B26              66        (*(uint32_t *) PC)[9:0] = (S+A-PC)[27:18]
                                   (*(uint32_t *) PC)[25:10] = (S+A-PC)[17:2]
*/
void grub_loongarch64_b26 (grub_uint32_t *place, grub_uint64_t offset)
{
  grub_uint32_t a = (grub_uint32_t*) offset - *place;

  *place |= ((a >> 18) & 0x3ff);
  *place |= (((a >> 2) & 0xffff) << 10);
}

/*
R_LARCH_ABS_HI20         67        (*(uint32_t *) PC)[24:5] = (S+A)[31:12]
*/
void grub_loongarch64_abs_hi20 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= (((offset >> 12) & 0xfffff) << 5);
}

/*
R_LARCH_ABS_LO12         68        (*(uint32_t *) PC)[21:10] = (S+A)[11:0]
*/
void grub_loongarch64_abs_lo12 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= ((offset & 0xfff) << 10);
}

/*
R_LARCH_ABS64_LO20       69        (*(uint32_t *) PC)[24:5] = (S+A)[51:32]
*/
void grub_loongarch64_abs64_lo20 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= (((offset >> 32) & 0xfffff) << 5);
}

/*
R_LARCH_ABS64_HI12       70        (*(uint32_t *) PC)[21:10] = (S+A)[63:52]
*/
void grub_loongarch64_abs64_hi12 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= (((offset >> 52) & 0xfff) << 10);
}

/*
lu12i.w
R_LARCH_GOT64_HI20       79        (*(uint32_t *) PC)[24:5] = (GP+G)[31:12]
*/
void grub_loongarch64_got64_hi20 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= (((offset >> 12) & 0xfffff) << 5);
}

/*
R_LARCH_GOT64_LO12       80        (*(uint32_t *) PC)[21:10] = (GP+G)[11:0]
*/
void grub_loongarch64_got64_lo12 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= ((offset & 0xfff) << 10);
}

/*
lu52i.d
R_LARCH_GOT64_LO20       81        (*(uint32_t *) PC)[21:10] = (GP+G)[63:52]
*/
void grub_loongarch64_got64_lo20 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= (((offset >> 52 ) & 0xfff) << 10);
}

/*
lu32i.d
R_LARCH_GOT64_HI12       82        (*(uint32_t *) PC)[24:5] = (GP+G)[51:32]
*/
void grub_loongarch64_got64_hi12 (grub_uint32_t *place, grub_uint64_t offset)
{
  *place |= (((offset >> 32 ) & 0xfffff) << 5);
}

grub_err_t
grub_loongarch64_dl_get_tramp_got_size (const void *ehdr, grub_size_t *tramp,
				 grub_size_t *got)
{
  const Elf64_Ehdr *e = ehdr;
  grub_size_t cntt = 0, cntg = 0;
  const Elf64_Shdr *s;
  unsigned i;

  for (i = 0, s = (Elf64_Shdr *) ((char *) e + grub_le_to_cpu64 (e->e_shoff));
       i < grub_le_to_cpu16 (e->e_shnum);
       i++, s = (Elf64_Shdr *) ((char *) s + grub_le_to_cpu16 (e->e_shentsize)))
    if (s->sh_type == grub_cpu_to_le32_compile_time (SHT_RELA))
      {
	const Elf64_Rela *rel, *max;

	for (rel = (Elf64_Rela *) ((char *) e + grub_le_to_cpu64 (s->sh_offset)),
	       max = (const Elf64_Rela *) ((char *) rel + grub_le_to_cpu64 (s->sh_size));
	     rel < max; rel = (const Elf64_Rela *) ((char *) rel + grub_le_to_cpu64 (s->sh_entsize)))
	  switch (ELF64_R_TYPE (grub_le_to_cpu64 (rel->r_info)))
	    {
	    case R_LARCH_GOT64_HI20:
	    case R_LARCH_GOT64_LO20:
	    case R_LARCH_GOT64_HI12:
	    case R_LARCH_GOT64_LO12:
	      cntg++;
	      break;
	    }
      }
  *tramp = 0;
  *got = cntg * sizeof (grub_uint64_t);

  return GRUB_ERR_NONE;
}
