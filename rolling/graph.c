// A pthread based linux program implementing graphing csv input coming from reading serial port (on separate thread) using Wayland.
// Features: Rolling buffer with configurable size, command line options

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <getopt.h>
#include <wayland-client.h>
#include <cairo/cairo.h>

#define DEFAULT_SERIAL_PORT "/dev/ttyS0"
#define DEFAULT_SERIAL_BAUD B9600
#define SERIAL_BUFFER_SIZE 256
#define DEFAULT_GRAPH_WIDTH 800
#define DEFAULT_GRAPH_HEIGHT 600
#define DEFAULT_GRAPH_MARGIN 50
#define DEFAULT_CSV_BUFFER_SIZE 1000 // Default maximum number of data points

// A struct to store the csv data
typedef struct {
    double x; // the x value
    double y; // the y value
} csv_data_t;

// A global variable to store the csv data array
csv_data_t *csv_data = NULL;

// A global variable to store the csv data size
int csv_data_size = 0;

// A global variable to store the maximum csv buffer size (rolling buffer)
int csv_buffer_max_size = DEFAULT_CSV_BUFFER_SIZE;

// Configuration structure
typedef struct {
    char *serial_port;
    speed_t baud_rate;
    int graph_width;
    int graph_height;
    int graph_margin;
    int csv_buffer_size;
} config_t;

// Global configuration
config_t config = {
    .serial_port = NULL,
    .baud_rate = DEFAULT_SERIAL_BAUD,
    .graph_width = DEFAULT_GRAPH_WIDTH,
    .graph_height = DEFAULT_GRAPH_HEIGHT,
    .graph_margin = DEFAULT_GRAPH_MARGIN,
    .csv_buffer_size = DEFAULT_CSV_BUFFER_SIZE
};

// Mutex for protecting csv_data access
pthread_mutex_t csv_data_mutex = PTHREAD_MUTEX_INITIALIZER;

// A global variable to store the serial port file descriptor
int serial_fd = -1;

// A global variable to store the wayland display
struct wl_display *display = NULL;

// A global variable to store the wayland registry
struct wl_registry *registry = NULL;

// A global variable to store the wayland compositor
struct wl_compositor *compositor = NULL;

// A global variable to store the wayland shell
struct wl_shell *shell = NULL;

// A global variable to store the wayland shm
struct wl_shm *shm = NULL;

// A global variable to store the wayland surface
struct wl_surface *surface = NULL;

// A global variable to store the wayland shell surface
struct wl_shell_surface *shell_surface = NULL;

// A global variable to store the wayland buffer
struct wl_buffer *buffer = NULL;

// Shared memory file descriptor and data
int shm_fd = -1;
void *shm_data = NULL;

// Function to print usage information
void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nOptions:\n");
    printf("  -p, --port PORT          Serial port device (default: %s)\n", DEFAULT_SERIAL_PORT);
    printf("  -b, --baud BAUD          Baud rate: 9600, 19200, 38400, 57600, 115200 (default: 9600)\n");
    printf("  -s, --buffer-size SIZE   Maximum CSV buffer size (default: %d)\n", DEFAULT_CSV_BUFFER_SIZE);
    printf("  -W, --width WIDTH        Graph width in pixels (default: %d)\n", DEFAULT_GRAPH_WIDTH);
    printf("  -H, --height HEIGHT      Graph height in pixels (default: %d)\n", DEFAULT_GRAPH_HEIGHT);
    printf("  -m, --margin MARGIN      Graph margin in pixels (default: %d)\n", DEFAULT_GRAPH_MARGIN);
    printf("  -h, --help               Display this help message\n");
    printf("\nDescription:\n");
    printf("  Reads CSV data (x,y pairs) from a serial port and displays a real-time graph.\n");
    printf("  When buffer size is reached, oldest data points are removed (rolling buffer).\n");
    printf("\nExamples:\n");
    printf("  %s -p /dev/ttyUSB0 -b 115200 -s 500\n", program_name);
    printf("  %s --port /dev/ttyACM0 --buffer-size 2000 --width 1024 --height 768\n", program_name);
}

