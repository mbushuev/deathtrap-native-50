# Third-party notices

The public package includes the unmodified x86 `D3D9.dll` and `D3DImm.dll`
from dgVoodoo 2.86.2, plus a Deathtrap-specific x86 dxwrapper build derived
from the Deathtrap dxwrapper fork. `DINPUT.dll` statically links MinHook 1.3.4.
MinHook source is downloaded at configure time and is not vendored here.

## dgVoodoo2

dgVoodoo 2

Copyright (c) 2013-2025 Dege

Project: https://github.com/dege-diosg/dgVoodoo2

Official documentation and redistribution terms:
https://dgvoodoo2.dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/

The author permits individual dgVoodoo files to be shipped with a game or game
mod. Hosting or redistributing dgVoodoo as a standalone component requires the
complete original ZIP package. Deathtrap Native 50 is a game-specific patch
and redistributes only the unmodified x86 runtime DLLs required by this game.
It does not include the control-panel application.

Bundled version: 2.86.2

- `D3DImm.dll` SHA-256:
  `8B2850D0AF5F07CF2928AC9666192C3ADCB0290F10ED8F942F1594F3A4F51C73`
- `D3D9.dll` SHA-256:
  `D8D2E15BF5D0E01C89317A733492997DE8F7F562A972FE564FD17D194EE5D1F3`

## dxwrapper

dxwrapper

Copyright (c) Elisha Riedlinger

Project: https://github.com/elishacloud/dxwrapper

The bundled build applies the included Deathtrap-specific source patch to the
documented upstream base (file version 1.8.8618.25). It contains the
native-canvas implementation, permanent world-depth correction and final
window presentation support. It is used only for DirectDraw/Direct3D 1 to D3D9
conversion. Its output is passed to the bundled dgVoodoo D3D9 runtime.

- `DDraw.dll` SHA-256:
  `8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33`
- `dxwrapper.dll` SHA-256:
  `A01EC795A633643CEA61A7CDC62EE97793D1674733D31E8240CE98C33E145841`

The full license is included at
`licenses/dxwrapper.txt` in the release archive and
`third_party/deathtrap-dxwrapper-release225/License.txt` in the repository.
The complete Deathtrap modification patch, its exact upstream base revision
and reproduction instructions are included under `source/dxwrapper` in the
release archive and `third_party/deathtrap-dxwrapper-release225/source` in the
repository.

## MinHook

MinHook - The Minimalistic API Hooking Library for x64/x86

Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Hacker Disassembler Engine 32 C

Copyright (c) 2008-2009, Vyacheslav Patkov.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Hacker Disassembler Engine 64 C

Copyright (c) 2008-2009, Vyacheslav Patkov.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
