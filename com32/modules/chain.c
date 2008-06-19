/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2003-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * chain.c
 *
 * Chainload a hard disk (currently rather braindead.)
 *
 * Usage: chain hd<disk#> [<partition>] [options]
 *        chain fd<disk#> [options]
 *	  chain mbr:<id> [<partition>] [options]
 *
 * ... e.g. "chain hd0 1" will boot the first partition on the first hard
 * disk.
 *
 *
 * The mbr: syntax means search all the hard disks until one with a
 * specific MBR serial number (bytes 440-443) is found.
 *
 * Partitions 1-4 are primary, 5+ logical, 0 = boot MBR (default.)
 *
 * Options:
 *
 * -file <loader>:
 *	loads the file <loader> **from the SYSLINUX filesystem**
 *	instead of loading the boot sector.
 *
 * -seg <segment>:
 *	loads at and jumps to <seg>:0000 instead of 0000:7C00.
 *
 * -ntldr <loader>:
 *	equivalent to -seg 0x2000 -file <loader>, used with WinNT's loaders
 *
 * -swap:
 *	if the disk is not fd0/hd0, install a BIOS stub which swaps
 *	the drive numbers.
 */

#include <com32.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <console.h>
#include <minmax.h>
#include <stdbool.h>
#include <syslinux/loadfile.h>
#include <syslinux/bootrm.h>

#define SECTOR 512		/* bytes/sector */

static struct options {
  const char *loadfile;
  uint16_t keeppxe;
  uint16_t seg;
  bool swap;
} opt;

static inline void error(const char *msg)
{
  fputs(msg, stderr);
}

/*
 * Call int 13h, but with retry on failure.  Especially floppies need this.
 */
static int int13_retry(const com32sys_t *inreg, com32sys_t *outreg)
{
  int retry = 6;		/* Number of retries */
  com32sys_t tmpregs;

  if ( !outreg ) outreg = &tmpregs;

  while ( retry-- ) {
    __intcall(0x13, inreg, outreg);
    if ( !(outreg->eflags.l & EFLAGS_CF) )
      return 0;			/* CF=0, OK */
  }

  return -1;			/* Error */
}

/*
 * Query disk parameters and EBIOS availability for a particular disk.
 */
struct diskinfo {
  int disk;
  int ebios;			/* EBIOS supported on this disk */
  int cbios;			/* CHS geometry is valid */
  int head;
  int sect;
} disk_info;

static int get_disk_params(int disk)
{
  static com32sys_t getparm, parm, getebios, ebios;

  disk_info.disk = disk;
  disk_info.ebios = disk_info.cbios = 0;

  /* Get EBIOS support */
  getebios.eax.w[0] = 0x4100;
  getebios.ebx.w[0] = 0x55aa;
  getebios.edx.b[0] = disk;
  getebios.eflags.b[0] = 0x3;	/* CF set */

  __intcall(0x13, &getebios, &ebios);

  if ( !(ebios.eflags.l & EFLAGS_CF) &&
       ebios.ebx.w[0] == 0xaa55 &&
       (ebios.ecx.b[0] & 1) ) {
    disk_info.ebios = 1;
  }

  /* Get disk parameters -- really only useful for
     hard disks, but if we have a partitioned floppy
     it's actually our best chance... */
  getparm.eax.b[1] = 0x08;
  getparm.edx.b[0] = disk;

  __intcall(0x13, &getparm, &parm);

  if ( parm.eflags.l & EFLAGS_CF )
    return disk_info.ebios ? 0 : -1;

  disk_info.head = parm.edx.b[1]+1;
  disk_info.sect = parm.ecx.b[0] & 0x3f;
  if ( disk_info.sect == 0 ) {
    disk_info.sect = 1;
  } else {
    disk_info.cbios = 1;	/* Valid geometry */
  }

  return 0;
}

/*
 * Get a disk block and return a malloc'd buffer.
 * Uses the disk number and information from disk_info.
 */
