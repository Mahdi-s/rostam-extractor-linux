#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

print_usage() {
    echo "Usage: $0 [folder] [--port PORT]"
    echo
    echo "Example:"
    echo "  $0 data/"
    echo "  $0 data/ --port 8080"
}

show_error() {
    echo
    echo "ERROR: $1"
    echo "$2"
    echo
}

select_directory() {
    if [ -n "$1" ]; then
        echo "$1"
        return
    fi

    echo "Folder to share with nearby devices:"
    printf "Path to extracted files folder: "
    read -r REPLY
    echo "$REPLY"
}

PORT_ARGS=()
FOLDER=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port|-p)
            if [ -z "$2" ]; then
                show_error "Invalid Port" "Missing port value."
                print_usage
                exit 1
            fi
            PORT_ARGS=("$1" "$2")
            shift 2
            ;;
        --port=*)
            PORT_ARGS=("$1")
            shift
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        *)
            if [ -z "$FOLDER" ]; then
                FOLDER="$1"
                shift
            else
                show_error "Unknown Argument" "Unknown argument: $1"
                print_usage
                exit 1
            fi
            ;;
    esac
done

NEEDS_BUILD=0
if [ ! -x "$DIR/rostam-share" ]; then
    NEEDS_BUILD=1
elif [ "$DIR/rostam_share.cpp" -nt "$DIR/rostam-share" ]; then
    NEEDS_BUILD=1
fi

if [ "$NEEDS_BUILD" = "1" ]; then
    if command -v g++ >/dev/null 2>&1; then
        echo "Building Wi-Fi sharing helper..."
        if ! g++ -O2 -std=c++17 "$DIR/rostam_share.cpp" -o "$DIR/rostam-share"; then
            show_error "Build Failed" "Could not compile the Wi-Fi sharing helper."
            exit 1
        fi
    else
        show_error "Missing Sharing Helper" "rostam-share was not found, and g++ is not available to build it. Put the compiled Linux rostam-share binary next to share.sh and run this again."
        exit 1
    fi
fi

FOLDER=$(select_directory "$FOLDER")
if [ -z "$FOLDER" ]; then
    exit 0
fi

if [ ! -d "$FOLDER" ]; then
    show_error "Folder Not Found" "The folder does not exist: $FOLDER"
    exit 1
fi

"$DIR/rostam-share" "$FOLDER" "${PORT_ARGS[@]}"
