# wayland_plotter
Simple serial plotter using Wayland 
Created by BingAI 

Okay, I will try to explain the code. The code is divided into several functions, each with a specific purpose. Here is a brief overview of each function:

- serial_init: This function opens the serial port and sets its attributes, such as baud rate, character size, parity, etc. It returns 0 on success or -1 on failure.
- serial_read_line: This function reads a line from the serial port and stores it in a buffer. It returns the number of characters read or -1 on failure.
- csv_parse_line: This function parses a line of csv data and stores the x and y values in a global array. It returns 0 on success or -1 on failure.
- serial_thread: This function runs in a separate thread and reads and parses csv data from the serial port. It uses the select function to check if the serial port is ready for reading. It returns NULL as the thread exit value.
- registry_global: This function handles the registry global event and binds the compositor and shell interfaces to global variables.
- registry_global_remove: This function handles the registry global remove event and does nothing.
- shell_surface_ping: This function handles the shell surface ping event and sends a pong reply to the compositor.
- shell_surface_configure: This function handles the shell surface configure event and does nothing.
- shell_surface_popup_done: This function handles the shell surface popup done event and does nothing.
- create_buffer_from_cairo_surface: This function creates a wayland buffer from a cairo surface using a shared memory pool. It returns the buffer or NULL on failure.
- draw_graph: This function draws a graph on a cairo surface from the csv data. It scales and offsets the data to fit the graph size and margin. It uses red color for the graph line.
- update_surface: This function updates the wayland surface with the graph. It creates a cairo surface, draws the graph on it, creates a wayland buffer from it, attaches it to the surface and commits it. It also flushes the display.
- wayland_init: This function initializes the wayland display and surface. It connects to the display, gets the registry, adds the registry listener, roundtrips the display, checks if the compositor and shell are available, creates a surface, creates a shell surface, adds the shell surface listener, sets the shell surface title and role, and updates the surface. It returns 0 on success or -1 on failure.
- wayland_cleanup: This function cleans up the wayland display and surface. It destroys the buffer, cairo surface, shell surface, surface, shell, compositor, registry and display. It also frees the csv data array.
- main: This is the main function of the program. It initializes the serial port and wayland display and surface. It creates a pthread for reading and parsing csv data from the serial port. It loops until Ctrl-C is pressed and waits for wayland events and updates the surface. It cancels and joins the pthread and cleans up the wayland display and surface. It closes the serial port and exits with success.

I hope this explanation helps you understand how the code works. Do you have any questions?


Sure, I can try to improve the code so it also supports resizing the window. I think I can use the shell surface configure event to get the new width and height of the window and update the graph accordingly. Here is the modified code:

```c
// A function to handle the shell surface configure event
void shell_surface_configure(void *data, struct wl_shell_surface *shell_surface,
                             uint32_t edges, int32_t width, int32_t height) {
    // Update the global graph width and height variables
    GRAPH_WIDTH = width;
    GRAPH_HEIGHT = height;

    // Update the wayland surface with the graph
    update_surface();
}

