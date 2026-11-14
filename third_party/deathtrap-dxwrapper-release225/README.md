# Deathtrap Native 50 graphics layer

Built from upstream revision
`576c8f28aefc595e98b98fad64f379b151b2896f` with the complete adjacent source
patch applied.
Built with Visual Studio 2022, Release/Win32; file version 1.8.8615.25.

The marked Deathtrap world pass uses a depth buffer for pre-transformed 3D
polygons. Depth comes from the reciprocal camera-space distance already
provided in RHW. The two skull lintels in Inversion carry a separate
projective texture coordinate so their existing depth and geometry can be
used with one mapping across both triangles of the trapezoid.
Blended polygons can test depth with writes disabled. Screen-space quads
and HUD draws retain their original ordering. If creating or binding the
depth surface fails, world draws use the existing constant-depth fallback.
Supported internal scales are 2..4, default 3. The exact source modification
and reproduction instructions are in `source/`. The layer also consumes the
patch's exported display mode and dimensions to provide borderless and framed
window presentation without an exclusive display-mode switch.

- DDraw.dll (proxy stub):
  `8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33`
- dxwrapper.dll:
  `C135D1B3434DD421F9CC3ED6826FD4DB7A50D528E2D8C7E6F18059DB7C2B5189`

The final backend remains unmodified dgVoodoo 2.86.2 D3D9 to D3D11.
On Steam Deck the same Dd7to9 layer hands D3D9 directly to Proton/DXVK and
uses Native 50's flip policy to suppress only internal page-restore Presents.
Upstream: https://github.com/elishacloud/dxwrapper . License: `License.txt`.
