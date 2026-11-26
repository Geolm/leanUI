#include "../lean_ui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// This example plays the role of the host application : it owns a "renderer" that just
// prints the draw commands on stdout, and simulates a few frames of mouse input to
// exercise the library. It covers:
//   - the classic widgets (text, value, toggle, segmented, slider, button, knob)
//   - ui_paragraph()               wrapped text, string view of [length] bytes
//   - ui_get_remaining() + ui_advance() + ui_get_row_height()  embedding custom content
//   - ui_update_mouse_wheel() + scrollbar   scrolling a window whose content overflows

//-----------------------------------------------------------------------------------------------------------------------------
// renderer callbacks : print the draw commands on stdout

void draw_box(float x, float y, float width, float height, float radius, uint32_t srgb_color, void* user)
{
    (void) user;
    fprintf(stdout, "draw_box(%f, %f, %f, %f, %f, %x)\n", x, y, width, height, radius, srgb_color);
}

void draw_text(float x, float y, const char* text, size_t length, uint32_t srgb_color, void* user)
{
    (void) user;
    fprintf(stdout, "draw_text(%f, %f, ", x, y);
    fwrite(text, 1, length, stdout); // [text] is a view of [length] bytes, not necessarily null-terminated
    fprintf(stdout, ", %x)\n", srgb_color);
}

void draw_line(float x0, float y0, float x1, float y1, float width, uint32_t srgb_color, void* user)
{
    (void) user;
    fprintf(stdout, "draw_line(%f, %f, %f, %f, %f, %x)\n", x0, y0, x1, y1, width, srgb_color);
}

void set_clip_rect(uint16_t min_x, uint16_t min_y, uint16_t max_x, uint16_t max_y, void* user)
{
    (void) user;
    fprintf(stdout, "set_clip_rect(%u, %u, %u, %u)\n", min_x, min_y, max_x, max_y);
}

float text_width(const char* text, size_t length, void* user)
{
    (void) user;
    (void) text;
    return 16.f * (float) length; // width of the [length] bytes view of [text]
}

//-----------------------------------------------------------------------------------------------------------------------------
// phase 1 : the classic demo window. Its content fits inside it, so no scrollbar appears

void draw_demo_window(ui_context* ctx)
{
    ui_begin_window(ctx, "Alright let's play!", 800, 100, 600, 1600, window_resizable);

    ui_text(ctx, align_left, "On the left");
    ui_text(ctx, align_right, "On the right");
    ui_newline(ctx);

    ui_value(ctx, "app time", "%2.2f sec", 45.f);
    ui_value(ctx, "delta time", "%2.2f ms", 16.f);
    ui_text(ctx, align_center, "In the middle");
    ui_newline(ctx);
    static bool test_bool, en_dessous;
    ui_toggle(ctx, "This is a toggle", &test_bool);
    ui_toggle(ctx, "and another one", &en_dessous);

    ui_separator(ctx);
    ui_text(ctx, align_center, "Choose your destiny");
    ui_newline(ctx);
    const char* list[] = {"Novice", "Warrior", "Master", "Champion"};
    static uint32_t selected = 2;

    ui_segmented(ctx, list, 4, &selected);
    ui_text(ctx, align_left, "Your choice");
    ui_text(ctx, align_center,  list[selected]);
    ui_newline(ctx);
    ui_separator(ctx);
    static float quantity = 5.f;
    ui_slider(ctx, "Distance", 0.f, 100.f, 1.f, &quantity, "%3.2fkm");

    ui_separator(ctx);
    ui_button(ctx, "Left Button", align_left);
    ui_button(ctx, "Center Button", align_center);
    ui_button(ctx, "Right Button", align_right);

    ui_newline(ctx);

    float drive, pan, volume;
    ui_knob(ctx, "drive", 1.f, 10.f, 2.f, &drive);
    ui_knob(ctx, "pan", -1.f, 1.f, 0.f, &pan);
    ui_knob(ctx, "volume", 0.f, 1.f, .7f, &volume);

    ui_newline(ctx);

    //-------------------------------------------------------------------------------------------------------------------------
    // new use case : ui_paragraph() displays a view of [length] bytes wrapped on spaces to the
    // window's content width and re-wrapped every frame ; '\n' forces a hard break. The view is
    // not NUL-terminated : the buffer holds more bytes than the view displays, and only the
    // first 103 bytes (the two sentences) are drawn
    ui_separator(ctx);
    ui_text(ctx, align_center, "Wrapped paragraph");
    ui_newline(ctx);

    static const char paragraph[] =
        "leanUI wraps text on spaces to the window's content width.\n"
        "A hard break comes from a newline character."
        " The rest of the buffer is never drawn.";
    ui_paragraph(ctx, paragraph, 103);

    //-------------------------------------------------------------------------------------------------------------------------
    // new use case : embed custom content in the layout. ui_get_remaining() gives the area from the
    // current row down to the bottom of the window's content ; draw anything in it, then tell the
    // layout how much vertical space was consumed with ui_advance(). Here a bar chart fills the
    // rest of the window (in a real app this is where a panel from another layout engine would go)
    ui_separator(ctx);
    ui_text(ctx, align_center, "Custom content");
    ui_newline(ctx);

    ui_rect remaining = ui_get_remaining(ctx);
    float row = ui_get_row_height(ctx);

    const float bars[] = {.35f, .8f, .55f, 1.f, .65f, .4f};
    const uint32_t num_bars = sizeof(bars)/sizeof(bars[0]);
    float chart_h = remaining.height - row;   // leave one row for the baseline
    float slot = remaining.width / (float) num_bars;
    for (uint32_t i = 0; i < num_bars; ++i)
    {
        float bar_h = chart_h * bars[i];
        draw_box(remaining.x + slot * i + slot*.15f, remaining.y + chart_h - bar_h, slot*.7f, bar_h, 4.f, 0xFFE68B3E, NULL);
    }
    draw_box(remaining.x, remaining.y + chart_h, remaining.width, 2.f, 1.f, 0x40553E2C, NULL); // baseline

    ui_advance(ctx, remaining.height);        // the panel consumed the whole remaining area

    ui_end_window(ctx);
}

