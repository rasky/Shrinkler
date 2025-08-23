// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Amiga hunk type definitions.

*/

#pragma once

#define HUNK_UNIT          0x3E7
#define HUNK_NAME          0x3E8
#define HUNK_CODE          0x3E9
#define HUNK_DATA          0x3EA
#define HUNK_BSS           0x3EB
#define HUNK_RELOC32       0x3EC
#define HUNK_RELOC16       0x3ED
#define HUNK_RELOC8        0x3EE
#define HUNK_EXT           0x3EF
#define HUNK_SYMBOL        0x3F0
#define HUNK_DEBUG         0x3F1
#define HUNK_END           0x3F2
#define HUNK_HEADER        0x3F3
#define HUNK_OVERLAY       0x3F5
#define HUNK_BREAK         0x3F6
#define HUNK_DREL32        0x3F7
#define HUNK_DREL16        0x3F8
#define HUNK_DREL8         0x3F9
#define HUNK_LIB           0x3FA
#define HUNK_INDEX         0x3FB
#define HUNK_RELOC32SHORT  0x3FC
#define HUNK_RELRELOC32    0x3FD
#define HUNK_ABSRELOC16    0x3FE

#define HUNKF_FAST         0x10000000
#define HUNKF_CHIP         0x20000000
