#!/usr/bin/env bash
set -Eeuo pipefail

readonly APP_ID=245010
readonly EXPECTED_DUNGEON_SHA256=95fe9ce0fff387f00704548f152e4340815213fcb3833dbe1b5c42871e7d2e56
readonly EXPECTED_EXE_SHA256=0c644a00e62652e046c5dad2960f0f6c8c1998f4ca065780fbd7811d9908bf1f

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
game_dir=""
compat_data=""
proton_command=""
display_width=""
display_height=""
skip_game_hash_check=0
skip_proton_setup=0

usage() {
    cat <<'EOF'
Usage: bash INSTALL-DECK.sh [options]

Options:
  --game-dir PATH             Directory containing DD_CD.EXE
  --compat-data PATH          Steam compatdata/245010 directory
  --proton PATH               Proton executable selected for the game
  --width PIXELS              Override detected display width
  --height PIXELS             Override detected display height
  --skip-game-hash-check      Do not warn about untested game binaries
  --skip-proton-setup         Install files without configuring Proton DLLs
  -h, --help                  Show this help

Run this script from an extracted release. Launch the game through Steam once
before installing so that App ID 245010 has a Proton prefix.
EOF
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

while (($#)); do
    case "$1" in
        --game-dir)
            (($# >= 2)) || die '--game-dir requires a path.'
            game_dir="$2"
            shift 2
            ;;
        --compat-data)
            (($# >= 2)) || die '--compat-data requires a path.'
            compat_data="$2"
            shift 2
            ;;
        --proton)
            (($# >= 2)) || die '--proton requires a path.'
            proton_command="$2"
            shift 2
            ;;
        --width)
            (($# >= 2)) || die '--width requires a value.'
            display_width="$2"
            shift 2
            ;;
        --height)
            (($# >= 2)) || die '--height requires a value.'
            display_height="$2"
            shift 2
            ;;
        --skip-game-hash-check)
            skip_game_hash_check=1
            shift
            ;;
        --skip-proton-setup)
            skip_proton_setup=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

if [[ -n "$display_width" || -n "$display_height" ]]; then
    [[ "$display_width" =~ ^[0-9]+$ && "$display_height" =~ ^[0-9]+$ ]] ||
        die '--width and --height must be positive integers supplied together.'
    ((display_width >= 640 && display_height >= 480)) ||
        die 'Display dimensions must be at least 640x480.'
fi

declare -a steamapps_roots=()

append_steamapps_root() {
    local candidate canonical existing
    candidate="$1"
    [[ -d "$candidate" ]] || return 0
    canonical="$(readlink -f -- "$candidate")"
    for existing in "${steamapps_roots[@]-}"; do
        [[ "$existing" == "$canonical" ]] && return 0
    done
    steamapps_roots+=("$canonical")
}

collect_steamapps_roots() {
    local vdf library candidate
    append_steamapps_root "$HOME/.steam/root/steamapps"
    append_steamapps_root "$HOME/.local/share/Steam/steamapps"

    for vdf in \
        "$HOME/.steam/root/steamapps/libraryfolders.vdf" \
        "$HOME/.local/share/Steam/steamapps/libraryfolders.vdf"; do
        [[ -f "$vdf" ]] || continue
        while IFS= read -r library; do
            library="${library//\\\\/\\}"
            append_steamapps_root "$library/steamapps"
        done < <(awk -F '"' '/"path"/ { print $4 }' "$vdf")
    done

    if [[ -d /run/media/deck ]]; then
        while IFS= read -r candidate; do
            append_steamapps_root "$candidate"
        done < <(find /run/media/deck -mindepth 2 -maxdepth 3 -type d \
            -name steamapps -print 2>/dev/null)
    fi
}

collect_steamapps_roots

find_game_directory() {
    local steamapps manifest install_dir candidate canonical existing duplicate
    local -a matches=()

    for steamapps in "${steamapps_roots[@]-}"; do
        manifest="$steamapps/appmanifest_${APP_ID}.acf"
        [[ -f "$manifest" ]] || continue
        install_dir="$(awk -F '"' '/"installdir"/ { print $4; exit }' "$manifest")"
        [[ -n "$install_dir" ]] || continue
        candidate="$steamapps/common/$install_dir"
        [[ -d "$candidate" ]] || continue
        if ! find "$candidate" -mindepth 1 -maxdepth 1 -type f \
                -iname DD_CD.EXE -print -quit 2>/dev/null | grep -q .; then
            continue
        fi
        canonical="$(readlink -f -- "$candidate")"
        duplicate=0
        for existing in "${matches[@]-}"; do
            [[ "$existing" == "$canonical" ]] && duplicate=1
        done
        ((duplicate)) || matches+=("$canonical")
    done

    if ((${#matches[@]} == 0)); then
        die 'Deathtrap Dungeon was not found. Pass --game-dir PATH.'
    fi
    ((${#matches[@]} == 1)) ||
        die 'More than one installation was found. Pass --game-dir PATH.'
    printf '%s\n' "${matches[0]}"
}

if [[ -z "$game_dir" ]]; then
    game_dir="$(find_game_directory)"
fi
game_dir="$(readlink -f -- "$game_dir")"
[[ -d "$game_dir" ]] || die "Game directory does not exist: $game_dir"

find_required_file() {
    local directory="$1" name="$2" result
    result="$(find "$directory" -mindepth 1 -maxdepth 1 -type f \
        -iname "$name" -print -quit 2>/dev/null)"
    [[ -n "$result" ]] || die "Required game file was not found: $directory/$name"
    printf '%s\n' "$result"
}

game_exe="$(find_required_file "$game_dir" DD_CD.EXE)"
dungeon_dll="$(find_required_file "$game_dir" Dungeon.dll)"
keys_destination="$(find_required_file "$game_dir/ASYLUM" keys.cfg)"
retail_config="$(find_required_file "$game_dir/ASYLUM" config.dat)"

if pgrep -af '[D]D_CD.EXE' >/dev/null 2>&1; then
    die 'Deathtrap Dungeon is running. Close it before installing.'
fi

if [[ -d "$script_dir/payload" ]]; then
    payload_root="$script_dir/payload"
    native_dll="$payload_root/DINPUT.dll"
    native_ini="$payload_root/deathtrap_native.ini"
    keys_source="$payload_root/keys.cfg"
    dxwrapper_dir="$payload_root/dxwrapper"
    dxwrapper_ini="$payload_root/dxwrapper.ini"
else
    repo_root="$(cd -- "$script_dir/.." && pwd -P)"
    native_dll="$repo_root/dist/DINPUT.dll"
    native_ini="$repo_root/config/deathtrap_native.ini"
    keys_source="$repo_root/config/keys.cfg"
    dxwrapper_dir="$repo_root/third_party/deathtrap-dxwrapper-release225/x86"
    dxwrapper_ini="$repo_root/config/dxwrapper-dgvoodoo.ini"
fi

declare -A source_files=(
    [DINPUT.dll]="$native_dll"
    [deathtrap_native.ini]="$native_ini"
    [DDraw.dll]="$dxwrapper_dir/DDraw.dll"
    [dxwrapper.dll]="$dxwrapper_dir/dxwrapper.dll"
    [dxwrapper.ini]="$dxwrapper_ini"
)
for destination in "${!source_files[@]}"; do
    [[ -f "${source_files[$destination]}" ]] ||
        die "Installer payload is missing: ${source_files[$destination]}"
done
[[ -f "$keys_source" ]] || die "Installer payload is missing: $keys_source"

verify_payload_hash() {
    local path="$1" expected="$2" actual
    actual="$(sha256sum -- "$path" | awk '{ print $1 }')"
    [[ "$actual" == "$expected" ]] || die "Bundled runtime failed verification: $path"
}

verify_payload_hash "$dxwrapper_dir/DDraw.dll" \
    8bae794eb7506711f57b690cfb8660a5f008b0185e764f8cb56d5972e01a9f33
verify_payload_hash "$dxwrapper_dir/dxwrapper.dll" \
    304c3a528ad1ab4b25bbee48da0371a9db435df1cdf733cd1be13c2ff27bcc8e

grep -Eq '^Dd7to9[[:space:]]*=[[:space:]]*1[[:space:]]*$' "$dxwrapper_ini" ||
    die 'Bundled dxwrapper configuration does not enable Dd7to9.'
grep -Eq '^DdrawUseExternalD3D9[[:space:]]*=[[:space:]]*1[[:space:]]*$' "$dxwrapper_ini" ||
    die 'Bundled dxwrapper configuration does not enable external D3D9.'
grep -Eq '^DdrawClearFlipBackBuffer[[:space:]]*=[[:space:]]*1[[:space:]]*$' "$dxwrapper_ini" ||
    die 'Bundled dxwrapper configuration does not contain the tested Windows clear-on-flip setting.'

if ((skip_game_hash_check == 0)); then
    actual_exe="$(sha256sum -- "$game_exe" | awk '{ print $1 }')"
    actual_dungeon="$(sha256sum -- "$dungeon_dll" | awk '{ print $1 }')"
    [[ "$actual_exe" == "$EXPECTED_EXE_SHA256" ]] ||
        printf 'Warning: DD_CD.EXE is not the tested Steam build (%s).\n' "$actual_exe" >&2
    [[ "$actual_dungeon" == "$EXPECTED_DUNGEON_SHA256" ]] ||
        printf 'Warning: Dungeon.dll is not the tested Steam build (%s).\n' "$actual_dungeon" >&2
fi

detect_display_size() {
    local mode width height modes_file
    for modes_file in /sys/class/drm/*-eDP-*/modes; do
        [[ -f "$modes_file" ]] || continue
        mode="$(head -n 1 "$modes_file")"
        if [[ "$mode" =~ ^([0-9]+)x([0-9]+)$ ]]; then
            width="${BASH_REMATCH[1]}"
            height="${BASH_REMATCH[2]}"
            if ((height > width)); then
                printf '%s %s\n' "$height" "$width"
            else
                printf '%s %s\n' "$width" "$height"
            fi
            return
        fi
    done
    printf '1280 800\n'
}

if [[ -z "$display_width" ]]; then
    read -r display_width display_height < <(detect_display_size)
fi

stamp="$(date +%Y%m%d-%H%M%S-%3N)"
backup_dir="$game_dir/back/deathtrap-native50-steamdeck-$stamp"
mkdir -p -- "$backup_dir"

backup_if_present() {
    local relative="$1" source backup_name
    source="$game_dir/$relative"
    [[ -e "$source" ]] || return 0
    backup_name="${relative//\//_}"
    cp -a -- "$source" "$backup_dir/$backup_name"
}

for existing in \
    DINPUT.dll deathtrap_native.ini DDraw.dll dxwrapper.dll dxwrapper.ini \
    D3D9.dll D3DImm.dll dgVoodoo.conf deathtrap-native50-deck-launch.sh; do
    backup_if_present "$existing"
done
cp -a -- "$keys_destination" "$backup_dir/ASYLUM_keys.cfg"
cp -a -- "$retail_config" "$backup_dir/ASYLUM_config.dat"

mkdir -p -- "$game_dir/logs"
for legacy_log in deathtrap_native_render.log deathtrap_native_present.log; do
    if [[ -f "$game_dir/$legacy_log" ]]; then
        mv -- "$game_dir/$legacy_log" \
            "$game_dir/logs/${legacy_log%.log}-legacy-$stamp.log"
    fi
done

for destination in "${!source_files[@]}"; do
    cp -- "${source_files[$destination]}" "$game_dir/$destination"
done

remove_local_final_backend() {
    local expected_name="$1" candidate backup_name
    while IFS= read -r candidate; do
        backup_name="removed_$(basename -- "$candidate")"
        cp -a -- "$candidate" "$backup_dir/$backup_name"
        rm -- "$candidate"
    done < <(find "$game_dir" -mindepth 1 -maxdepth 1 -type f \
        -iname "$expected_name" -print 2>/dev/null)
}

# dgVoodoo is the tested final D3D9 backend on Windows, but its extra
# D3D9 -> D3D11 translation hangs or flashes under Proton/DXVK. Keep the
# Deathtrap Dd7to9 layer and hand its D3D9 output directly to Proton instead.
remove_local_final_backend D3D9.dll
remove_local_final_backend D3DImm.dll
remove_local_final_backend dgVoodoo.conf

rewrite_ini_display() {
    local source="$1" width="$2" height="$3" temporary
    temporary="$(mktemp)"
    awk -v width="$width" -v height="$height" '
        { sub(/\r$/, "") }
        /^[[:space:]]*WindowWidth[[:space:]]*=/ {
            sub(/[0-9]+[[:space:]]*$/, width); width_count++
        }
        /^[[:space:]]*WindowHeight[[:space:]]*=/ {
            sub(/[0-9]+[[:space:]]*$/, height); height_count++
        }
        { lines[NR] = $0 }
        END {
            if (width_count != 1 || height_count != 1) exit 42
            for (i = 1; i <= NR; i++) printf "%s\r\n", lines[i]
        }
    ' "$source" >"$temporary" || {
        rm -f -- "$temporary"
        die 'Display settings were not found exactly once in deathtrap_native.ini.'
    }
    mv -- "$temporary" "$source"
}

rewrite_retail_config() {
    local source="$1" split_file rewritten
    split_file="$(mktemp)"
    rewritten="$(mktemp)"
    sed -E \
        -e 's/([^[:space:]])(RENDERING_PLATFORM[[:space:]])/\1\n\2/g' \
        -e 's/([^[:space:]])(D3D_ALLOW_MIPMAP[[:space:]])/\1\n\2/g' \
        -e 's/([^[:space:]])(D3D_ALLOW_PALETTISED[[:space:]])/\1\n\2/g' \
        -e 's/([^[:space:]])(D3D_TYPE1_SHADOWS[[:space:]])/\1\n\2/g' \
        "$source" >"$split_file"
    awk '
        BEGIN {
            values["RENDERING_PLATFORM"] = "13"
            values["D3D_ALLOW_MIPMAP"] = "1"
            values["D3D_ALLOW_PALETTISED"] = "0"
            values["D3D_TYPE1_SHADOWS"] = "1"
            order[1] = "RENDERING_PLATFORM"
            order[2] = "D3D_ALLOW_MIPMAP"
            order[3] = "D3D_ALLOW_PALETTISED"
            order[4] = "D3D_TYPE1_SHADOWS"
        }
        { sub(/\r$/, "") }
        {
            for (i = 1; i <= 4; i++) {
                name = order[i]
                if ($0 ~ "^[[:space:]]*" name "([[:space:]]+.*)?[[:space:]]*$") {
                    if (!seen[name]++) lines[++count] = name " " values[name]
                    next
                }
            }
            lines[++count] = $0
        }
        END {
            for (i = 1; i <= 4; i++) {
                name = order[i]
                if (!seen[name]) lines[++count] = name " " values[name]
            }
            for (i = 1; i <= count; i++) printf "%s\r\n", lines[i]
        }
    ' "$split_file" >"$rewritten"
    mv -- "$rewritten" "$source"
    rm -f -- "$split_file"
}

rewrite_ini_display "$game_dir/deathtrap_native.ini" \
    "$display_width" "$display_height"
awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' \
    "$keys_source" >"$keys_destination"
rewrite_retail_config "$retail_config"

cat >"$game_dir/deathtrap-native50-deck-launch.sh" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
export WINEDLLOVERRIDES="dinput,ddraw=n,b;d3d9,d3dim=b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
exec "$@"
EOF
chmod 755 -- "$game_dir/deathtrap-native50-deck-launch.sh"

find_compat_data() {
    local steamapps candidate
    steamapps="$(dirname -- "$(dirname -- "$game_dir")")"
    candidate="$steamapps/compatdata/$APP_ID"
    if [[ -d "$candidate/pfx" ]]; then
        printf '%s\n' "$(readlink -f -- "$candidate")"
        return
    fi
    for steamapps in "${steamapps_roots[@]-}"; do
        candidate="$steamapps/compatdata/$APP_ID"
        if [[ -d "$candidate/pfx" ]]; then
            printf '%s\n' "$(readlink -f -- "$candidate")"
            return
        fi
    done
    return 1
}

find_steam_client_root() {
    local candidate
    for candidate in "$HOME/.steam/root" "$HOME/.local/share/Steam"; do
        [[ -d "$candidate/steamapps" ]] || continue
        readlink -f -- "$candidate"
        return
    done
    return 1
}

find_proton() {
    local prefix="$1" major steamapps steam_root candidate
    major="$(grep -oE '[0-9]+' "$prefix/version" 2>/dev/null | head -n 1 || true)"
    steamapps="$(dirname -- "$(dirname -- "$prefix")")"
    local -a candidates=()
    [[ -n "$major" ]] && candidates+=("$steamapps/common/Proton $major.0/proton")
    candidates+=(
        "$steamapps/common/Proton - Experimental/proton"
        "$steamapps/common/Proton 11.0/proton"
        "$steamapps/common/Proton 10.0/proton"
        "$steamapps/common/Proton 9.0 (Beta)/proton"
    )
    if steam_root="$(find_steam_client_root 2>/dev/null)"; then
        [[ -n "$major" ]] && candidates+=("$steam_root/steamapps/common/Proton $major.0/proton")
        candidates+=(
            "$steam_root/steamapps/common/Proton - Experimental/proton"
            "$steam_root/steamapps/common/Proton 11.0/proton"
            "$steam_root/steamapps/common/Proton 10.0/proton"
            "$steam_root/steamapps/common/Proton 9.0 (Beta)/proton"
        )
        while IFS= read -r candidate; do
            candidates+=("$candidate")
        done < <(find "$steam_root/compatibilitytools.d" -mindepth 2 -maxdepth 2 \
            -type f -name proton -print 2>/dev/null || true)
    fi
    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done
    return 1
}

proton_configured=0
if ((skip_proton_setup == 0)); then
    if [[ -n "$compat_data" ]]; then
        compat_data="$(readlink -f -- "$compat_data")"
        [[ -d "$compat_data/pfx" ]] || die "Invalid Proton compatdata directory: $compat_data"
    else
        compat_data="$(find_compat_data)" ||
            die 'The Proton prefix is missing. Launch the game once and run the installer again.'
    fi
    if [[ -n "$proton_command" ]]; then
        proton_command="$(readlink -f -- "$proton_command")"
        [[ -x "$proton_command" ]] || die "Proton executable is not runnable: $proton_command"
    else
        proton_command="$(find_proton "$compat_data")" ||
            die 'A Steam-managed Proton installation was not found. Pass --proton PATH.'
    fi
    steam_client_root="$(find_steam_client_root)" ||
        die 'The Steam client directory was not found.'
    steamapps="$(dirname -- "$(dirname -- "$compat_data")")"
    library_root="$(dirname -- "$steamapps")"
    if [[ -f "$compat_data/pfx/user.reg" ]]; then
        cp -a -- "$compat_data/pfx/user.reg" "$backup_dir/proton-user.reg"
    fi
    for dll in dinput ddraw; do
        STEAM_COMPAT_CLIENT_INSTALL_PATH="$steam_client_root" \
        STEAM_COMPAT_DATA_PATH="$compat_data" \
        STEAM_COMPAT_INSTALL_PATH="$game_dir" \
        STEAM_COMPAT_LIBRARY_PATHS="$library_root" \
        STEAM_COMPAT_APP_ID="$APP_ID" \
        SteamAppId="$APP_ID" \
        SteamGameId="$APP_ID" \
            "$proton_command" run reg.exe add 'HKCU\Software\Wine\DllOverrides' \
                /v "$dll" /t REG_SZ /d native,builtin /f >/dev/null
    done
    for dll in d3d9 d3dim; do
        STEAM_COMPAT_CLIENT_INSTALL_PATH="$steam_client_root" \
        STEAM_COMPAT_DATA_PATH="$compat_data" \
        STEAM_COMPAT_INSTALL_PATH="$game_dir" \
        STEAM_COMPAT_LIBRARY_PATHS="$library_root" \
        STEAM_COMPAT_APP_ID="$APP_ID" \
        SteamAppId="$APP_ID" \
        SteamGameId="$APP_ID" \
            "$proton_command" run reg.exe add 'HKCU\Software\Wine\DllOverrides' \
                /v "$dll" /t REG_SZ /d builtin /f >/dev/null
    done
    proton_configured=1
fi

printf 'Installed Deathtrap Native 50 into: %s\n' "$game_dir"
printf 'Configured display size: %sx%s\n' "$display_width" "$display_height"
printf 'Rollback copy: %s\n' "$backup_dir"
if ((proton_configured)); then
    printf 'Configured Proton to load Native 50 and Dd7to9, with D3D9 rendered by Proton/DXVK.\n'
else
    printf '%s\n' 'Proton setup was skipped. Use this Steam Launch Option:'
    printf '%s\n' '"./deathtrap-native50-deck-launch.sh" %command%'
fi
