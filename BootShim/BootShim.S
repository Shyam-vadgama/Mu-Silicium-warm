_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#ifndef TEXT_OFFSET
#define TEXT_OFFSET 0
#endif

#ifndef KERNEL_FLAGS
#define KERNEL_FLAGS 0xa
#endif

#ifndef KERNEL_IMG_SIZE
#define KERNEL_IMG_SIZE 0x04000000
#endif

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  .quad TEXT_OFFSET     /* Text Offset: 0 = relocatable anywhere in RAM */

_StackSize:
  .quad KERNEL_IMG_SIZE /* Image Size: 64MB effective capacity */
  .quad KERNEL_FLAGS    /* Flags: LE, 4K page, relocatable anywhere in RAM */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

#ifndef FB_BASE
#define FB_BASE 0x82D00000
#endif

#ifndef FB_SIZE
#define FB_SIZE 0x00D00000
#endif

#ifndef FB_BASE_ALT
#define FB_BASE_ALT 0x9EC00000
#endif

#ifndef FB_SIZE_ALT
#define FB_SIZE_ALT 0x00800000
#endif

#define STAGE_DELAY 0x40000000

_Start:
  /* STAGE 1: Fill Framebuffer with Solid OPAQUE RED (0xFFFF0000) */
  /* Primary Framebuffer (0x82D00000) */
  ldr   x8, =FB_BASE
  ldr   x9, =FB_SIZE
  movz  x10, #0xFFFF, lsl #16
  orr   x10, x10, x10, lsl #32
.Lred_loop1:
  stp   x10, x10, [x8], #16
  subs  x9, x9, #16
  b.ne  .Lred_loop1

  /* Alternate Framebuffer (0x9EC00000) */
  ldr   x8, =FB_BASE_ALT
  ldr   x9, =FB_SIZE_ALT
.Lred_loop2:
  stp   x10, x10, [x8], #16
  subs  x9, x9, #16
  b.ne  .Lred_loop2

  /* Delay so RED is clearly visible (~1.0s) */
  movz  x8, #0x4000, lsl #16
.Lred_delay:
  subs  x8, x8, #1
  b.ne  .Lred_delay

  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _SkipCopy
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  b     _Copied

_SkipCopy:
  ldr   x6, =FD_SIZE
  add   x4, x4, x6

_Copied:
  /* STAGE 2: Fill Framebuffer with Solid OPAQUE GREEN (0xFF00FF00) */
  ldr   x8, =FB_BASE
  ldr   x9, =FB_SIZE
  movz  x10, #0xFF00
  movk  x10, #0xFF00, lsl #16
  orr   x10, x10, x10, lsl #32
.Lgreen_loop1:
  stp   x10, x10, [x8], #16
  subs  x9, x9, #16
  b.ne  .Lgreen_loop1

  ldr   x8, =FB_BASE_ALT
  ldr   x9, =FB_SIZE_ALT
.Lgreen_loop2:
  stp   x10, x10, [x8], #16
  subs  x9, x9, #16
  b.ne  .Lgreen_loop2

  /* Delay so GREEN is clearly visible (~1.0s) */
  movz  x8, #0x4000, lsl #16
.Lgreen_delay:
  subs  x8, x8, #1
  b.ne  .Lgreen_delay

#ifdef PAYLOAD_BASE
  /* x4 now points just past the FD: an optional "XNUP" payload may follow */
  ldr   w2, [x4]
  movz  w3, #0x4E58
  movk  w3, #0x5055, lsl #16
  cmp   w2, w3
  b.ne  _NoPayload
  ldr   w6, [x4, #4]    /* Payload Size */
  add   x6, x6, #23     /* Header + Round Up to 16 */
  and   x6, x6, #~15
  movz  x5, #(PAYLOAD_BASE >> 16), lsl #16

_PayloadLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _PayloadLoop

_NoPayload:
#endif
  /* STAGE 3: Fill Framebuffer with Solid OPAQUE BLUE (0xFF0000FF) */
  ldr   x8, =FB_BASE
  ldr   x9, =FB_SIZE
  movz  x10, #0x00FF
  movk  x10, #0xFF00, lsl #16
  orr   x10, x10, x10, lsl #32
.Lblue_loop1:
  stp   x10, x10, [x8], #16
  subs  x9, x9, #16
  b.ne  .Lblue_loop1

  ldr   x8, =FB_BASE_ALT
  ldr   x9, =FB_SIZE_ALT
.Lblue_loop2:
  stp   x10, x10, [x8], #16
  subs  x9, x9, #16
  b.ne  .Lblue_loop2

  /* Delay so BLUE is clearly visible (~1.0s) */
  movz  x8, #0x4000, lsl #16
.Lblue_delay:
  subs  x8, x8, #1
  b.ne  .Lblue_delay

  ldr   x5, =FD_BASE

_Entry:
  br    x5

_Dead:
  b     _Dead

.text
.align 4

_Payload:
