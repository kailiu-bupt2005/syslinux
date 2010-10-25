;; -----------------------------------------------------------------------
;;
;;   Copyright 1994-2009 H. Peter Anvin - All Rights Reserved
;;   Copyright 2009-2010 Intel Corporation; author: H. Peter Anvin
;;
;;   This program is free software; you can redistribute it and/or modify
;;   it under the terms of the GNU General Public License as published by
;;   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
;;   Boston MA 02111-1307, USA; either version 2 of the License, or
;;   (at your option) any later version; incorporated herein by reference.
;;
;; -----------------------------------------------------------------------

;;
;; runkernel.inc
;;
;; Common code for running a Linux kernel
;;

;
; Hook macros, that may or may not be defined
;
%ifndef HAVE_UNLOAD_PREP
%macro UNLOAD_PREP 0
%endmacro
%endif

;
; writestr_qchk: writestr, except allows output to be suppressed
;		assumes CS == DS
;
writestr_qchk:
		test byte [QuietBoot],QUIET_FLAG
		jz writestr
		ret

		section .data16
crlfloading_msg	db CR, LF
loading_msg     db 'Loading ', 0
dotdot_msg      db '.'
dot_msg         db '.', 0
ready_msg	db 'ready.', CR, LF, 0
err_oldkernel   db 'Cannot load a ramdisk with an old kernel image.'
                db CR, LF, 0
err_noinitrd    db CR, LF, 'Could not find ramdisk image: ', 0

boot_image      db 'BOOT_IMAGE='
boot_image_len  equ $-boot_image
		db 0

		section .bss16
		alignb 4
MyHighMemSize	resd 1			; Possibly adjusted highmem size
RamdiskMax	resd 1			; Highest address for ramdisk
KernelSize	resd 1			; Size of kernel in bytes
KernelSects	resd 1			; Size of kernel in sectors
KernelEnd	resd 1			; Ending address of the kernel image
InitRDStart	resd 1			; Start of initrd (pre-relocation)
InitRDEnd	resd 1			; End of initrd (pre-relocation)
CmdLineLen	resw 1			; Length of command line including null
CmdLineEnd	resw 1			; End of the command line in real_mode_seg
SetupSecs	resw 1			; Number of setup sectors (+bootsect)
KernelVersion	resw 1			; Kernel protocol version
;
; These are derived from the command-line parser
;
InitRDPtr	resw 1			; Pointer to initrd= option in command line
LoadFlags	resb 1			; Loadflags from kernel
QuietBoot	resb 1			; Set if a quiet boot is requested