struct ebios_dapa {
  uint16_t len;
  uint16_t count;
  uint16_t off;
  uint16_t seg;
  uint64_t lba;
} *dapa;

static void *read_sector(unsigned int lba)
{
  com32sys_t inreg;
  void *buf = __com32.cs_bounce;
  void *data;

  memset(&inreg, 0, sizeof inreg);

  if ( disk_info.ebios ) {
    dapa->len = sizeof(*dapa);
    dapa->count = 1;		/* 1 sector */
    dapa->off = OFFS(buf);
    dapa->seg = SEG(buf);
    dapa->lba = lba;

    inreg.esi.w[0] = OFFS(dapa);
    inreg.ds       = SEG(dapa);
    inreg.edx.b[0] = disk_info.disk;
    inreg.eax.b[1] = 0x42;	/* Extended read */
  } else {
    unsigned int c, h, s, t;

    if ( !disk_info.cbios ) {
      /* We failed to get the geometry */

      if ( lba )
	return NULL;		/* Can only read MBR */

      s = 1;  h = 0;  c = 0;
    } else {
      s = (lba % disk_info.sect) + 1;
      t = lba / disk_info.sect;	/* Track = head*cyl */
      h = t % disk_info.head;
      c = t / disk_info.head;
    }

    if ( s > 63 || h > 256 || c > 1023 )
      return NULL;

    inreg.eax.w[0] = 0x0201;	/* Read one sector */
    inreg.ecx.b[1] = c & 0xff;
    inreg.ecx.b[0] = s + (c >> 6);
    inreg.edx.b[1] = h;
    inreg.edx.b[0] = disk_info.disk;
    inreg.ebx.w[0] = OFFS(buf);
    inreg.es       = SEG(buf);
  }

  if (int13_retry(&inreg, NULL))
    return NULL;

  data = malloc(SECTOR);
  if (data)
    memcpy(data, buf, SECTOR);
  return data;
}

/* Search for a specific drive, based on the MBR signature; bytes
   440-443. */
static int find_disk(uint32_t mbr_sig, void *buf)
{
  int drive;
  bool is_me;

  for (drive = 0x80; drive <= 0xff; drive++) {
    if (get_disk_params(drive))
      continue;			/* Drive doesn't exist */
    if (!(buf = read_sector(0)))
      continue;			/* Cannot read sector */
    is_me = (*(uint32_t *)((char *)buf + 440) == mbr_sig);
    free(buf);
    if (is_me)
      return drive;
  }
  return -1;
}

/* A DOS partition table entry */
struct part_entry {
  uint8_t active_flag;		/* 0x80 if "active" */
  uint8_t start_head;
  uint8_t start_sect;
  uint8_t start_cyl;
  uint8_t ostype;
  uint8_t end_head;
  uint8_t end_sect;
  uint8_t end_cyl;
  uint32_t start_lba;
  uint32_t length;
} __attribute__((packed));


/* Search for a logical partition.  Logical partitions are actually implemented
   as recursive partition tables; theoretically they're supposed to form a
   linked list, but other structures have been seen.

   To make things extra confusing: data partition offsets are relative to where
   the data partition record is stored, whereas extended partition offsets
   are relative to the beginning of the extended partition all the way back
   at the MBR... but still not absolute! */

int nextpart;			/* Number of the next logical partition */

static struct part_entry *
find_logical_partition(int whichpart, char *table, struct part_entry *self,
		       struct part_entry *root)
{
  static struct part_entry ltab_entry;
  struct part_entry *ptab = (struct part_entry *)(table + 0x1be);
  struct part_entry *found;
  char *sector;

  int i;

  if ( *(uint16_t *)(table + 0x1fe) != 0xaa55 )
    return NULL;		/* Signature missing */

  /* We are assumed to already having enumerated all the data partitions
     in this table if this is the MBR.  For MBR, self == NULL. */

