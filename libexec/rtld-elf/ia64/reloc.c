/*-
 * Copyright 1996, 1997, 1998, 1999 John D. Polstra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Dynamic linker for ELF.
 *
 * John Polstra <jdp@polstra.com>.
 */

#include <sys/param.h>
#include <sys/mman.h>

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "rtld.h"

extern Elf_Dyn _DYNAMIC;

/*
 * Macros for loading/storing unaligned 64-bit values.  These are
 * needed because relocations can point to unaligned data.  This
 * occurs in the DWARF2 exception frame tables generated by the
 * compiler, for instance.
 *
 * We don't use these when relocating jump slots and GOT entries,
 * since they are guaranteed to be aligned.
 *
 * XXX dfr stub for now.
 */
#define load64(p)	(*(u_int64_t *) (p))
#define store64(p, v)	(*(u_int64_t *) (p) = (v))

/* Allocate an @fptr. */

#define FPTR_CHUNK_SIZE		64

struct fptr_chunk {
	struct fptr fptrs[FPTR_CHUNK_SIZE];
};

static struct fptr_chunk first_chunk;
static struct fptr_chunk *current_chunk = &first_chunk;
static struct fptr *next_fptr = &first_chunk.fptrs[0];
static struct fptr *last_fptr = &first_chunk.fptrs[FPTR_CHUNK_SIZE];

/*
 * We use static storage initially so that we don't have to call
 * malloc during init_rtld().
 */
static struct fptr *
alloc_fptr(Elf_Addr target, Elf_Addr gp)
{
	struct fptr* fptr;

	if (next_fptr == last_fptr) {
		current_chunk = malloc(sizeof(struct fptr_chunk));
		next_fptr = &current_chunk->fptrs[0];
		last_fptr = &current_chunk->fptrs[FPTR_CHUNK_SIZE];
	}
	fptr = next_fptr;
	next_fptr++;
	fptr->target = target;
	fptr->gp = gp;
	return fptr;
}

/* Relocate a non-PLT object with addend. */
static int
reloc_non_plt_obj(Obj_Entry *obj_rtld, Obj_Entry *obj, const Elf_Rela *rela,
		  SymCache *cache, struct fptr **fptrs)
{
	Elf_Addr *where = (Elf_Addr *) (obj->relocbase + rela->r_offset);

	switch (ELF_R_TYPE(rela->r_info)) {
	case R_IA64_REL64LSB:
		/*
		 * We handle rtld's relocations in rtld_start.S
		 */
		if (obj != obj_rtld)
			store64(where,
				load64(where) + (Elf_Addr) obj->relocbase);
		break;

	case R_IA64_DIR64LSB: {
		const Elf_Sym *def;
		const Obj_Entry *defobj;
		Elf_Addr target;

		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
				  false, cache);
		if (def == NULL)
			return -1;
		target = (Elf_Addr) (defobj->relocbase + def->st_value);
		store64(where, target + rela->r_addend);
		break;
	}

	case R_IA64_FPTR64LSB: {
		/*
		 * We have to make sure that all @fptr references to
		 * the same function are identical so that code can
		 * compare function pointers. We actually only bother
		 * to ensure this within a single object. If the
		 * caller's alloca failed, we don't even ensure that.
		 */
		const Elf_Sym *def, *ref;
		const Obj_Entry *defobj;
		struct fptr *fptr = 0;
		Elf_Addr target, gp;

		/*
		 * Not sure why the call to find_symdef() doesn't work 
		 * properly (it fails if the symbol is local). Perhaps 
		 * this is a toolchain issue - revisit after we
		 * upgrade the ia64 toolchain.
		 */
		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
				  false, cache);
		if (def == NULL) {
			def = &obj->symtab[ELF_R_SYM(rela->r_info)];
			defobj = obj;
		}
		/*
		 * If this is an undefined weak reference, we need to
		 * have a zero target,gp fptr, not pointing to relocbase.
		 * This isn't quite right.  Maybe we should check
		 * explicitly for def == &sym_zero.
		 */
		if (def->st_value == 0 &&
		    (ref = obj->symtab + ELF_R_SYM(rela->r_info)) &&
		    ELF_ST_BIND(ref->st_info) == STB_WEAK) {
			target = 0;
			gp = 0;
		} else {
			target = (Elf_Addr) (defobj->relocbase + def->st_value);
			gp = (Elf_Addr) defobj->pltgot;
		}

		/*
		 * Find the @fptr, using fptrs as a helper.
		 */
		if (fptrs)
			fptr = fptrs[ELF_R_SYM(rela->r_info)];
		if (!fptr) {
			fptr = alloc_fptr(target, gp);
			if (fptrs)
				fptrs[ELF_R_SYM(rela->r_info)] = fptr;
		}
		store64(where, (Elf_Addr) fptr);
		break;
	}

	default:
		_rtld_error("%s: Unsupported relocation type %d"
			    " in non-PLT relocations\n", obj->path,
			    ELF_R_TYPE(rela->r_info));
		return -1;
	}

	return(0);
}

