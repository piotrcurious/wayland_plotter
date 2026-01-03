Key Improvements:
1. Command Line Options
-p, --port: Serial port device
-b, --baud: Baud rate (9600, 19200, 38400, 57600, 115200)
-s, --buffer-size: Maximum CSV buffer size (rolling buffer limit)
-W, --width: Graph width in pixels
-H, --height: Graph height in pixels
-m, --margin: Graph margin in pixels
-h, --help: Display help message
2. Rolling Buffer Implementation
When buffer reaches max size, oldest entries are automatically removed
Uses efficient memmove() to shift data left
Configurable buffer size (10 to 100,000 points)
Visual indicator shows "(ROLLING)" when buffer is full
Periodic notifications (every 100 removals) to track rolling activity
3. Enhanced Display Features
Real-time buffer status: "Points: X / Y (ROLLING)"
Axis labels showing min/max values for X and Y
Buffer size information displayed while waiting for data
Startup configuration summary
4. Better Memory Management
Only allocates memory up to max buffer size
No memory leaks with rolling buffer
Proper cleanup of all resources
Usage Examples:
# Compile
gcc -o graph graph.c -lpthread -lwayland-client -lcairo -lrt

# Default settings
./graph

# Custom serial port and buffer size
./graph -p /dev/ttyUSB0 -b 115200 -s 500

# Large display with big buffer
./graph --width 1920 --height 1080 --buffer-size 5000

# Show help
./graph --help
Testing Without Serial Port:
Create a test data generator:
# Generate test CSV data
while true; do 
  echo "$RANDOM,$RANDOM"; 
  sleep 0.1; 
done
The rolling buffer ensures memory usage stays constant regardless of how long the program runs!
