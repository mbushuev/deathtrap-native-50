# Deathtrap release 0.0.225 graphics layer

Source: Deathtrap dxwrapper fork, branch `codex/release-225-depth`, commit
`f56dfb3665f711fb1f09de3d8af9d53cad66727f`, based on upstream revision
`576c8f28aefc595e98b98fad64f379b151b2896f`.
Built with Visual Studio 2022, Release/Win32; file version 1.8.8618.25.

Permanent version of the user-accepted world-depth diagnostic: set XYZRHW Z
to 0.5 only in the marked Deathtrap world pass with hardware depth disabled.
RHW is unchanged. The main patch also marks the non-widened 4:3 world pass.
No probe flag or diagnostic capture is included.
Supported internal scales are 2..4, default 3. The exact source modification
and reproduction instructions are in `source/`.

- DDraw.dll (proxy stub):
  `8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33`
- dxwrapper.dll:
  `BC633D310B125C704EE489286BAB86607EFCEB2ACA6B4BC22E9B66667FD04170`

The final backend remains unmodified dgVoodoo 2.86.2 D3D9 to D3D11.
Upstream: https://github.com/elishacloud/dxwrapper . License: `License.txt`.