// Function to parse baud rate string to speed_t
speed_t parse_baud_rate(const char *baud_str) {
    int baud = atoi(baud_str);
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default:
            fprintf(stderr, "Unsupported baud rate: %s, using default 9600\n", baud_str);
            return B9600;
    }
}

// Function to parse command line arguments
int parse_arguments(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"baud", required_argument, 0, 'b'},
        {"buffer-size", required_argument, 0, 's'},
        {"width", required_argument, 0, 'W'},
        {"height", required_argument, 0, 'H'},
        {"margin", required_argument, 0, 'm'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "p:b:s:W:H:m:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                config.serial_port = strdup(optarg);
                break;
            case 'b':
                config.baud_rate = parse_baud_rate(optarg);
                break;
            case 's':
                config.csv_buffer_size = atoi(optarg);
                if (config.csv_buffer_size < 10) {
                    fprintf(stderr, "Buffer size too small, minimum is 10\n");
                    return -1;
                }
                if (config.csv_buffer_size > 100000) {
                    fprintf(stderr, "Buffer size too large, maximum is 100000\n");
                    return -1;
                }
                break;
            case 'W':
                config.graph_width = atoi(optarg);
                if (config.graph_width < 200 || config.graph_width > 4096) {
                    fprintf(stderr, "Width must be between 200 and 4096\n");
                    return -1;
                }
                break;
            case 'H':
                config.graph_height = atoi(optarg);
                if (config.graph_height < 200 || config.graph_height > 4096) {
                    fprintf(stderr, "Height must be between 200 and 4096\n");
                    return -1;
                }
                break;
            case 'm':
                config.graph_margin = atoi(optarg);
                if (config.graph_margin < 10 || config.graph_margin > 200) {
                    fprintf(stderr, "Margin must be between 10 and 200\n");
                    return -1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // Set default serial port if not specified
    if (config.serial_port == NULL) {
        config.serial_port = strdup(DEFAULT_SERIAL_PORT);
    }

    // Update global csv_buffer_max_size
    csv_buffer_max_size = config.csv_buffer_size;

    return 0;
}

// A function to create a shared memory file
int create_shm_file(size_t size) {
    int fd;
    char name[256];
    
    snprintf(name, sizeof(name), "/wl_shm-%d-%d", getpid(), rand());
    
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return -1;
    }
    
    shm_unlink(name);
    
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate");
        close(fd);
        return -1;
    }
    
    return fd;
}

