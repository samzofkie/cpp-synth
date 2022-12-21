#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cairo.h>
#include <cairo-xlib.h>

#include "xwindow.h"

using namespace std;


XWindow::XWindow() :
  window_width(1000), window_height(1000),
  total_time(3),
  start_time(0),
  end_time(4),
  tempo(120),
  sample_rate(44100),
  track_height(95),
  space_between_tracks(5),
  track_head_width(175),
  track_head_fill(
      cairo_image_surface_create_from_png("../data/steel.png")),
  header_height(100),
  header(new Header(this)),
  pa_sink(new PAHandler(4096))
{
  srand(time(NULL));
    
  create_window(); 
  
  for (int i=0; i<8; i++)
    tracks.push_back(new Track(this));

  PCM *snare = new PCM("../data/snare.wav");
  tracks[1]->add_pcm(snare);

  //event_loop();
}


XWindow::~XWindow()
{
  XCloseDisplay(display);
  cairo_surface_destroy(track_head_fill);
  delete header;
  for (Track *track : tracks)
    delete track;
  delete pa_sink;
}


void XWindow::create_window()
{
  if ((display = XOpenDisplay(NULL)) == NULL) {
    cout << "XOpenDisplay() failed!\n";
    exit(1);
  }
  screen = DefaultScreen(display);
  window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                               0, 0, window_width, window_height, 
                               0, 0, 0);
  XSelectInput(display, window, ButtonPressMask | KeyPressMask | ExposureMask);
  XMapWindow(display, window);
  
  // The cairo surface potentially needs to be as big as 
  // the screen in the display. Defensive would be to make it
  // as big as the Largest screen in the display.
  int surface_width = DisplayWidth(display, screen);
  int surface_height = DisplayHeight(display, screen);
  cairo_surf = cairo_xlib_surface_create(display,
             window, DefaultVisual(display, screen),
             surface_width, surface_height);
  cairo_xlib_surface_set_size(cairo_surf, surface_width, surface_height);
  cr = cairo_create(cairo_surf);
}


double XWindow::global_time_to_adjusted_pixels(double time)
{
  cout << start_time << " ";
  cout << time << " ";
  cout << end_time << endl;
  assert(time >= start_time); 
  assert(time <= end_time);

  double total_time = end_time - start_time;
  double relative_time = time - start_time;
  double pixels_per_second = (window_width - track_head_width)
    / total_time;
  double pixel = relative_time * pixels_per_second 
      + track_head_width;
  return pixel;
}


void XWindow::draw_grid()
{
  // Black out underneath
  cairo_rectangle(cr, track_head_width,
              header_height, window_width - track_head_width,
              window_height - header_height);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_fill(cr);

  double total_track_height = track_height + space_between_tracks;

  // Vertical lines
  double beats_per_second = tempo/60; 
  double seconds_per_beat = 60/tempo; 
  double start_second = ceil(start_time * beats_per_second)
    * seconds_per_beat;  
  double first_pixel_x = 
    global_time_to_adjusted_pixels(start_second);
  
  double last_pixel_x;
  if (end_time < total_time)
    last_pixel_x = window_width;
  else
    last_pixel_x = global_time_to_adjusted_pixels(total_time); 
  
  double pixels_per_beat = (window_width - track_head_width) /
    (end_time - start_time) * seconds_per_beat;
  
  //cout << first_pixel_x << " " << last_pixel_x << endl;
  //cout << pixels_per_beat << endl; 
  for (double i = first_pixel_x; 
      i <= last_pixel_x+1; 
      i += pixels_per_beat) {
    //cout << i << endl;
    
    if (i<0) exit(1);
    cairo_move_to(cr, i, header_height);
    cairo_line_to(cr, i, header_height + 
        tracks.size() * total_track_height);
  }
  //cout << endl;
      
  // Horizontal lines
  for (vector<Track*>::size_type i=0; i<tracks.size()+1; i++) {
    cairo_move_to(cr, track_head_width, 
        total_track_height*i + header_height + 0.5);
    cairo_line_to(cr, last_pixel_x,
        total_track_height*i + header_height + 0.5);
  } 

  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
}