  if ( self ) {
    /* Scan the data partitions. */

    for ( i = 0 ; i < 4 ; i++ ) {
      if ( ptab[i].ostype == 0x00 || ptab[i].ostype == 0x05 ||
	   ptab[i].ostype == 0x0f || ptab[i].ostype == 0x85 )
	continue;		/* Skip empty or extended partitions */

      if ( !ptab[i].length )
	continue;

      /* Adjust the offset to account for the extended partition itself */
      ptab[i].start_lba += self->start_lba;

      /* Sanity check entry: must not extend outside the extended partition.
	 This is necessary since some OSes put crap in some entries. */
      if ( ptab[i].start_lba + ptab[i].length <= self->start_lba ||
	   ptab[i].start_lba >= self->start_lba + self->length )
	continue;

      /* OK, it's a data partition.  Is it the one we're looking for? */
      if ( nextpart++ == whichpart ) {
	memcpy(&ltab_entry, &ptab[i], sizeof ltab_entry);
	return &ltab_entry;
      }
    }
  }

  /* Scan the extended partitions. */
  for ( i = 0 ; i < 4 ; i++ ) {
    if ( ptab[i].ostype != 0x05 &&
	 ptab[i].ostype != 0x0f && ptab[i].ostype != 0x85 )
      continue;		/* Skip empty or data partitions */

    if ( !ptab[i].length )
      continue;

    /* Adjust the offset to account for the extended partition itself */
    if ( root )
      ptab[i].start_lba += root->start_lba;

    /* Sanity check entry: must not extend outside the extended partition.
       This is necessary since some OSes put crap in some entries. */
    if ( root )
      if ( ptab[i].start_lba + ptab[i].length <= root->start_lba ||
	   ptab[i].start_lba >= root->start_lba + root->length )
	continue;

    /* Process this partition */
    if ( !(sector = read_sector(ptab[i].start_lba)) )
      continue;			/* Read error, must be invalid */

    found = find_logical_partition(whichpart, sector, &ptab[i],
				   root ? root : &ptab[i]);
    free(sector);
    if (found)
      return found;
  }

  /* If we get here, there ain't nothing... */
  return NULL;
}

static void do_boot(void *boot_sector, size_t boot_size,
		    struct syslinux_rm_regs *regs)
{
  uint16_t * const bios_fbm  = (uint16_t *)0x413;
  addr_t dosmem = *bios_fbm << 10; /* Technically a low bound */
  struct syslinux_memmap *mmap;
  struct syslinux_movelist *mlist = NULL;
  addr_t endimage;
  uint8_t driveno   = regs->edx.b[0];
  uint8_t swapdrive = driveno & 0x80;
  int i;
  addr_t loadbase = opt.seg ? (opt.seg << 4) : 0x7c00;

  mmap = syslinux_memory_map();

  if (!mmap) {
    error("Cannot read system memory map");
    return;
  }