// A function to initialize the serial port
int serial_init() {
    // Open the serial port in read/write mode, non-blocking mode and no controlling terminal mode
    serial_fd = open(config.serial_port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd == -1) {
        perror("open");
        return -1;
    }

    // Get the current attributes of the serial port
    struct termios options;
    if (tcgetattr(serial_fd, &options) == -1) {
        perror("tcgetattr");
        return -1;
    }

    // Set the input and output baud rate
    if (cfsetispeed(&options, config.baud_rate) == -1) {
        perror("cfsetispeed");
        return -1;
    }
    if (cfsetospeed(&options, config.baud_rate) == -1) {
        perror("cfsetospeed");
        return -1;
    }

    // Set the character size to 8 bits, no parity, one stop bit and no hardware flow control
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CRTSCTS;

    // Set the raw input mode, no echo, no signal characters and no extended functions
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

    // Set the raw output mode, no processing of output characters
    options.c_oflag &= ~OPOST;

    // Set the minimum number of characters for non-canonical read and timeout value
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;

    // Apply the new attributes of the serial port
    if (tcsetattr(serial_fd, TCSANOW, &options) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

// A function to read a line from the serial port
int serial_read_line(char *buffer, int size) {
    int index = 0; // the current index of the buffer
    char c; // the current character read from the serial port

    // Loop until the buffer is full or a newline character is read
    while (index < size - 1) {
        // Read one character from the serial port
        if (read(serial_fd, &c, 1) == 1) {
            // If the character is a newline or carriage return, break the loop
            if (c == '\n' || c == '\r') {
                break;
            }
            // Otherwise, store the character in the buffer and increment the index
            buffer[index] = c;
            index++;
        }
    }

    // Terminate the buffer with a null character
    buffer[index] = '\0';

    // Return the number of characters read
    return index;
}

// A function to parse a csv line and store the data in the global array (with rolling buffer)
int csv_parse_line(char *line) {
    // Skip empty lines
    if (strlen(line) == 0) {
        return 0;
    }

    // Allocate memory for a new csv data struct
    csv_data_t data;

    // Scan the line for two double values separated by a comma
    if (sscanf(line, "%lf,%lf", &data.x, &data.y) != 2) {
        fprintf(stderr, "Invalid csv format: %s\n", line);
        return -1;
    }

    // Lock the mutex before modifying csv_data
    pthread_mutex_lock(&csv_data_mutex);

    // Check if buffer is full - implement rolling buffer
    if (csv_data_size >= csv_buffer_max_size) {
        // Remove the oldest entry by shifting all data left
        memmove(&csv_data[0], &csv_data[1], sizeof(csv_data_t) * (csv_data_size - 1));
        // Add new data at the end
        csv_data[csv_data_size - 1] = data;
        // Size remains the same
        
        // Optional: Print rolling buffer notification (only occasionally to avoid spam)
        static int roll_count = 0;
        if (++roll_count % 100 == 0) {
            printf("Rolling buffer: removed oldest entries (total: %d, buffer full)\n", roll_count);
        }
    } else {
        // Buffer not full yet - reallocate and add new data
        csv_data_t *new_data = realloc(csv_data, sizeof(csv_data_t) * (csv_data_size + 1));
        if (new_data == NULL) {
            perror("realloc");
            pthread_mutex_unlock(&csv_data_mutex);
            return -1;
        }
        csv_data = new_data;

        // Store the new data in the global array and increment the size
        csv_data[csv_data_size] = data;
        csv_data_size++;
    }

    // Unlock the mutex
    pthread_mutex_unlock(&csv_data_mutex);

    return 0;
}

// A function to read and parse csv data from the serial port in a separate thread
void *serial_thread(void *arg) {
    // Create a buffer to store the serial line
    char buffer[SERIAL_BUFFER_SIZE];

    // Create a file descriptor set for the select function
    fd_set fds;

    // Create a timeval struct for the select timeout
    struct timeval tv;

    // Loop until the thread is canceled
    while (1) {
        // Clear the file descriptor set
        FD_ZERO(&fds);

        // Add the serial port file descriptor to the set
        FD_SET(serial_fd, &fds);

        // Set the timeout to 1 second
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        // Call the select function to check if the serial port is ready for reading
        int ret = select(serial_fd + 1, &fds, NULL, NULL, &tv);
        if (ret == -1) {
            perror("select");
            break;
        }
        else if (ret == 0) {
            // Timeout, no data available
            continue;
        }
        else {
            // Data available, read a line from the serial port
            if (serial_read_line(buffer, SERIAL_BUFFER_SIZE) > 0) {
                // Parse the line and store the data in the global array
                if (csv_parse_line(buffer) == -1) {
                    fprintf(stderr, "Failed to parse csv line: %s\n", buffer);
                }
            }
        }
    }

    // Return NULL as the thread exit value
    return NULL;
}

// A function to handle the registry global event
void registry_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    // If the interface is wl_compositor, bind it to the global variable
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
    // If the interface is wl_shell, bind it to the global variable
    else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
    }
    // If the interface is wl_shm, bind it to the global variable
    else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
}

// A function to handle the registry global remove event
void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    // Do nothing
}

// A struct to store the registry listener callbacks
struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// A function to handle the shell surface ping event
void shell_surface_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial) {
    // Send a pong reply to the compositor
    wl_shell_surface_pong(shell_surface, serial);
}

// A function to handle the shell surface configure event
void shell_surface_configure(void *data, struct wl_shell_surface *shell_surface,
                             uint32_t edges, int32_t width, int32_t height) {
    // Do nothing
}

// A function to handle the shell surface popup done event
void shell_surface_popup_done(void *data, struct wl_shell_surface *shell_surface) {
    // Do nothing
}

// A struct to store the shell surface listener callbacks
struct wl_shell_surface_listener shell_surface_listener = {
    .ping = shell_surface_ping,
    .configure = shell_surface_configure,
    .popup_done = shell_surface_popup_done,
};

// A function to create a wayland buffer with proper shared memory
struct wl_buffer *create_buffer() {
    int stride = config.graph_width * 4; // 4 bytes per pixel (ARGB)
    int size = stride * config.graph_height;

    // Create shared memory file
    shm_fd = create_shm_file(size);
    if (shm_fd < 0) {
        return NULL;
    }

    // Map the shared memory
    shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return NULL;
    }

    // Create a wl_shm_pool from the shared memory
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, shm_fd, size);

    // Create a wl_buffer from the pool
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, config.graph_width, config.graph_height, 
                                                         stride, WL_SHM_FORMAT_ARGB8888);

    // Destroy the pool (buffer is still valid)
    wl_shm_pool_destroy(pool);

    return buffer;
}

// A function to draw a graph on the shared memory buffer
void draw_graph() {
    if (shm_data == NULL) {
        return;
    }

    // Create a cairo surface from the shared memory
    int stride = config.graph_width * 4;
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        shm_data, CAIRO_FORMAT_ARGB32, config.graph_width, config.graph_height, stride);

    // Create a cairo context from the surface
    cairo_t *cr = cairo_create(surface);

    // Clear the surface with white color
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Lock the mutex before reading csv_data
    pthread_mutex_lock(&csv_data_mutex);

    // Check if we have data to draw
    if (csv_data_size > 0) {
        // Set the line width and color for the graph
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); // red

        // Find the minimum and maximum x and y values in the csv data
        double min_x = csv_data[0].x;
        double max_x = csv_data[0].x;
        double min_y = csv_data[0].y;
        double max_y = csv_data[0].y;
        
        for (int i = 1; i < csv_data_size; i++) {
            if (csv_data[i].x < min_x) min_x = csv_data[i].x;
            if (csv_data[i].x > max_x) max_x = csv_data[i].x;
            if (csv_data[i].y < min_y) min_y = csv_data[i].y;
            if (csv_data[i].y > max_y) max_y = csv_data[i].y;
        }

        // Avoid division by zero
        if (max_x == min_x) max_x = min_x + 1.0;
        if (max_y == min_y) max_y = min_y + 1.0;

        // Calculate the scale and offset factors for the x and y axes
        double scale_x = (config.graph_width - 2 * config.graph_margin) / (max_x - min_x);
        double offset_x = config.graph_margin - min_x * scale_x;
        double scale_y = (config.graph_height - 2 * config.graph_margin) / (max_y - min_y);
        double offset_y = config.graph_height - config.graph_margin + min_y * scale_y; // Flip Y axis

        // Move to the first point in the csv data
        cairo_move_to(cr, 
                     csv_data[0].x * scale_x + offset_x, 
                     offset_y - csv_data[0].y * scale_y); // Flip Y

        // Loop through the rest of the csv data and draw lines to each point
        for (int i = 1; i < csv_data_size; i++) {
            cairo_line_to(cr, 
                         csv_data[i].x * scale_x + offset_x, 
                         offset_y - csv_data[i].y * scale_y); // Flip Y
        }

        // Stroke the graph
        cairo_stroke(cr);
        
        // Draw buffer status text
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14.0);
        cairo_move_to(cr, 10, 20);
        
        char status_text[128];
        snprintf(status_text, sizeof(status_text), "Points: %d / %d %s", 
                 csv_data_size, csv_buffer_max_size,
                 csv_data_size >= csv_buffer_max_size ? "(ROLLING)" : "");
        cairo_show_text(cr, status_text);
        
        // Draw axis labels with min/max values
        cairo_set_font_size(cr, 12.0);
        char label[64];
        
        // X-axis min
        snprintf(label, sizeof(label), "%.2f", min_x);
        cairo_move_to(cr, config.graph_margin, config.graph_height - config.graph_margin + 20);
        cairo_show_text(cr, label);
        
        // X-axis max
        snprintf(label, sizeof(label), "%.2f", max_x);
        cairo_move_to(cr, config.graph_width - config.graph_margin - 40, config.graph_height - config.graph_margin + 20);
        cairo_show_text(cr, label);
        
        // Y-axis min
        snprintf(label, sizeof(label), "%.2f", min_y);
        cairo_move_to(cr, 5, config.graph_height - config.graph_margin);
        cairo_show_text(cr, label);
        
        // Y-axis max
        snprintf(label, sizeof(label), "%.2f", max_y);
        cairo_move_to(cr, 5, config.graph_margin);
        cairo_show_text(cr, label);
        
    } else {
        // Draw "Waiting for data..." message
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 20.0);
        cairo_move_to(cr, config.graph_width / 2 - 80, config.graph_height / 2);
        cairo_show_text(cr, "Waiting for data...");
        
        // Show buffer size info
        cairo_set_font_size(cr, 14.0);
        cairo_move_to(cr, config.graph_width / 2 - 100, config.graph_height / 2 + 30);
        char buffer_info[128];
        snprintf(buffer_info, sizeof(buffer_info), "Buffer size: %d points", csv_buffer_max_size);
        cairo_show_text(cr, buffer_info);
    }

    // Unlock the mutex
    pthread_mutex_unlock(&csv_data_mutex);

    // Destroy the cairo context
    cairo_destroy(cr);
    
    // Destroy the cairo surface
    cairo_surface_destroy(surface);
}

