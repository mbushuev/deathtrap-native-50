# Deathtrap release 0.0.226 graphics layer

Built from upstream revision
`576c8f28aefc595e98b98fad64f379b151b2896f` with the complete adjacent source
patch applied.
Built with Visual Studio 2022, Release/Win32; file version 1.8.8618.25.

Permanent version of the user-accepted world-depth diagnostic: set XYZRHW Z
to 0.5 only in the marked Deathtrap world pass with hardware depth disabled.
RHW is unchanged. The main patch also marks the non-widened 4:3 world pass.
No probe flag or diagnostic capture is included.
Supported internal scales are 2..4, default 3. The exact source modification
and reproduction instructions are in `source/`. The layer also consumes the
patch's exported display mode and dimensions to provide borderless and framed
window presentation without an exclusive display-mode switch.

- DDraw.dll (proxy stub):
  `8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33`
- dxwrapper.dll:
  `A01EC795A633643CEA61A7CDC62EE97793D1674733D31E8240CE98C33E145841`

The final backend remains unmodified dgVoodoo 2.86.2 D3D9 to D3D11.
Upstream: https://github.com/elishacloud/dxwrapper . License: `License.txt`.