  if (loadbase < 0x7c00) {
    /* Special hack: if we are to be loaded below 0x7c00, we need to handle
       the part that goes below 0x7c00 specially, since that's where the
       shuffler lives.  To deal with that, stuff the balance at the end
       of low memory and put a small copy stub there.

       The only tricky bit is that we need to set up registers for our
       move, and then restore them to what they should be at the end of
       the code. */
    static uint8_t copy_down_code[] = {
      0xf3, 0x66, 0xa5,		/* 00: rep movsd */
      0xbe, 0, 0,		/* 03: mov si,0 */
      0xbf, 0, 0,		/* 06: mov di,0 */
      0x8e, 0xde,		/* 09: mov ds,si */
      0x8e, 0xc7,		/* 0b: mov es,di */
      0x66, 0xb9, 0, 0, 0, 0,	/* 0d: mov ecx,0 */
      0x66, 0xbe, 0, 0, 0, 0,	/* 13: mov esi,0 */
      0x66, 0xbf, 0, 0, 0, 0,	/* 19: mov edi,0 */
      0xea, 0, 0, 0, 0,		/* 1f: jmp 0:0 */
      /* pad out to segment boundary */
      0x90, 0x90, 0x90, 0x90,	/* 24: ... */
      0x90, 0x90, 0x90, 0x90,	/* 28: ... */
      0x90, 0x90, 0x90, 0x90,	/* 2c: ... */
    };
    size_t low_size  = min(boot_size, 0x7c00-loadbase);
    size_t high_size = boot_size - low_size;
    size_t low_addr  = (0x7c00 + high_size + 15) & ~15;
    size_t move_addr = (low_addr + low_size + 15) & ~15;
    const size_t move_size = sizeof copy_down_code;

    if (move_addr+move_size >= dosmem-0x7c00)
      goto too_big;

    *(uint16_t *)&copy_down_code[0x04] = regs->ds;
    *(uint16_t *)&copy_down_code[0x07] = regs->es;
    *(uint32_t *)&copy_down_code[0x0f] = regs->ecx.l;
    *(uint32_t *)&copy_down_code[0x15] = regs->esi.l;
    *(uint32_t *)&copy_down_code[0x1b] = regs->edi.l;
    *(uint16_t *)&copy_down_code[0x20] = regs->ip;
    *(uint16_t *)&copy_down_code[0x22] = regs->cs;

    regs->ecx.l = (low_size+3) >> 2;
    regs->esi.l = 0;
    regs->edi.l = loadbase & 15;
    regs->ds    = low_addr >> 4;
    regs->es    = loadbase >> 4;
    regs->cs    = move_addr >> 4;
    regs->ip    = 0;

    endimage = move_addr + move_size;

    if (high_size)
      if (syslinux_add_movelist(&mlist, 0x7c00,
				(addr_t)boot_sector+low_size, high_size))
	goto enomem;
    if (syslinux_add_movelist(&mlist, low_addr,
			      (addr_t)boot_sector, low_size))
      goto enomem;
    if (syslinux_add_movelist(&mlist, move_addr,
			      (addr_t)copy_down_code, move_size))
      goto enomem;
  } else {
    /* Nothing below 0x7c00, much simpler... */

    if (boot_size >= dosmem-0x7c00)
      goto too_big;

    endimage = loadbase + boot_size;

    if (syslinux_add_movelist(&mlist, loadbase, (addr_t)boot_sector,
			      boot_size))
      goto enomem;
  }