//-----------------------------------------------------------------------------------------------------------------------------
// phase 2 : a small window whose content is taller than its visible area. The scrollbar shows
// up (content_height is known from the previous frame) and the content can be scrolled with
// the mouse wheel or by dragging the scrollbar in the right gutter

void draw_log_window(ui_context* ctx)
{
    ui_begin_window(ctx, "System log", 100, 100, 500, 300, 0);

    ui_text(ctx, align_left, "System log");
    ui_newline(ctx);
    ui_separator(ctx);
    for (int i = 0; i < 14; ++i)
    {
        ui_text(ctx, align_left, "entry %02d : something happened", i);
        ui_newline(ctx);
    }
    ui_separator(ctx);

    static const char note[] =
        "This window's content is taller than its visible area. "
        "Scroll it with the mouse wheel or drag the scrollbar in the right gutter. "
        "Widgets scrolled outside the window cannot be clicked any more.";
    ui_paragraph(ctx, note, strlen(note));

    ui_end_window(ctx);
}

//-----------------------------------------------------------------------------------------------------------------------------
// phase 3 : a chromeless window : no border, no title, no resize handle, and a layout that
// starts at the top edge of the window. Scrolling is unaffected, so the scrollbar still shows.

void draw_background_window(ui_context* ctx)
{
    ui_begin_window(ctx, "Panel", 100, 100, 500, 300, window_no_chrome);

    for (int i = 0; i < 14; ++i)
    {
        ui_text(ctx, align_left, "panel entry %02d : more text", i);
        ui_newline(ctx);
    }

    ui_end_window(ctx);
}

//-----------------------------------------------------------------------------------------------------------------------------
// phase 4 : dragging a window by its title bar. The border, title and content must move in the same
// frame the mouse moves (a stale title lag is a one-frame-late window)

void draw_draggable_window(ui_context* ctx)
{
    ui_begin_window(ctx, "Dragging me", 100, 100, 400, 300, window_resizable);

    ui_text(ctx, align_left, "content");
    ui_newline(ctx);

    ui_end_window(ctx);
}

//-----------------------------------------------------------------------------------------------------------------------------
// simulates one frame worth of mouse input, then renders the [draw] window.
// Per the documented input contract [button] is a state-change event sent once :
// button_pressed when the button goes down, button_released when it is let go,
// button_idle for frames without a change (the library keeps the button down between frames).
// [wheel] is the wheel notches fed to ui_update_mouse_wheel() this frame (0 = no event).

