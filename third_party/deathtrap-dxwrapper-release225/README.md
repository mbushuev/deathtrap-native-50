# Deathtrap Native 50 graphics layer

Built from upstream revision
`576c8f28aefc595e98b98fad64f379b151b2896f` with the complete adjacent source
patch applied.
Built with Visual Studio 2022, Release/Win32; file version 1.8.8615.25.

The Deathtrap world queue retains the original ordering between sort layers.
Within a layer, pre-transformed 3D polygons share a depth buffer. Depth comes
from the reciprocal camera-space distance already provided in RHW.
For planar room quads that need a projective texture mapping, the native
renderer derives it from the four authored corners. The wrapper evaluates
that same mapping across both triangles and any vertices created by clipping.
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
  `1D5937E71987F23AB342B3FB7867EACD32AE2CABC5E6C6CE346CDC84BFC3DB60`

The final backend remains unmodified dgVoodoo 2.86.2 D3D9 to D3D11.
On Steam Deck the same Dd7to9 layer hands D3D9 directly to Proton/DXVK and
uses Native 50's flip policy to suppress only internal page-restore Presents.
Upstream: https://github.com/elishacloud/dxwrapper . License: `License.txt`.