/* Process the non-PLT relocations. */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	SymCache *cache;
	struct fptr **fptrs;

	cache = (SymCache *)alloca(obj->nchains * sizeof(SymCache));
	if (cache != NULL)
		memset(cache, 0, obj->nchains * sizeof(SymCache));

	/*
	 * When relocating rtld itself, we need to avoid using malloc.
	 */
        if (obj == obj_rtld)
		fptrs = (struct fptr **)
			alloca(obj->nchains * sizeof(struct fptr *));
	else
		fptrs = (struct fptr **)
			malloc(obj->nchains * sizeof(struct fptr *));

	if (fptrs == NULL)
		return -1;
	memset(fptrs, 0, obj->nchains * sizeof(struct fptr *));

	/* Perform relocations without addend if there are any: */
	rellim = (const Elf_Rel *) ((caddr_t) obj->rel + obj->relsize);
	for (rel = obj->rel;  obj->rel != NULL && rel < rellim;  rel++) {
		Elf_Rela locrela;

		locrela.r_info = rel->r_info;
		locrela.r_offset = rel->r_offset;
		locrela.r_addend = 0;
		if (reloc_non_plt_obj(obj_rtld, obj, &locrela, cache, fptrs))
			return -1;
	}

	/* Perform relocations with addend if there are any: */
	relalim = (const Elf_Rela *) ((caddr_t) obj->rela + obj->relasize);
	for (rela = obj->rela;  obj->rela != NULL && rela < relalim;  rela++) {
		if (reloc_non_plt_obj(obj_rtld, obj, rela, cache, fptrs))
			return -1;
	}

	/*
	 * Remember the fptrs in case of later calls to dlsym(). Don't 
	 * bother for rtld - we will lazily create a table in
	 * make_function_pointer(). At this point we still can't risk
	 * calling malloc().
	 */
	if (obj != obj_rtld)
		obj->priv = fptrs;
	else
		obj->priv = NULL;

	return 0;
}

/* Process the PLT relocations. */
int
reloc_plt(Obj_Entry *obj)
{
	/* All PLT relocations are the same kind: Elf_Rel or Elf_Rela. */
	if (obj->pltrelsize != 0) {
		const Elf_Rel *rellim;
		const Elf_Rel *rel;

		rellim = (const Elf_Rel *)
			((char *)obj->pltrel + obj->pltrelsize);
		for (rel = obj->pltrel;  rel < rellim;  rel++) {
			Elf_Addr *where;

			assert(ELF_R_TYPE(rel->r_info) == R_IA64_IPLTLSB);

			/* Relocate the @fptr pointing into the PLT. */
			where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
			*where += (Elf_Addr)obj->relocbase;
		}
	} else {
		const Elf_Rela *relalim;
		const Elf_Rela *rela;

		relalim = (const Elf_Rela *)
			((char *)obj->pltrela + obj->pltrelasize);
		for (rela = obj->pltrela;  rela < relalim;  rela++) {
			Elf_Addr *where;

			assert(ELF_R_TYPE(rela->r_info) == R_IA64_IPLTLSB);

			/* Relocate the @fptr pointing into the PLT. */
			where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
			*where += (Elf_Addr)obj->relocbase;
		}
	}
	return 0;
}