void simulate_frame(ui_context* ctx, enum ui_button_state button, float x, float y, float wheel, void (*draw)(ui_context*))
{
    ui_update_mouse_pos(ctx, x, y);
    if (button != button_idle)
        ui_update_mouse_button(ctx, button);
    if (wheel != 0.f)
        ui_update_mouse_wheel(ctx, wheel);

    fprintf(stdout, "\n===== frame : mouse(%g, %g), button=%s, wheel=%g =====\n",
            x, y, button == button_pressed ? "pressed" : button == button_released ? "released" : "idle", wheel);

    ui_begin_frame(ctx, 1.f/60.f);
    draw(ctx);
    ui_end_frame(ctx);
}

//-----------------------------------------------------------------------------------------------------------------------------
int main(void)
{
    ui_def def =
    {
        .preallocated_buffer = malloc(ui_min_memory_size()),
        .font_height = 32.f,
        .renderer_callbacks =
        {
            .draw_box = draw_box,
            .draw_text = draw_text,
            .draw_line = draw_line,
            .set_clip_rect = set_clip_rect,
            .text_width = text_width
        }
    };

    ui_context* ctx = ui_init(&def);

    //-------------------------------------------------------------------------------------------------------------------------
    // phase 1 : the classic demo window, pressed button outside of it (nothing interactive happens)
    simulate_frame(ctx, button_pressed, 187.f, 847.f, 0.f, draw_demo_window);

    //-------------------------------------------------------------------------------------------------------------------------
    // phase 2 : scrolling the "System log" window (100,100) 500x300.
    // Its content area starts at y=164 and is 228px tall while the content is ~1200px high, so the
    // scrollbar shows on the right edge (x 584..592). The script is:
    //   warm-up frame (no input) so the library learns the content height and the scrollbar appears
    //   three wheel notches down while the cursor sits inside the content -> the scroll eases down
    //   the cursor moves over the scrollbar -> the bar grows and brightens
    //   the cursor presses on the thumb and drags it to the bottom of the window, then lets go

    simulate_frame(ctx, button_idle, 1000.f, 1000.f, 0.f, draw_log_window);   // warm-up, scrollbar appears

    // scroll down one notch per frame : wheel deltas add up until ui_end_frame
    simulate_frame(ctx, button_idle, 300.f, 200.f, -1.f, draw_log_window);
    simulate_frame(ctx, button_idle, 300.f, 200.f, -1.f, draw_log_window);
    simulate_frame(ctx, button_idle, 300.f, 200.f, -1.f, draw_log_window);

    // let the scroll ease towards its target (no input)
    simulate_frame(ctx, button_idle, 300.f, 200.f, 0.f, draw_log_window);
    simulate_frame(ctx, button_idle, 300.f, 200.f, 0.f, draw_log_window);
    simulate_frame(ctx, button_idle, 300.f, 200.f, 0.f, draw_log_window);

    // hover over the scrollbar : the bar grows and brightens
    simulate_frame(ctx, button_idle, 588.f, 250.f, 0.f, draw_log_window);
    simulate_frame(ctx, button_idle, 588.f, 250.f, 0.f, draw_log_window);

    // press on the thumb and drag it down, then let go
    simulate_frame(ctx, button_pressed, 588.f, 250.f, 0.f, draw_log_window);
    simulate_frame(ctx, button_idle,   588.f, 320.f, 0.f, draw_log_window);   // held : mouse_down is kept
    simulate_frame(ctx, button_idle,   588.f, 350.f, 0.f, draw_log_window);   // held
    simulate_frame(ctx, button_released, 588.f, 350.f, 0.f, draw_log_window);

    //-------------------------------------------------------------------------------------------------------------------------
    // phase 3 : the window_no_chrome window, warm-up frame then a wheel notch down
    simulate_frame(ctx, button_idle, 1000.f, 1000.f, 0.f, draw_background_window);   // warm-up, scrollbar appears
    simulate_frame(ctx, button_idle, 350.f, 250.f, -1.f, draw_background_window);
    simulate_frame(ctx, button_idle, 350.f, 250.f, 0.f, draw_background_window);

    //-------------------------------------------------------------------------------------------------------------------------
    // phase 4 : drag the "Dragging me" window by its title bar (grab, then move with the button held)
    simulate_frame(ctx, button_pressed, 250.f, 130.f, 0.f, draw_draggable_window);   // grab the title bar
    simulate_frame(ctx, button_idle,   350.f, 250.f, 0.f, draw_draggable_window);   // held : move the mouse

    free(def.preallocated_buffer);

    return 0;
}
