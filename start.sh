#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DIALOG="zenity"
if ! command -v zenity >/dev/null 2>&1; then
    if command -v kdialog >/dev/null 2>&1; then
        DIALOG="kdialog"
    else
        echo "Error: zenity or kdialog is required to run Rostam Extractor."
        exit 1
    fi
fi

show_info() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --info --title="$1" --text="$2"
    else
        kdialog --title "$1" --msgbox "$2"
    fi
}

show_error() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --error --title="$1" --text="$2"
    else
        kdialog --title "$1" --error "$2"
    fi
}

select_file() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --file-selection --title="$1"
    else
        kdialog --title "$1" --getopenfilename "$DIR"
    fi
}

select_directory() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --file-selection --directory --title="$1"
    else
        kdialog --title "$1" --getexistingdirectory "$DIR"
    fi
}

run_progress() {
    if [ "$DIALOG" = "zenity" ]; then
        zenity --progress --title="Rostam Extractor" --text="Starting extraction..." --percentage=0 --auto-close --auto-kill
    else
        kdialog --title "Rostam Extractor" --progressbar "Starting extraction..." 100
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
        show_error "Missing g++" "The extraction engine must be compiled first. Please install g++ and run this again."
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

if "$DIR/rostam-core" "$INPUT_FILE" "$OUTPUT_DIR" | run_progress; then
    show_info "Success" "Extraction Complete!"
    xdg-open "$OUTPUT_DIR" >/dev/null 2>&1 &
else
    show_error "Extraction Failed" "The extraction did not complete successfully."
    exit 1
fi