void XWindow::draw_tracks()
{
  for (vector<Track*>::size_type i=0; i<tracks.size(); i++)
    tracks[i]->draw(cr, 0, 
        i*(track_height+space_between_tracks) + space_between_tracks/2 + header_height,
                window_width, track_height);
}


double XWindow::calc_start_time(double mouse_position, 
    double new_end_time)
{
  return ((new_end_time - mouse_position) * (mouse_position - start_time)
    / (mouse_position - end_time)) + mouse_position;

}


// Scroll logic
  // x is mouse position (in seconds)
  //   start_time + x / end_time - x = 
  //   new_start_time + x / new_end_time - x
  // to zoom in on whats underneath the cursor.
  // But if we want the difference between
  // start_time and new_start_time plus the difference
  // between end_time and new_end_time to always be 
  // a constant (like 0.25) we need:
  //  | start_time - new_start_time | +
  //  | new_end_time - end_time | = 0.25
void XWindow::handle_click(XEvent e)
{
  /*printf("Click! @ (%d, %d) by button %u\n", 
            e.xbutton.x, e.xbutton.y, e.xbutton.button);*/
  double total_time = end_time - start_time;
  
  double x = max(0.0, e.xbutton.x - track_head_width); 
  x = (total_time / 
      (window_width - track_head_width)) * x + start_time;
  
  bool scrolled = false;
  double proportion;

  // Scroll up ~> zoom out
  if (e.xbutton.button == 4) {
    proportion = 1.2;
    scrolled = true;
  }
  
  // Scroll down ~> zoom in
  else if (e.xbutton.button == 5) { 
    proportion = 0.8;
    scrolled = true;
  }

  if (scrolled) {
    double new_end_time = (1-proportion) * x 
      + (proportion*end_time);
    double new_start_time = calc_start_time(x, new_end_time);
    if (new_start_time < 0)
      new_start_time = 0;
    
    start_time = new_start_time;
    end_time = new_end_time;

    draw_grid();
    draw_tracks();          
  }

}


void XWindow::event_loop()
{
  char keybuf[8];
  KeySym key;
  XEvent e;
  for (;;) {
    XNextEvent(display, &e);
    switch (e.type)
    {
      case ButtonPress:         
        handle_click(e);
        break;

      case KeyPress:
        XLookupString(&e.xkey, keybuf, sizeof(keybuf), &key, NULL);
        printf("KeyPress string: %s\n", keybuf);
        break;

      case Expose:
        window_width = e.xexpose.width;
        window_height = e.xexpose.height;
        
        header->draw(cr, 0, 0, window_width, header_height);
        draw_grid();
        draw_tracks();
        break;

      default:
        if (e.type != 65)
          cout << "Unhandled XEvent.type: " << e.type << endl;
    }
  }
}


void rounded_rect(cairo_t *cr, double x, double y,
                               double w, double h, double radius)
{
  cairo_move_to(cr, x+radius, y);
  cairo_line_to(cr, x+w-radius, y);
  cairo_arc(cr, x+w-radius, y+radius,
            radius, M_PI/-2.0, 0);
  cairo_line_to(cr, x+w, y+h-radius);
  cairo_arc(cr, x+w-radius, y+h-radius,
            radius, 0, M_PI/2.0);
  cairo_line_to(cr, x+radius, y+h);
  cairo_arc(cr, x+radius, y+h-radius,
            radius, M_PI/2.0, M_PI);
  cairo_line_to(cr, x, y+radius);
  cairo_arc(cr, x+radius, y+radius,
            radius, M_PI, M_PI/-2.0);
  cairo_close_path(cr);
}
