#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DIALOG="terminal"
if command -v zenity >/dev/null 2>&1; then
    DIALOG="zenity"
elif command -v kdialog >/dev/null 2>&1; then
    DIALOG="kdialog"
fi

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
        "$DIR/rostam-core" "$INPUT_FILE" "$OUTPUT_DIR" | zenity --progress --title="Rostam Extractor" --text="Starting extraction..." --percentage=0 --auto-close --auto-kill
    else
        "$DIR/rostam-core" "$INPUT_FILE" "$OUTPUT_DIR"
    fi
}

if [ ! -x "$DIR/rostam-core" ]; then
    if command -v g++ >/dev/null 2>&1; then
        show_info "Rostam Extractor" "First time setup: Compiling the extraction engine..."
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