// A function to update the wayland surface with the graph
void update_surface() {
    // Draw the graph on the shared memory
    draw_graph();

    // Damage the entire surface to request a redraw
    wl_surface_damage(surface, 0, 0, config.graph_width, config.graph_height);

    // Commit the wayland surface
    wl_surface_commit(surface);

    // Flush the display
    wl_display_flush(display);
}

// A function to initialize the wayland display and surface
int wayland_init() {
    // Connect to the wayland display
    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to the wayland display\n");
        return -1;
    }

    // Get the wayland registry
    registry = wl_display_get_registry(display);
    if (registry == NULL) {
        fprintf(stderr, "Failed to get the wayland registry\n");
        return -1;
    }

    // Add the registry listener callbacks
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // Roundtrip the display to get the global objects
    wl_display_roundtrip(display);

    // Check if the compositor, shell, and shm are available
    if (compositor == NULL || shell == NULL || shm == NULL) {
        fprintf(stderr, "Missing wayland global objects\n");
        return -1;
    }

    // Create a wayland surface from the compositor
    surface = wl_compositor_create_surface(compositor);
    if (surface == NULL) {
        fprintf(stderr, "Failed to create the wayland surface\n");
        return -1;
    }

    // Create a wayland shell surface from the shell and the surface
    shell_surface = wl_shell_get_shell_surface(shell, surface);
    if (shell_surface == NULL) {
        fprintf(stderr, "Failed to create the wayland shell surface\n");
        return -1;
    }

    // Add the shell surface listener callbacks
    wl_shell_surface_add_listener(shell_surface, &shell_surface_listener, NULL);

    // Set the shell surface title
    wl_shell_surface_set_title(shell_surface, "Serial CSV Graph");

    // Set the shell surface role as a toplevel window
    wl_shell_surface_set_toplevel(shell_surface);

    // Create the buffer
    buffer = create_buffer();
    if (buffer == NULL) {
        fprintf(stderr, "Failed to create buffer\n");
        return -1;
    }

    // Attach the buffer to the surface
    wl_surface_attach(surface, buffer, 0, 0);

    // Update the wayland surface with the graph
    update_surface();

    return 0;
}

