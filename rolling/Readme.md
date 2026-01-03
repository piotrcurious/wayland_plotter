# Rolling Plotter — Readme

This component implements a real-time plotting application that reads CSV data from a serial port and displays it using Wayland and Cairo. The main feature is a configurable rolling buffer that keeps memory usage bounded by discarding the oldest points when the buffer reaches its maximum size.

Below is a clearer description of features, command-line options, usage examples, and troubleshooting/testing tips.

## Features / Key Improvements

### Command-line options
- `-p`, `--port` — Serial port device (e.g. `/dev/ttyUSB0`)
- `-b`, `--baud` — Baud rate. Supported examples: `9600`, `19200`, `38400`, `57600`, `115200`
- `-s`, `--buffer-size` — Maximum CSV buffer size (rolling buffer limit)
- `-W`, `--width` — Graph width in pixels
- `-H`, `--height` — Graph height in pixels
- `-m`, `--margin` — Graph margin in pixels
- `-h`, `--help` — Display help message

Example: `./graph -p /dev/ttyUSB0 -b 115200 -s 500`

### Rolling buffer implementation
- When the buffer reaches its configured maximum size, the oldest entries are automatically removed to make room for new data.
- The implementation uses an efficient left-shift (memmove) to maintain contiguous memory.
- The buffer size is configurable; valid ranges typically cover from 10 up to 100,000 points (configurable in code or via command line).
- A visual indicator `(ROLLING)` appears in the display when the buffer is full and rolling is active.
- The program emits periodic notifications (e.g., every 100 removals) so you can monitor rolling activity without flooding logs.

### Enhanced display features
- Real-time buffer status is shown in the UI: `Points: X / Y (ROLLING)` when full.
- Axis labels show minimum and maximum values for both X and Y axes to help interpretation.
- While waiting for data, buffer and status information are displayed so you know the program is ready.
- On startup a short configuration summary is printed so you can verify settings (port, baud, buffer size, window size, etc.).

### Memory and resource management
- Memory is allocated only up to the configured maximum buffer size.
- Rolling buffer behaviour keeps memory usage effectively constant regardless of run time.
- Proper cleanup is performed on exit (no memory leaks, resources freed).

## Usage

Compile:
```sh
gcc -o graph graph.c -lpthread -lwayland-client -lcairo -lrt
```

Run with default settings:
```sh
./graph
```

Custom serial port and buffer size:
```sh
./graph -p /dev/ttyUSB0 -b 115200 -s 500
```

Large display with large buffer:
```sh
./graph --width 1920 --height 1080 --buffer-size 5000
```

Show help:
```sh
./graph --help
```

## Testing without a serial port

If you don't have a serial device available, you can generate test CSV data from the shell and pipe it to the program (or to a temporary pseudo-tty). Example generator that emits two random fields separated by a comma:

```sh
while true; do
  echo "$RANDOM,$RANDOM"
  sleep 0.1
done
```

You can redirect this output into the program depending on your setup, or use tools like `socat` to create a virtual serial device.

## Notes and recommendations
- Choose a buffer size appropriate to your memory and latency needs. Smaller buffers reduce memory and redrawing cost; larger buffers keep more history available.
- The `(ROLLING)` indicator helps you spot when data retention is being sacrificed for new data.
- If you need persistent storage, add an option to dump or stream incoming data to disk before it is removed from the in-memory buffer.

## Troubleshooting
- If no data appears, verify serial port permissions and baud rate.
- Ensure the serial device is not already opened by another process.
- If graphics fail to initialize, check Wayland and Cairo library availability and linked versions.

The rolling buffer ensures memory usage stays bounded regardless of how long the program runs — making this suitable for long-lived data collection and monitoring tasks.
