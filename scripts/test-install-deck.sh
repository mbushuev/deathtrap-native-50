#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
installer="$repo_root/scripts/install-deck.sh"
test_root="$(mktemp -d)"

cleanup() {
    [[ "$test_root" == /tmp/* && -d "$test_root" ]] && rm -rf -- "$test_root"
}
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
    grep -q '^VibrationOutputGainPercent=300$' \
        "$game/deathtrap_native.ini"
    grep -q '^DdrawClearFlipBackBuffer = 1$' "$game/dxwrapper.ini"
    grep -q '^DdrawUseExternalD3D9 = 1$' "$game/dxwrapper.ini"
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
fake_proton="$steamapps/common/Proton 10.0/proton"
mkdir -p -- "$steamapps/common/Proton 10.0" "$compat_data/pfx"
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
printf '10.0-test\n' >"$compat_data/version"
printf 'existing registry\n' >"$compat_data/pfx/user.reg"
cat >"$fake_proton" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
printf '%s|%s|%s|%s|%s\n' \
    "$STEAM_COMPAT_CLIENT_INSTALL_PATH" \
    "$STEAM_COMPAT_DATA_PATH" \
    "$STEAM_COMPAT_INSTALL_PATH" \
    "$STEAM_COMPAT_LIBRARY_PATHS" \
    "$*" >>"$FAKE_PROTON_LOG"
value_name=''
value_data=''
while (($#)); do
    case "$1" in
        /v)
            value_name="$2"
            shift 2
            ;;
        /d)
            value_data="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done
[[ -n "$value_name" && -n "$value_data" ]]
printf '"%s"="%s"\n' "$value_name" "$value_data" \
    >>"$STEAM_COMPAT_DATA_PATH/pfx/user.reg"
EOF
chmod 755 -- "$fake_proton"
export HOME="$fake_home"
export FAKE_PROTON_LOG="$test_root/proton.log"
bash "$installer" --width 1280 --height 800 \
    --skip-game-hash-check >/dev/null
verify_install "$proton_game" 1280 800 0
[[ "$(wc -l <"$FAKE_PROTON_LOG")" -eq 4 ]] ||
    fail 'expected four Proton registry commands'
for dll in dinput ddraw; do
    grep -q "reg.exe add .* /v $dll .* /d native,builtin /f" \
        "$FAKE_PROTON_LOG" || fail "missing Proton override for $dll"
done
for dll in d3d9 d3dim; do
    grep -q "reg.exe add .* /v $dll .* /d builtin /f" \
        "$FAKE_PROTON_LOG" || fail "missing Proton builtin override for $dll"
done
grep -q "^$steam_root|$compat_data|$proton_game|$steamapps|" \
    "$FAKE_PROTON_LOG" ||
    fail 'incorrect Steam or Proton environment'
latest_backup="$(find "$proton_game/back" -mindepth 1 -maxdepth 1 -type d \
    -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
grep -q '^existing registry$' "$latest_backup/proton-user.reg"
grep -q '^old fallback launch helper$' \
    "$latest_backup/deathtrap-native50-deck-launch.sh"

printf 'Steam Deck installer tests passed.\n'
