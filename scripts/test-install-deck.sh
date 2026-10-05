#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
installer="$repo_root/scripts/install-deck.sh"
test_root="$(mktemp -d)"

report_error() {
    local status="$?"
    printf 'Steam Deck installer test failed at line %s: %s\n' \
        "${BASH_LINENO[0]}" "$BASH_COMMAND" >&2
    exit "$status"
}

cleanup() {
    [[ "$test_root" == /tmp/* && -d "$test_root" ]] && rm -rf -- "$test_root"
}
trap report_error ERR
trap cleanup EXIT

fail() {
    printf 'Steam Deck installer test failed: %s\n' "$*" >&2
    exit 1
}

create_game_fixture() {
    local game="$1"
    mkdir -p -- "$game/ASYLUM"
    printf 'test executable\n' >"$game/DD_CD.EXE"
    printf 'test dungeon\n' >"$game/DUNGEON.DLL"
    printf 'OLD_KEY 1\r\n' >"$game/ASYLUM/keys.cfg"
    printf 'RESOLUTION 5RENDERING_PLATFORM 0\r\nD3D_ALLOW_MIPMAP 0\r\nD3D_ALLOW_PALETTISED 1\r\nD3D_TYPE1_SHADOWS 0\r\n' \
        >"$game/ASYLUM/config.dat"
}

assert_crlf() {
    local path="$1" lf_count cr_count
    lf_count="$(tr -cd '\n' <"$path" | wc -c)"
    cr_count="$(tr -cd '\r' <"$path" | wc -c)"
    [[ "$lf_count" -gt 0 && "$lf_count" -eq "$cr_count" ]] ||
        fail "invalid CRLF normalization: $path"
    if LC_ALL=C grep -q $'\r\r' "$path"; then
        fail "CR-CR sequence found: $path"
    fi
}

assert_text_line() {
    local path="$1" expected="$2"
    awk -v expected="$expected" '
        { sub(/\015$/, "") }
        $0 == expected { found = 1 }
        END { exit !found }
    ' "$path"
}

verify_install() {
    local game="$1" width="$2" height="$3" expect_launcher="$4" file
    for file in DINPUT.dll deathtrap_native.ini DDraw.dll dxwrapper.dll \
        dxwrapper.ini; do
        [[ -f "$game/$file" ]] || fail "missing installed file: $file"
    done
    for file in D3D9.dll D3DImm.dll dgVoodoo.conf; do
        [[ ! -e "$game/$file" ]] || fail "Deck installer retained incompatible backend: $file"
    done
    grep -q "^WindowWidth=$width" "$game/deathtrap_native.ini"
    grep -q "^WindowHeight=$height" "$game/deathtrap_native.ini"
    grep -q '^RENDERING_PLATFORM 13' "$game/ASYLUM/config.dat"
    grep -q '^D3D_ALLOW_MIPMAP 1' "$game/ASYLUM/config.dat"
    grep -q '^D3D_ALLOW_PALETTISED 0' "$game/ASYLUM/config.dat"
    grep -q '^D3D_TYPE1_SHADOWS 1' "$game/ASYLUM/config.dat"
    assert_text_line "$game/deathtrap_native.ini" \
        'VibrationOutputGainPercent=300'
    assert_text_line "$game/dxwrapper.ini" 'DdrawClearFlipBackBuffer = 1'
    assert_text_line "$game/dxwrapper.ini" 'DdrawUseExternalD3D9 = 1'
    if ((expect_launcher)); then
        [[ -f "$game/deathtrap-native50-deck-launch.sh" ]] ||
            fail 'missing fallback launch helper'
        grep -q 'WINEDLLOVERRIDES="dinput,ddraw=n,b;d3d9,d3dim=b' \
            "$game/deathtrap-native50-deck-launch.sh"
    else
        [[ ! -e "$game/deathtrap-native50-deck-launch.sh" ]] ||
            fail 'normal Proton setup retained the fallback launch helper'
    fi
    assert_crlf "$game/deathtrap_native.ini"
    assert_crlf "$game/ASYLUM/keys.cfg"
    assert_crlf "$game/ASYLUM/config.dat"
}

bash -n "$installer"
bash -n "$0"
if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$installer" "$0"
fi

direct_game="$test_root/direct/Deathtrap Dungeon"
create_game_fixture "$direct_game"
printf 'old dgVoodoo D3D9\n' >"$direct_game/D3D9.dll"
printf 'old dgVoodoo D3DImm\n' >"$direct_game/D3DImm.dll"
printf 'old dgVoodoo config\n' >"$direct_game/dgVoodoo.conf"
bash "$installer" --game-dir "$direct_game" --width 1920 --height 1080 \
    --skip-game-hash-check --skip-proton-setup >/dev/null
verify_install "$direct_game" 1920 1080 1

printf 'previous install\n' >"$direct_game/DINPUT.dll"
bash "$installer" --game-dir "$direct_game" --width 1280 --height 800 \
    --skip-game-hash-check --skip-proton-setup >/dev/null
latest_backup="$(find "$direct_game/back" -mindepth 1 -maxdepth 1 -type d \
    -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
grep -q '^previous install$' "$latest_backup/DINPUT.dll"
verify_install "$direct_game" 1280 800 1

release_root="$test_root/release"
mkdir -p -- "$release_root/payload/dxwrapper" "$release_root/payload/dgVoodoo"
cp -- "$installer" "$release_root/INSTALL-DECK.sh"
cp -- "$repo_root/dist/DINPUT.dll" "$release_root/payload/DINPUT.dll"
cp -- "$repo_root/config/deathtrap_native.ini" "$release_root/payload/deathtrap_native.ini"
cp -- "$repo_root/config/keys.cfg" "$release_root/payload/keys.cfg"
cp -- "$repo_root/config/dxwrapper-dgvoodoo.ini" "$release_root/payload/dxwrapper.ini"
cp -- "$repo_root/config/dgVoodoo-recommended.conf" "$release_root/payload/dgVoodoo.conf"
cp -- "$repo_root/third_party/deathtrap-dxwrapper-release225/x86/DDraw.dll" \
    "$release_root/payload/dxwrapper/DDraw.dll"
cp -- "$repo_root/third_party/deathtrap-dxwrapper-release225/x86/dxwrapper.dll" \
    "$release_root/payload/dxwrapper/dxwrapper.dll"
cp -- "$repo_root/third_party/dgVoodoo2-2.86.2/x86/D3D9.dll" \
    "$release_root/payload/dgVoodoo/D3D9.dll"
cp -- "$repo_root/third_party/dgVoodoo2-2.86.2/x86/D3DImm.dll" \
    "$release_root/payload/dgVoodoo/D3DImm.dll"

packaged_game="$test_root/packaged/Deathtrap Dungeon"
create_game_fixture "$packaged_game"
bash "$release_root/INSTALL-DECK.sh" "$packaged_game" \
    --width 1280 --height 800 --skip-game-hash-check \
    --skip-proton-setup >/dev/null
verify_install "$packaged_game" 1280 800 1

in_place_game="$test_root/in-place/Deathtrap Dungeon"
create_game_fixture "$in_place_game"
cp -a -- "$release_root/." "$in_place_game/"
bash "$in_place_game/INSTALL-DECK.sh" --width 1280 --height 800 \
    --skip-game-hash-check --skip-proton-setup >/dev/null
verify_install "$in_place_game" 1280 800 1

fake_home="$test_root/home"
steam_root="$fake_home/.local/share/Steam"
steamapps="$steam_root/steamapps"
proton_game="$steamapps/common/Deathtrap Dungeon"
compat_data="$steamapps/compatdata/245010"
mkdir -p -- "$compat_data/pfx"
create_game_fixture "$proton_game"
printf 'old fallback launch helper\n' \
    >"$proton_game/deathtrap-native50-deck-launch.sh"
cat >"$steamapps/appmanifest_245010.acf" <<'EOF'
"AppState"
{
    "appid"       "245010"
    "installdir"  "Deathtrap Dungeon"
}
EOF
cat >"$compat_data/pfx/user.reg" <<'EOF'
WINE REGISTRY Version 2

[Software\\Wine\\DllOverrides] 1234567890
#time=1dd000000000000
"ddraw"="old"
"unrelated"="keep"

[Software\\Wine\\Other] 1234567890
#time=1dd000000000000
"preserved"="yes"
EOF
cp -- "$compat_data/pfx/user.reg" "$test_root/original-user.reg"
export HOME="$fake_home"
bash "$installer" --width 1280 --height 800 \
    --skip-game-hash-check >/dev/null
verify_install "$proton_game" 1280 800 0
for override in \
    'dinput|native,builtin' \
    'ddraw|native,builtin' \
    'd3d9|builtin' \
    'd3dim|builtin'; do
    dll="${override%%|*}"
    value="${override#*|}"
    [[ "$(grep -Fxc "\"$dll\"=\"$value\"" \
        "$compat_data/pfx/user.reg")" -eq 1 ]] ||
        fail "incorrect persisted Proton override for $dll"
done
grep -q '^"unrelated"="keep"$' "$compat_data/pfx/user.reg" ||
    fail 'unrelated DLL override was not preserved'
grep -q '^"preserved"="yes"$' "$compat_data/pfx/user.reg" ||
    fail 'unrelated registry section was not preserved'
latest_backup="$(find "$proton_game/back" -mindepth 1 -maxdepth 1 -type d \
    -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
cmp -s "$test_root/original-user.reg" "$latest_backup/proton-user.reg" ||
    fail 'original Proton registry backup does not match'
grep -q '^old fallback launch helper$' \
    "$latest_backup/deathtrap-native50-deck-launch.sh"

printf 'Steam Deck installer tests passed.\n'
