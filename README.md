# Rostam Extractor Linux

Minimal Linux tools for extracting embedded files from MPEG-TS satellite recordings and sharing the extracted folder over local Wi-Fi.

## Extract Files

Run the core extractor directly:

```bash
./rostam-core [--pid PID] <input.ts> <output_dir>
```

Example:

```bash
./rostam-core --pid 258 /scratch1/mahdisae/rostam/pid258.ts data/
```

If `--pid` is not provided, the default PID is `6530`.

Extractor flags:

```text
--pid PID      MPEG-TS PID to extract, from 0 to 8191
-p PID         Short form of --pid
--pid=PID      Inline form of --pid
--help, -h     Show usage
```

## Guided Start Script

Use the wrapper if you want prompts:

```bash
./start.sh [--pid PID]
```

The script uses `zenity` or `kdialog` if already installed. If neither exists, it falls back to terminal prompts. It also builds `rostam-core` from source if needed and `g++` is available.

Start script flags:

```text
--pid PID      Pass PID to rostam-core
-p PID         Short form of --pid
--pid=PID      Inline form of --pid
--help, -h     Show usage
```

## Share Extracted Files Over Wi-Fi

After extraction, share the output folder with phones or computers on the same Wi-Fi:

```bash
./share.sh <folder> [--port PORT]
```

Example:

```bash
./share.sh data/
```

The sharing tool starts a read-only local web server, prints a QR code, and shows URLs like `http://192.168.1.20:8080/`. Scan the QR code or open the URL from another device on the same Wi-Fi. Press `Ctrl+C` to stop sharing.

Sharing flags:

```text
--port PORT    HTTP port to serve on, default 8080
-p PORT        Short form of --port
--port=PORT    Inline form of --port
--help, -h     Show usage
```

## Build Manually

If binaries are not included:

```bash
g++ -O3 -std=c++17 main.cpp -o rostam-core
g++ -O2 -std=c++17 rostam_share.cpp -o rostam-share
chmod +x start.sh share.sh rostam-core rostam-share
```

No Python, Node, FFmpeg, Qt, Electron, Boost, or web server package is required.