/* Relocate the jump slots in an object. */
int
reloc_jmpslots(Obj_Entry *obj)
{
	if (obj->jmpslots_done)
		return 0;
	/* All PLT relocations are the same kind: Elf_Rel or Elf_Rela. */
	if (obj->pltrelsize != 0) {
		const Elf_Rel *rellim;
		const Elf_Rel *rel;

		rellim = (const Elf_Rel *)
			((char *)obj->pltrel + obj->pltrelsize);
		for (rel = obj->pltrel;  rel < rellim;  rel++) {
			Elf_Addr *where;
			const Elf_Sym *def;
			const Obj_Entry *defobj;

			assert(ELF_R_TYPE(rel->r_info) == R_IA64_IPLTLSB);
			where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
			def = find_symdef(ELF_R_SYM(rel->r_info), obj,
					  &defobj, true, NULL);
			if (def == NULL)
				return -1;
			reloc_jmpslot(where,
				      (Elf_Addr)(defobj->relocbase
						 + def->st_value),
				      defobj);
		}
	} else {
		const Elf_Rela *relalim;
		const Elf_Rela *rela;

		relalim = (const Elf_Rela *)
			((char *)obj->pltrela + obj->pltrelasize);
		for (rela = obj->pltrela;  rela < relalim;  rela++) {
			Elf_Addr *where;
			const Elf_Sym *def;
			const Obj_Entry *defobj;

			/* assert(ELF_R_TYPE(rela->r_info) == R_ALPHA_JMP_SLOT); */
			where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
			def = find_symdef(ELF_R_SYM(rela->r_info), obj,
					  &defobj, true, NULL);
			if (def == NULL)
				return -1;
			reloc_jmpslot(where,
				      (Elf_Addr)(defobj->relocbase
						 + def->st_value),
				      defobj);
		}
	}
	obj->jmpslots_done = true;
	return 0;
}

/* Fixup the jump slot at "where" to transfer control to "target". */
Elf_Addr
reloc_jmpslot(Elf_Addr *where, Elf_Addr target, const Obj_Entry *obj)
{
	Elf_Addr stubaddr;

	dbg(" reloc_jmpslot: where=%p, target=%p, gp=%p",
	    (void *)where, (void *)target, (void *)obj->pltgot);
	stubaddr = *where;
	if (stubaddr != target) {

		/*
		 * Point this @fptr directly at the target. Update the
		 * gp value first so that we don't break another cpu
		 * which is currently executing the PLT entry.
		 */
		where[1] = (Elf_Addr) obj->pltgot;
		ia64_mf();
		where[0] = target;
		ia64_mf();
	}

	/*
	 * The caller needs an @fptr for the adjusted entry. The PLT
	 * entry serves this purpose nicely.
	 */
	return (Elf_Addr) where;
}

/*
 * XXX ia64 doesn't seem to have copy relocations.
 *
 * Returns 0 on success, -1 on failure.
 */
int
do_copy_relocations(Obj_Entry *dstobj)
{

	return 0;
}

/*
 * Return the @fptr representing a given function symbol.
 */
void *
make_function_pointer(const Elf_Sym *sym, const Obj_Entry *obj)
{
	struct fptr **fptrs = obj->priv;
	int index = sym - obj->symtab;

	if (!fptrs) {
		/*
		 * This should only happen for something like
		 * dlsym("dlopen"). Actually, I'm not sure it can ever 
		 * happen.
		 */
		fptrs = (struct fptr **)
			malloc(obj->nchains * sizeof(struct fptr *));
		memset(fptrs, 0, obj->nchains * sizeof(struct fptr *));
		((Obj_Entry*) obj)->priv = fptrs;
	}
	if (!fptrs[index]) {
		Elf_Addr target, gp;
		target = (Elf_Addr) (obj->relocbase + sym->st_value);
		gp = (Elf_Addr) obj->pltgot;
		fptrs[index] = alloc_fptr(target, gp);
	}
	return fptrs[index];
}

void
call_initfini_pointer(const Obj_Entry *obj, Elf_Addr target)
{
	struct fptr fptr;

	fptr.gp = (Elf_Addr) obj->pltgot;
	fptr.target = target;
	dbg(" initfini: target=%p, gp=%p",
	    (void *) fptr.target, (void *) fptr.gp);
	((InitFunc) &fptr)();
}

/* Initialize the special PLT entries. */
void
init_pltgot(Obj_Entry *obj)
{
	const Elf_Dyn *dynp;
	Elf_Addr *pltres = 0;

	/*
	 * Find the PLT RESERVE section.
	 */
	for (dynp = obj->dynamic;  dynp->d_tag != DT_NULL;  dynp++) {
		if (dynp->d_tag == DT_IA64_PLT_RESERVE)
			pltres = (u_int64_t *)
				(obj->relocbase + dynp->d_un.d_ptr);
	}
	if (!pltres)
		errx(1, "Can't find DT_IA64_PLT_RESERVE entry");

	/*
	 * The PLT RESERVE section is used to get values to pass to
	 * _rtld_bind when lazy binding.
	 */
	pltres[0] = (Elf_Addr) obj;
	pltres[1] = FPTR_TARGET(_rtld_bind_start);
	pltres[2] = FPTR_GP(_rtld_bind_start);
}
