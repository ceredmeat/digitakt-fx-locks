| SPDX-License-Identifier: GPL-2.0-or-later
| FX Locks: the three places it joins the firmware.
        .section .run, "ax"

| 0x40031586(view, id, delta), the view's "turn for the held steps" (was: lea -16(sp),sp ;
| move.l a2,-(sp) ; move.l d2,-(sp)), by jmp.
        .globl  fxl_enc_s
fxl_enc_s:
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        jsr     fxl_enc
        lea     12(%sp), %sp
        tst.l   %d0
        bmi.s   1f
        rts                                     | ours: d0 is the answer (1)
1:      lea     -16(%sp), %sp
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        jmp     0x4003158e

| 0x4003171c(view, id, &locked), the value a page view shows (was: lea -40(sp),sp ;
| movem.l d2/a2-a3,(sp)), by jmp.
        .globl  fxl_shown_s
fxl_shown_s:
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        jsr     fxl_shown
        lea     12(%sp), %sp
        tst.l   %d0
        bmi.s   1f
        rts
1:      lea     -40(%sp), %sp
        movem.l %d2/%a2-%a3, (%sp)
        jmp     0x40031724

| In the render handler, after the tracks' blocks: 0x40077fc2 (was: pea 0x80001a18), by jsr, so
| machine-pages (which takes 0x40077fba, just before) can be in the same build. Every register kept.
        .globl  fxl_render_s
fxl_render_s:
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        jsr     fxl_render
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        move.l  (%sp), -(%sp)                   | [ret][ret]
        move.l  %d0, -(%sp)
        move.l  #0x80001a18, %d0
        move.l  %d0, 8(%sp)                     | [d0][ret][0x80001a18]: the replaced pea
        move.l  (%sp)+, %d0
        rts
