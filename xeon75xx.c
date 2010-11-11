/* Copyright (C) 2009/2010 Intel Corporation

   Decode Intel Xeon75xx memory errors. Requires the mce-75xx.ko driver
   load. The core errors are the same as Nehalem.

   mcelog is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   mcelog is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system; if not, write to the Free Software Foundation, 
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 

   Author: Andi Kleen 
*/

#include <stdio.h>
#include <stddef.h>
#include "mcelog.h"
#include "xeon75xx.h"

/* This used to decode the old xeon 75xx memory error aux format. But that has never
   been merged into mainline kernels, so removed it again. */

void 
xeon75xx_memory_error(struct mce *m, unsigned msize, int *channel, int *dimm)
{
}


void xeon75xx_decode_dimm(struct mce *m, unsigned msize)
{
}