  if (opt.swap && driveno != swapdrive) {
    static const uint8_t swapstub_master[] = {
      /* The actual swap code */
      0x53,			/* 00: push bx */
      0x0f,0xb6,0xda,		/* 01: movzx bx,dl */
      0x2e,0x8a,0x57,0x60,	/* 04: mov dl,[cs:bx+0x60] */
      0x5b,			/* 08: pop bx */
      0xea,0,0,0,0,		/* 09: jmp far 0:0 */
      0x90,0x90,		/* 0E: nop; nop */
      /* Code to install this in the right location */
      /* Entry with DS = CS; ES = SI = 0; CX = 256 */
      0x26,0x66,0x8b,0x7c,0x4c,	/* 10: mov edi,[es:si+4*0x13] */
      0x66,0x89,0x3e,0x0a,0x00,	/* 15: mov [0x0A],edi */
      0x26,0x8b,0x3e,0x13,0x04,	/* 1A: mov di,[es:0x413] */
      0x4f,			/* 1F: dec di */
      0x26,0x89,0x3e,0x13,0x04,	/* 20: mov [es:0x413],di */
      0x66,0xc1,0xe7,0x16,	/* 25: shl edi,16+6 */
      0x26,0x66,0x89,0x7c,0x4c,	/* 29: mov [es:si+4*0x13],edi */
      0x66,0xc1,0xef,0x10,	/* 2E: shr edi,16 */
      0x8e,0xc7,		/* 32: mov es,di */
      0x31,0xff,		/* 34: xor di,di */
      0xf3,0x66,0xa5,		/* 36: rep movsd */
      0xbe,0,0,			/* 39: mov si,0 */
      0xbf,0,0,			/* 3C: mov di,0 */
      0x8e,0xde,		/* 3F: mov ds,si */
      0x8e,0xc7,		/* 41: mov es,di */
      0x66,0xb9,0,0,0,0,	/* 43: mov ecx,0 */
      0x66,0xbe,0,0,0,0,	/* 49: mov esi,0 */
      0x66,0xbf,0,0,0,0,	/* 4F: mov edi,0 */
      0xea,0,0,0,0,		/* 55: jmp 0:0 */
      /* pad out to segment boundary */
      0x90, 0x90,		/* 5A: ... */
      0x90, 0x90, 0x90, 0x90,	/* 5C: ... */
    };
    static uint8_t swapstub[1024];
    uint8_t *p;

    regs->ebx.b[0] = regs->edx.b[0] = swapdrive;

    /* Note: we can't rely on either INT 13h nor the dosmem
       vector to be correct at this stage, so we have to use an
       installer stub to put things in the right place.
       Round the installer location to a 1K boundary so the only
       possible overlap is the identity mapping. */
    endimage = (endimage + 1023) & ~1023;

    /* Create swap stub */
    memcpy(swapstub, swapstub_master, sizeof swapstub_master);
    *(uint16_t *)&swapstub[0x3a] = regs->ds;
    *(uint16_t *)&swapstub[0x3d] = regs->es;
    *(uint32_t *)&swapstub[0x45] = regs->ecx.l;
    *(uint32_t *)&swapstub[0x4b] = regs->esi.l;
    *(uint32_t *)&swapstub[0x51] = regs->edi.l;
    *(uint16_t *)&swapstub[0x56] = regs->ip;
    *(uint16_t *)&swapstub[0x58] = regs->cs;
    p = &swapstub[sizeof swapstub_master];

    /* Mapping table; start out with identity mapping everything */
    for (i = 0; i < 256; i++)
      p[i] = i;

    /* And the actual swap */
    p[driveno] = swapdrive;
    p[swapdrive] = driveno;

    /* Adjust registers */
    regs->ds = regs->cs = endimage >> 4;
    regs->es = regs->esi.l = 0;
    regs->ecx.l = sizeof swapstub >> 2;
    regs->ip = 0x10;		/* Installer offset */

    if (syslinux_add_movelist(&mlist, endimage, (addr_t)swapstub,
			      sizeof swapstub))
      goto enomem;

    endimage += sizeof swapstub;
  }

  /* Tell the shuffler not to muck with this area... */
  syslinux_add_memmap(&mmap, endimage, 0xa0000-endimage, SMT_RESERVED);

  fputs("Booting...\n", stdout);
  syslinux_shuffle_boot_rm(mlist, mmap, opt.keeppxe, regs);
  error("Chainboot failed!\n");
  return;

too_big:
  error("Loader file too large");
  return;

enomem:
  error("Out of memory");
  return;
}

