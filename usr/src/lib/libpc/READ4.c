/* Copyright (c) 1979 Regents of the University of California */

static char sccsid[] = "@(#)READ4.c 1.5 6/10/81";

#include "h00vars.h"

long
READ4(curfile)

	register struct iorec	*curfile;
{
	long			data;
	int			retval;

	if (curfile->funit & FWRITE) {
		ERROR("%s: Attempt to read, but open for writing\n",
			curfile->pfname);
		return;
	}
	UNSYNC(curfile);
	retval = fscanf(curfile->fbuf, "%ld", &data);
	if (retval == EOF) {
		ERROR("%s: Tried to read past end of file\n", curfile->pfname);
		return;
	}
	if (retval == 0) {
		ERROR("%s: Bad data found on integer read\n", curfile->pfname);
		return;
	}
	curfile->funit &= ~EOLN;
	curfile->funit |= SYNC;
	return data;
}
