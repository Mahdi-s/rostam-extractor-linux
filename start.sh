#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DIALOG="terminal"
if command -v zenity >/dev/null 2>&1; then
    DIALOG="zenity"
elif command -v kdialog >/dev/null 2>&1; then
    DIALOG="kdialog"
fi

print_usage() {
    echo "Usage: $0 [--pid PID]"
    echo "       $0 [-p PID]"
}

show_info() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --info --title="$1" --text="$2"
    elif [ "$DIALOG" = "kdialog" ]; then
        kdialog --title "$1" --msgbox "$2"
    else
        echo
        echo "== $1 =="
        echo "$2"
        echo
    fi
}

show_error() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --error --title="$1" --text="$2"
    elif [ "$DIALOG" = "kdialog" ]; then
        kdialog --title "$1" --error "$2"
    else
        echo
        echo "ERROR: $1"
        echo "$2"
        echo
    fi
}

select_file() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --file-selection --title="$1"
    elif [ "$DIALOG" = "kdialog" ]; then
        kdialog --title "$1" --getopenfilename "$DIR"
    else
        echo "$1" >&2
        printf "Path to input .ts file: " >&2
        read -r REPLY
        echo "$REPLY"
    fi
}

select_directory() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --file-selection --directory --title="$1"
    elif [ "$DIALOG" = "kdialog" ]; then
        kdialog --title "$1" --getexistingdirectory "$DIR"
    else
        echo "$1" >&2
        printf "Folder for extracted files: " >&2
        read -r REPLY
        echo "$REPLY"
    fi
}

run_extractor() {
    if [ "$DIALOG" = "zenity" ]; then
        "$DIR/rostam-core" "${PID_ARGS[@]}" "$INPUT_FILE" "$OUTPUT_DIR" | zenity --progress --title="Rostam Extractor" --text="Starting extraction..." --percentage=0 --auto-close --auto-kill
    else
        "$DIR/rostam-core" "${PID_ARGS[@]}" "$INPUT_FILE" "$OUTPUT_DIR"
    fi
}

PID_ARGS=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --pid|-p)
            if [ -z "$2" ]; then
                show_error "Invalid PID" "Missing PID value."
                print_usage
                exit 1
            fi
            PID_ARGS=("$1" "$2")
            shift 2
            ;;
        --pid=*)
            PID_ARGS=("$1")
            shift
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        *)
            show_error "Unknown Argument" "Unknown argument: $1"
            print_usage
            exit 1
            ;;
    esac
done

NEEDS_BUILD=0
if [ ! -x "$DIR/rostam-core" ]; then
    NEEDS_BUILD=1
elif [ "$DIR/main.cpp" -nt "$DIR/rostam-core" ]; then
    NEEDS_BUILD=1
elif [ "$DIR/rostam_stream.hpp" -nt "$DIR/rostam-core" ]; then
    NEEDS_BUILD=1
elif [ "$DIR/rostam_utils.hpp" -nt "$DIR/rostam-core" ]; then
    NEEDS_BUILD=1
elif ! "$DIR/rostam-core" --help 2>&1 | grep -q -- "--pid"; then
    NEEDS_BUILD=1
fi

if [ "$NEEDS_BUILD" = "1" ]; then
    if command -v g++ >/dev/null 2>&1; then
        show_info "Rostam Extractor" "Compiling the extraction engine..."
        if ! g++ -O3 -std=c++17 "$DIR/main.cpp" -o "$DIR/rostam-core"; then
            show_error "Compile Failed" "Could not compile the extraction engine."
            exit 1
        fi
    else
        show_error "Missing Extraction Engine" "rostam-core was not found, and g++ is not available to build it. Put the compiled rostam-core binary next to start.sh and run this again."
        exit 1
    fi
fi

INPUT_FILE=$(select_file "Select the Rostam .ts recording")
if [ -z "$INPUT_FILE" ]; then
    exit 0
fi

OUTPUT_DIR=$(select_directory "Select folder to save extracted files")
if [ -z "$OUTPUT_DIR" ]; then
    exit 0
fi

if run_extractor; then
    show_info "Success" "Extraction Complete!"
    if command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$OUTPUT_DIR" >/dev/null 2>&1 &
    fi
else
    show_error "Extraction Failed" "The extraction did not complete successfully."
    exit 1
fi
