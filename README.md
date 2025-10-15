# leanUI

[![Build Status](https://github.com/Geolm/leanUI/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/geolm/leanUI/actions)


leanUI is a small immediate-mode UI library written in C99.  
It’s designed for games and lightweight applications that need a clean, polished interface without bringing in a full framework.

<img src="docs/example.png" alt="leanUI example" width="50%">


---

## Overview

- Immediate-mode API (rendering is also immediate, now command list to traverse, easier to integrate your own rendering)
- No allocations, no dependencies beyond the C standard library
- Single header / single source, easy to drop into any project 
- Built-in easing and animation for a smooth, modern feel
- A focused set of well-designed widgets  
- ~500 lines of code, compiles in seconds
- Low memory footprint ~3kb as the lib does not store any data for widget and don't use hashmap

---

## Widgets

leanUI provides a small, cohesive set of widgets:

- **Buttons** — with hover and press animations

![buttons](docs/buttons.png)

- **Labels / value fields** — for clean key–value layouts  
- **Toggles** — animated switches inspired by iOS

![toggle](docs/toggle.png)

- **Segmented controls** — for mode switching

![segmented](docs/segmented.png)

- **Sliders** 

![slider](docs/slider.png)

Each widget is built with consistent spacing, alignment, and interaction patterns.  
The goal is not to cover everything, but to make a minimal set look and feel right out of the box.

## Animation

Unlike most tiny immediate-mode UI libraries, leanUI has animation built in:

- Hover, press and toggle states are automatically eased over time.
- Animation framerate independent, uses delta time between frames.
- No per-widget state is stored — transitions are computed implicitly based on widget identity.  
  
This gives small UIs a fluid, modern feel without adding complexity to your code.

## Layout

Very simple and effective layout (no rows or columns)
- Horizontal alignment : left, center or right
- Manually call to ui_newline to make a carriage return

---

## Integration

You just need to add lean_ui.c/h to your project, set the callbacks at init and update input/delta_time each frame.

### Callbacks
- **draw_box**: The rectangle’s rounded corners are expected to fit entirely within the rectangle’s bounds; no part of the corner should extend beyond x, y, width, or height. Colors are defined in the sRGB color space. leanUI uses alpha blending.

- **draw_text**: Draws text at (x, y). The y coordinate specifies the top, but the text will be centered vertically relative to the font height.

- **set_clip_rect**: Sets a clipping rectangle so that all subsequent drawing is limited to (x, y, width, height). You can ignore this callback if no widget will be drawn outside the window frame.

- **text_width**: Returns the width that the renderer would need to draw the specified text.


### Inputs

* **ui_update_mouse_pos**: Should be called each time the mouse moves
* **ui_update_mouse_button**: Call this function whenever a mouse button changes state. Do not call with button_pressed param if the button remains pressed.

---

## Example

For a complete and compiling example see [test/test.c](test/test.c)

```c
#include "lean_ui.h"

void frame(ui_context* ctx, float delta_time)
{
    ui_begin_frame(ctx, 1.f/60.f);
    ui_begin_window(ctx, "Alright let's play!", 800, 100, 600, 1200, window_resizable);

    const char* list[] = {"One", "Two", "Three", "Four"};
    static uint32_t selected = 2;
    ui_segmented(ctx, list, 4, &selected);

    ui_separator(ctx);

    static float quantity = 5.f;
    ui_slider(ctx, "Distance", 0.f, 100.f, 1.f, &quantity, "%3.2fkm");

    if (ui_button(ctx, "Left Button", align_left))
    {
        // process
    }

    ui_end_window(ctx);
    ui_end_frame(ctx);
}


```

