# Reproducing the Deathtrap dxwrapper build

The bundled binaries are built from the public dxwrapper repository with the
adjacent `deathtrap-dxwrapper.patch` and
`deathtrap-page-restore-present.patch` applied in that order.

- Upstream: https://github.com/elishacloud/dxwrapper
- Exact upstream base: `576c8f28aefc595e98b98fad64f379b151b2896f`
- Toolchain: Visual Studio 2022, Release/Win32

Reproduction steps:

```powershell
git clone --recursive https://github.com/elishacloud/dxwrapper.git
Set-Location dxwrapper
git checkout 576c8f28aefc595e98b98fad64f379b151b2896f
git apply path\to\deathtrap-dxwrapper.patch
git apply path\to\deathtrap-page-restore-present.patch
git submodule update --init --recursive
msbuild dxwrapper.sln /t:Rebuild /m:1 /p:Configuration=Release /p:Platform=Win32 /p:PreBuildEventUseInBuild=false
```

Expected outputs and SHA-256 hashes:

- `bin/Release/stub.dll` (packaged as `DDraw.dll`):
  `8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33`
- `bin/Release/dxwrapper.dll`:
  `304C3A528AD1AB4B25BBEE48DA0371A9DB435DF1CDF733CD1BE13C2FF27BCC8E`

dxwrapper is licensed under the Mozilla Public License 2.0. The complete
license is included in the repository as
`third_party/deathtrap-dxwrapper-release225/License.txt` and in the release
archive as `licenses/dxwrapper.txt`.