int main(int argc, char *argv[])
{
  char *mbr;
  void *boot_sector = NULL;
  struct part_entry *partinfo;
  struct syslinux_rm_regs regs;
  char *drivename, *partition;
  int hd, drive, whichpart;
  int i;
  size_t boot_size = SECTOR;

  openconsole(&dev_null_r, &dev_stdcon_w);

  drivename = NULL;
  partition = NULL;

  /* Prepare the register set */
  memset(&regs, 0, sizeof regs);

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-file") && argv[i+1]) {
      opt.loadfile = argv[++i];
    } else if (!strcmp(argv[i], "-seg") && argv[i+1]) {
      uint32_t segval = strtoul(argv[++i], NULL, 0);
      if (segval < 0x50 || segval > 0x9f000) {
	error("Invalid segment");
	goto bail;
      }
      opt.seg = segval;
    } else if (!strcmp(argv[i], "-ntldr") && argv[i+1]) {
      opt.seg = 0x2000;		/* NTLDR wants this address */
      opt.loadfile = argv[++i];
    } else if (!strcmp(argv[i], "-swap")) {
      opt.swap = true;
    } else if (!strcmp(argv[i], "keeppxe")) {
      opt.keeppxe = 3;
    } else {
      if (!drivename)
	drivename = argv[i];
      else if (!partition)
	partition = argv[i];
    }
  }

  if ( !drivename ) {
    error("Usage: chain.c32 (hd#|fd#|mbr:#) [partition] [options]\n");
    goto bail;
  }

  if (opt.seg) {
    regs.es = regs.cs = regs.ss = regs.ds = regs.fs = regs.gs = opt.seg;
  } else {
    regs.ip = regs.esp.l = 0x7c00;
  }

  /* Divvy up the bounce buffer.  To keep things sector-
     aligned, give the EBIOS DAPA the first sector, then
     the MBR next, and the rest is used for the partition-
     chasing stack. */
  dapa = (struct ebios_dapa *)__com32.cs_bounce;
  mbr  = (char *)__com32.cs_bounce + SECTOR;

  drivename = argv[1];
  partition = argv[2];		/* Possibly null */

  hd = 0;
  if ( !memcmp(drivename, "mbr:", 4) ) {
    drive = find_disk(strtoul(drivename+4, NULL, 0), mbr);
    if (drive == -1) {
      error("Unable to find requested MBR signature\n");
      goto bail;
    }
  } else {
    if ( (drivename[0] == 'h' || drivename[0] == 'f') &&
	 drivename[1] == 'd' ) {
      hd = drivename[0] == 'h';
      drivename += 2;
    }
    drive = (hd ? 0x80 : 0) | strtoul(drivename, NULL, 0);
  }

  /* DOS kernels want the drive number in BL instead of DL.  Indulge them. */
  regs.ebx.b[0] = regs.edx.b[0] = drive;

  whichpart = 0;		/* Default */

  if ( partition )
    whichpart = strtoul(partition, NULL, 0);

  if ( !(drive & 0x80) && whichpart ) {
    error("Warning: Partitions of floppy devices may not work\n");
  }

  /* Get the disk geometry and disk access setup */
  if ( get_disk_params(drive) ) {
    error("Cannot get disk parameters\n");
    goto bail;
  }

  /* Get MBR */
  if ( !(mbr = read_sector(0)) ) {
    error("Cannot read Master Boot Record\n");
    goto bail;
  }

  if ( whichpart == 0 ) {
    /* Boot the MBR */
    partinfo = NULL;
    boot_sector = mbr;
  } else if ( whichpart <= 4 ) {
    /* Boot a primary partition */
    partinfo = &((struct part_entry *)(mbr + 0x1be))[whichpart-1];
    if ( partinfo->ostype == 0 ) {
      error("Invalid primary partition\n");
      goto bail;
    }
  } else {
    /* Boot a logical partition */

    nextpart = 5;
    partinfo = find_logical_partition(whichpart, mbr, NULL, NULL);

    if ( !partinfo || partinfo->ostype == 0 ) {
      error("Requested logical partition not found\n");
      goto bail;
    }
  }

  /* Do the actual chainloading */
  if (opt.loadfile) {
    fputs("Loading the boot file...\n", stdout);
    if ( loadfile(opt.loadfile, &boot_sector, &boot_size) ) {
      error("Failed to load the boot file\n");
      goto bail;
    }
  } else if (partinfo) {
    /* Actually read the boot sector */
    /* Pick the first buffer that isn't already in use */
    if ( !(boot_sector = read_sector(partinfo->start_lba)) ) {
      error("Cannot read boot sector\n");
      goto bail;
    }
  }

  if (partinfo) {
    /* 0x7BE is the canonical place for the first partition entry. */
    regs.esi.w[0] = 0x7be;
    memcpy((char *)0x7be, partinfo, sizeof(*partinfo));
  }

  do_boot(boot_sector, boot_size, &regs);

bail:
  return 255;
}