// A function to clean up the wayland display and surface
void wayland_cleanup() {
    // Unmap shared memory
    if (shm_data != NULL && shm_data != MAP_FAILED) {
        munmap(shm_data, config.graph_width * config.graph_height * 4);
    }

    // Close shared memory fd
    if (shm_fd >= 0) {
        close(shm_fd);
    }

    // Destroy the wayland buffer
    if (buffer != NULL) {
        wl_buffer_destroy(buffer);
    }

    // Destroy the wayland shell surface
    if (shell_surface != NULL) {
        wl_shell_surface_destroy(shell_surface);
    }

    // Destroy the wayland surface
    if (surface != NULL) {
        wl_surface_destroy(surface);
    }

    // Destroy the wayland shm
    if (shm != NULL) {
        wl_shm_destroy(shm);
    }

    // Destroy the wayland shell
    if (shell != NULL) {
        wl_shell_destroy(shell);
    }

    // Destroy the wayland compositor
    if (compositor != NULL) {
        wl_compositor_destroy(compositor);
    }

    // Destroy the wayland registry
    if (registry != NULL) {
        wl_registry_destroy(registry);
    }

    // Disconnect from the wayland display
    if (display != NULL) {
        wl_display_disconnect(display);
    }

    // Free the global csv data array
    if (csv_data != NULL) {
        free(csv_data);
    }
    
    // Free config strings
    if (config.serial_port != NULL) {
        free(config.serial_port);
    }
}

// The main function
int main(int argc, char *argv[]) {
    // Parse command line arguments
    if (parse_arguments(argc, argv) != 0) {
        return 1;
    }
    
    // Print configuration
    printf("Starting Serial CSV Grapher\n");
    printf("Configuration:\n");
    printf("  Serial Port: %s\n", config.serial_port);
    printf("  Baud Rate: %d\n", 
           config.baud_rate == B9600 ? 9600 :
           config.baud_rate == B19200 ? 19200 :
           config.baud_rate == B38400 ? 38400 :
           config.baud_rate == B57600 ? 57600 :
           config.baud_rate == B115200 ? 115200 : 0);
    printf("  Buffer Size: %d points (rolling)\n", config.csv_buffer_size);
    printf("  Graph Size: %dx%d pixels\n", config.graph_width, config.graph_height);
    printf("  Graph Margin: %d pixels\n", config.graph_margin);
    printf("\n");

    // Initialize the serial port
    if (serial_init() == -1) {
        fprintf(stderr, "Failed to initialize the serial port\n");
        // Continue anyway - we can still test the display
    }

    // Initialize the wayland display and surface
    if (wayland_init() == -1) {
        fprintf(stderr, "Failed to initialize the wayland display and surface\n");
        return -1;
    }

    // Create a pthread for reading and parsing csv data from the serial port
    pthread_t thread;
    if (serial_fd >= 0) {
        if (pthread_create(&thread, NULL, serial_thread, NULL) != 0) {
            perror("pthread_create");
            return -1;
        }
        printf("Serial reader thread started\n");
    }

    printf("Graph window opened. Send CSV data in format: x,y\\n\n");

    // Loop until the user closes the window
    while (wl_display_dispatch(display) != -1) {
        // Update the wayland surface with the graph
        update_surface();
        
        // Small delay to avoid excessive CPU usage
        usleep(50000); // 50ms = ~20 FPS
    }

    // Cancel and join the pthread if it was created
    if (serial_fd >= 0) {
        pthread_cancel(thread);
        pthread_join(thread, NULL);
    }

    // Clean up the wayland display and surface
    wayland_cleanup();

    // Close the serial port
    if (serial_fd >= 0) {
        close(serial_fd);
    }

    printf("Program exited cleanly\n");

    // Exit the program with success
    return 0;
}
