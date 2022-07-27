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
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/i18n.h>
#include <grub/cpu/reloc.h>

/* Check if EHDR is a valid ELF header.  */
grub_err_t
grub_arch_dl_check_header (void *ehdr)
{
  Elf_Ehdr *e = ehdr;

  /* Check the magic numbers.  */
  if (e->e_ident[EI_CLASS] != ELFCLASS64
      || e->e_ident[EI_DATA] != ELFDATA2LSB || e->e_machine != EM_LOONGARCH)
    return grub_error (GRUB_ERR_BAD_OS, N_("invalid arch-dependent ELF magic"));

  return GRUB_ERR_NONE;
}

#pragma GCC diagnostic ignored "-Wcast-align"

/*
 * Unified function for both REL and RELA.
 */
grub_err_t
grub_arch_dl_relocate_symbols (grub_dl_t mod, void *ehdr,
			       Elf_Shdr *s, grub_dl_segment_t seg)
{
  Elf_Rel *rel, *max;
  unsigned unmatched_got_pc_page = 0;
  struct grub_loongarch64_stack stack;
  grub_loongarch64_stack_init (&stack);

  for (rel = (Elf_Rel *) ((char *) ehdr + s->sh_offset),
	 max = (Elf_Rel *) ((char *) rel + s->sh_size);
       rel < max;
       rel = (Elf_Rel *) ((char *) rel + s->sh_entsize))
    {
      Elf_Sym *sym;
      void *place;
      grub_uint64_t sym_addr;

      if (rel->r_offset >= seg->size)
	return grub_error (GRUB_ERR_BAD_MODULE,
			   "reloc offset is outside the segment");

      sym = (Elf_Sym *) ((char*)mod->symtab
			 + mod->symsize * ELF_R_SYM (rel->r_info));

      sym_addr = sym->st_value;
      if (s->sh_type == SHT_RELA)
	sym_addr += ((Elf_Rela *) rel)->r_addend;

      place = (grub_uint64_t *) ((grub_addr_t)seg->addr + rel->r_offset);

      switch (ELF_R_TYPE (rel->r_info))
	{
	case R_LARCH_64:
	  {
	    grub_uint64_t *abs_place = place;

	    grub_dprintf ("dl", "  reloc_abs64 %p => 0x%016llx\n",
			  place, (unsigned long long) sym_addr);

	    *abs_place = (grub_uint64_t) sym_addr;
	  }
	  break;
	case R_LARCH_MARK_LA:
	  break;
	case R_LARCH_SOP_PUSH_PCREL:
	case R_LARCH_SOP_PUSH_PLT_PCREL:
	  grub_loongarch64_sop_push (&stack, sym_addr - (grub_uint64_t)place);
	  break;
	case R_LARCH_B26:
	  grub_loongarch64_b26 (place, sym_addr - (grub_uint64_t) place);
	  break;
	case R_LARCH_PCALA_HI20:
	    {
	      grub_int32_t si20, si12;

	      si20 = ((grub_uint64_t) sym_addr & ~0xfffULL) - (((grub_uint64_t) place) & ~0xfffULL);
	      si12 = ((grub_uint64_t) sym_addr - (grub_uint64_t) place) & 0xfffULL;
	      if (si12 > 0x7ff)
		si20 += 0x1000;

	      grub_loongarch64_pcala_hi20 (place, si20);
	    }
	  break;
	case R_LARCH_PCALA_LO12:
	  grub_loongarch64_pcala_lo12 (place, sym_addr);
	  break;
	case R_LARCH_GOT_PC_HI20:
	    {
	      grub_uint64_t *gp = mod->gotptr;
	      grub_int32_t si20;
	      grub_int32_t si12;
	      Elf_Rela *rel2;

	      si20 = ((grub_uint64_t) gp & ~0xfffULL) - (((grub_uint64_t) place) & ~0xfffULL);
	      si12 = ((grub_uint64_t) gp - (grub_uint64_t) place) & 0xfffULL;
	      if (si12 > 0x7ff)
		si20 += 0x1000;

	      *gp = (grub_uint64_t) sym_addr;
	      mod->gotptr = gp + 1;
	      unmatched_got_pc_page++;
	      grub_dprintf("dl", "  reloc_got %p => 0x%016llx (0x%016llx)\n",
			   place, (unsigned long long) sym_addr, (unsigned long long) gp);
	      grub_loongarch64_got_pc_hi20 (place, si20);
	      for (rel2 = (Elf_Rela *) ((char *) rel + s->sh_entsize);
		   rel2 < (Elf_Rela *) max;
		   rel2 = (Elf_Rela *) ((char *) rel2 + s->sh_entsize))
		if (ELF_R_SYM (rel2->r_info)
		    == ELF_R_SYM (rel->r_info)
		    && ((Elf_Rela *) rel)->r_addend == rel2->r_addend
		    && ELF_R_TYPE (rel2->r_info) == R_LARCH_GOT_PC_LO12)
		  {
		    grub_loongarch64_got_pc_lo12 ((void *) ((grub_addr_t) seg->addr + rel2->r_offset),
						  (grub_uint64_t)gp);
		    break;
		  }
	      if (rel2 >= (Elf_Rela *) max)
		return grub_error (GRUB_ERR_BAD_MODULE,
				   "GOT_PC_HI20 without matching GOT_PC_LO12");
	    }
	  break;
	case R_LARCH_GOT_PC_LO12:
	  if (unmatched_got_pc_page == 0)
	    return grub_error (GRUB_ERR_BAD_MODULE,
			       "GOT_PC_LO12 without matching GOT_PC_HI2");
	  unmatched_got_pc_page--;
	  break;
	GRUB_LOONGARCH64_RELOCATION (&stack, place, sym_addr)
	default:
	  {
	    char rel_info[17]; /* log16(2^64) = 16, plus NUL.  */

	    grub_snprintf (rel_info, sizeof (rel_info) - 1, "%" PRIxGRUB_UINT64_T,
			   (grub_uint64_t) ELF_R_TYPE (rel->r_info));
	    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
			       N_("relocation 0x%s is not implemented yet"), rel_info);
	  }
	  break;
	}
    }
  return GRUB_ERR_NONE;
}
