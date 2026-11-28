// leanUI graphic regression test (macOS only).
//
// Renders every leanUI case offscreen through onedraw (no window, no Metal device passed in :
// onedraw creates the default one), writes each screenshot to test/out/, and compares it
// against the committed reference in test/references/ with a PSNR threshold (60 dB, the same
// one onedraw's own tests use). Run with -update-refs to regenerate the references from the
// current output. Animation cases simulate clicks and advance fixed delta_time steps, and only
// the animated widget's region is captured, so the references stay small on disk.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define FLAG_IMPLEMENTATION
#include "flag.h"

#include "vmem.h"
#include "../lean_ui.h"
#include "onedraw.h"

//-----------------------------------------------------------------------------------------------------------------------------
// constants

#define VIEW_WIDTH        (1024)
#define VIEW_HEIGHT       (800)
#define FONT_HEIGHT       (16.f)
#define DT                (1.f / 60.f)
#define SETTLE_FRAMES     (24)
#define PSNR_THRESHOLD_DB (60.0f)
#define OUT_DIR           "test/out"
#define REF_DIR           "test/references"

// parking spot for "no widget is under the cursor"
#define MOUSE_FAR         (10000.f)

//-----------------------------------------------------------------------------------------------------------------------------
// lean_ui renderer callbacks -> onedraw, colors are passed through unchanged

struct ui_backend
{
    struct onedraw* r;
    float font_size;
};

static void ui_draw_box(float x, float y, float width, float height, float radius, uint32_t srgb_color, void* user)
{
    struct ui_backend* b = (struct ui_backend*)user;
    od_draw_box(b->r, x, y, x + width, y + height, radius, (draw_color)srgb_color);
}

// lean_ui starts every text line at the top of a row that is 1.5 * font_height tall ; the baked
// font's line box is taller than font_height, so center the line box in the row
static void ui_draw_text(float x, float y, const char* text, size_t length, uint32_t srgb_color, void* user)
{
    struct ui_backend* b = (struct ui_backend*)user;
    float line_height = od_text_line_height(b->r, od_font_regular, b->font_size);
    od_draw_string_view(b->r, x, y + (b->font_size * 1.5f - line_height) * .5f, text, length, od_font_regular, b->font_size, (draw_color)srgb_color);
}

static void ui_draw_line(float x0, float y0, float x1, float y1, float width, uint32_t srgb_color, void* user)
{
    struct ui_backend* b = (struct ui_backend*)user;
    od_draw_line(b->r, x0, y0, x1, y1, width, (draw_color)srgb_color);
}

// lean_ui pushes one clip per window (ui_begin_window) and "resets" it to the full screen in
// ui_end_window() : the reset maps to a pop, the windows being the only clips of the frame
static void ui_set_clip_rect(uint16_t min_x, uint16_t min_y, uint16_t max_x, uint16_t max_y, void* user)
{
    struct ui_backend* b = (struct ui_backend*)user;
    if ((min_x == 0) && (min_y == 0) && (max_x == UINT16_MAX) && (max_y == UINT16_MAX))
        od_pop_clip(b->r);
    else
        od_canvas_cliprect(b->r, (float)min_x, (float)min_y, (float)max_x, (float)max_y);
}

static float ui_text_width(const char* text, size_t length, void* user)
{
    struct ui_backend* b = (struct ui_backend*)user;
    return od_string_view_width(b->r, text, length, od_font_regular, b->font_size);
}

//-----------------------------------------------------------------------------------------------------------------------------
// harness : one offscreen renderer, one ui_context, one reusable capture buffer

struct harness
{
    struct onedraw* r;
    ui_context* ctx;
    uint8_t* pixels;          // BGRA until bgra_to_rgba, VIEW_WIDTH*VIEW_HEIGHT*4 bytes
    struct linear_allocator arena;
    bool update_refs;
    uint32_t num_captures;
    uint32_t num_failures;
    char out_file[256];
    char ref_file[256];
};

static const char* out_path(struct harness* h, const char* name)
{
    snprintf(h->out_file, sizeof(h->out_file), OUT_DIR "/%s.png", name);
    return h->out_file;
}

static const char* ref_path(struct harness* h, const char* name)
{
    snprintf(h->ref_file, sizeof(h->ref_file), REF_DIR "/%s.png", name);
    return h->ref_file;
}

// Swaps the b and r channels of a BGRA capture buffer in place
static void bgra_to_rgba(uint8_t* pixels, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t b = pixels[i * 4 + 0];
        pixels[i * 4 + 0] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = b;
    }
}

// Writes the current capture to test/out/, compares it against the committed reference
// (identical images pass, otherwise the PSNR over all RGBA channels must stay above the
// threshold), and rewrites the reference when update_refs is set. A missing or wrong-sized
// reference counts as a failure unless update_refs recreates it
static void process_capture(struct harness* h, const char* name, uint32_t w, uint32_t ht)
{
    const uint32_t pixel_count = w * ht;
    h->num_captures++;

    bgra_to_rgba(h->pixels, pixel_count);
    stbi_write_png(out_path(h, name), (int)w, (int)ht, 4, h->pixels, (int)(w * 4));

    int ref_w = 0, ref_h = 0, ref_channels = 0;
    uint8_t* reference = stbi_load(ref_path(h, name), &ref_w, &ref_h, &ref_channels, 4);
    if (reference == NULL)
    {
        if (h->update_refs)
        {
            stbi_write_png(ref_path(h, name), (int)w, (int)ht, 4, h->pixels, (int)(w * 4));
            printf("%s : reference created\n", name);
        }
        else
        {
            h->num_failures++;
            fprintf(stderr, "%s : reference missing (run with -update-refs to create it)\n", name);
        }
        return;
    }

    if ((uint32_t)ref_w != w || (uint32_t)ref_h != ht)
    {
        h->num_failures++;
        fprintf(stderr, "%s : reference is %dx%d, capture is %ux%u\n", name, ref_w, ref_h, w, ht);
        stbi_image_free(reference);
        return;
    }

    double ssd = 0.0;
    uint32_t diff_pixels = 0, max_delta = 0;
    for (uint32_t p = 0; p < pixel_count; ++p)
    {
        bool differs = false;
        for (uint32_t c = 0; c < 4; ++c)
        {
            int a = h->pixels[p * 4 + c];
            int b = reference[p * 4 + c];
            int delta = (a > b) ? (a - b) : (b - a);
            if (delta != 0)
            {
                differs = true;
                ssd += (double)delta * (double)delta;
            }
            if (delta > (int)max_delta)
                max_delta = (uint32_t)delta;
        }
        if (differs)
            diff_pixels++;
    }
    stbi_image_free(reference);

    if (ssd == 0.0)
    {
        printf("%s : identical\n", name);
        return;
    }

    float psnr = (float)(10.0 * log10((255.0 * 255.0) / (ssd / ((double)pixel_count * 4.0))));
    if (psnr >= PSNR_THRESHOLD_DB)
    {
        printf("%s : psnr %.1f dB (threshold %.0f) - ok, %u/%u px differ, max delta %u\n",
               name, psnr, PSNR_THRESHOLD_DB, diff_pixels, pixel_count, max_delta);
    }
    else
    {
        h->num_failures++;
        fprintf(stderr, "%s : psnr %.1f dB (threshold %.0f) - MISMATCH, %u/%u px differ, max delta %u\n",
                name, psnr, PSNR_THRESHOLD_DB, diff_pixels, pixel_count, max_delta);
    }

    if (h->update_refs)
    {
        stbi_write_png(ref_path(h, name), (int)w, (int)ht, 4, h->pixels, (int)(w * 4));
        printf("%s : reference updated\n", name);
    }
}

// Simulates one frame of mouse input (the documented contract : [button] is a state-change
// event sent once, the library latches the button down between frames), draws the case's
// window, renders it into the offscreen target, and reads back the region [x, y, x+w, y+ht]
// when [name] is not NULL
static void frame(struct harness* h, void (*draw)(ui_context*), enum ui_button_state button,
                  float mouse_x, float mouse_y, float wheel,
                  const char* name, uint32_t x, uint32_t y, uint32_t w, uint32_t ht)
{
    ui_update_mouse_pos(h->ctx, mouse_x, mouse_y);
    if (button != button_idle)
        ui_update_mouse_button(h->ctx, button);
    if (wheel != 0.f)
        ui_update_mouse_wheel(h->ctx, wheel);

    od_begin_frame(h->r);
    ui_begin_frame(h->ctx, DT);
    draw(h->ctx);
    ui_end_frame(h->ctx);

    if (name != NULL)
    {
        od_set_capture_region(h->r, x, y, w, ht);
        od_take_screenshot(h->r, h->pixels);
    }

    od_end_frame(h->r, NULL);

    if (name != NULL)
        process_capture(h, name, w, ht);
}

// Redraws [draw] for SETTLE_FRAMES frames with the cursor parked far away, so every animation,
// hover, scroll and doubleclick timer of the case has settled before the next case starts.
// MAX_WINDOWS is small, so every case must end here instead of leaking a half-animated state
static void settle(struct harness* h, void (*draw)(ui_context*))
{
    for (uint32_t i = 0; i < SETTLE_FRAMES; ++i)
        frame(h, draw, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);
}

// Runs the 13 frames of a 0.2s tween started by a click (press on frame 1, release on frame
// 2, the cursor stays put) and captures [base_name]_f1/f4/f7/f10/f13. A click replaces the
// whole animation struct after ui_begin_frame advanced its t, so the click frame renders
// t = 0 and t = 1 first renders on frame 13 : those captures are t = 0, 1/4, 1/2, 3/4, 1
static void tween_frames(struct harness* h, void (*draw)(ui_context*), float mouse_x, float mouse_y,
                         const char* base_name, uint32_t x, uint32_t y, uint32_t w, uint32_t ht)
{
    const uint32_t capture_frames[] = {1, 4, 7, 10, 13};
    char name[64];

    for (uint32_t k = 1; k <= 13; ++k)
    {
        const char* capture = NULL;
        for (uint32_t i = 0; i < 5; ++i)
            if (capture_frames[i] == k)
            {
                snprintf(name, sizeof(name), "%s_f%u", base_name, k);
                capture = name;
                break;
            }

        enum ui_button_state button = (k == 1) ? button_pressed : (k == 2) ? button_released : button_idle;
        frame(h, draw, button, mouse_x, mouse_y, 0.f, capture, x, y, w, ht);
    }
}

// Draws frames 2..[last_frame] with the cursor parked and no input event (frame 1 belongs
// to the event that started the motion), capturing [base_name]_f<k> on the listed frames.
// Used for the exponential tails of progress, scroll and scrollbar hover, where nothing
// changes but the eased value
static void tail_frames(struct harness* h, void (*draw)(ui_context*), float mouse_x, float mouse_y,
                        const char* base_name, const uint32_t* capture_frames, uint32_t num_capture_frames,
                        uint32_t last_frame, uint32_t x, uint32_t y, uint32_t w, uint32_t ht)
{
    char name[64];

    for (uint32_t k = 2; k <= last_frame; ++k)
    {
        const char* capture = NULL;
        for (uint32_t i = 0; i < num_capture_frames; ++i)
            if (capture_frames[i] == k)
            {
                snprintf(name, sizeof(name), "%s_f%u", base_name, k);
                capture = name;
                break;
            }

        frame(h, draw, button_idle, mouse_x, mouse_y, 0.f, capture, x, y, w, ht);
    }
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 1 : the showcase window, every widget in one frame, sized to fit its content so no
// scrollbar shows. Full-screen capture

static bool showcase_toggle1 = false;
static bool showcase_toggle2 = true;
static uint32_t showcase_segment = 2;
static float showcase_distance = 5.f;
static float showcase_drive = 4.f;
static float showcase_pan = .5f;
static float showcase_volume = .3f;
static const ui_meter_entry showcase_disk[] =
{
    {.label = "System", .value = 96.f},
    {.label = "Apps",   .value = 224.f},
    {.label = "Data",   .value = 64.f}
};

static void draw_showcase(ui_context* ctx)
{
    ui_begin_window(ctx, "Showcase", 130.f, 20.f, 760.f, 760.f, window_resizable);

    ui_text(ctx, align_left, "On the left");
    ui_text(ctx, align_right, "On the right");
    ui_newline(ctx);

    ui_value(ctx, "app time", "%2.2f sec", 45.f);
    ui_value(ctx, "delta time", "%2.2f ms", 16.f);

    ui_toggle(ctx, "This is a toggle", &showcase_toggle1);
    ui_toggle(ctx, "and another one", &showcase_toggle2);

    ui_separator(ctx);
    ui_text(ctx, align_center, "Choose your destiny");
    ui_newline(ctx);

    const char* list[] = {"Novice", "Warrior", "Master", "Champion"};
    ui_segmented(ctx, list, 4, &showcase_segment);
    ui_text(ctx, align_left, "Your choice");
    ui_text(ctx, align_center, list[showcase_segment]);
    ui_newline(ctx);

    ui_separator(ctx);
    ui_slider(ctx, "Distance", 0.f, 100.f, 1.f, &showcase_distance, "%3.2fkm");

    ui_separator(ctx);
    ui_progress(ctx, "Download", .35f, "%3.0f%%", 35.f);
    ui_progress(ctx, "Upload", .70f, "%2.2fMB/s", 1.75f);

    ui_separator(ctx);
    ui_meter(ctx, "Disk usage", showcase_disk, 3, 512.f, "Free", "%.0f GB");

    ui_separator(ctx);
    ui_button(ctx, "Left Button", align_left);
    ui_button(ctx, "Center Button", align_center);
    ui_button(ctx, "Right Button", align_right);
    ui_newline(ctx);

    ui_knob(ctx, "drive", 1.f, 10.f, 2.f, &showcase_drive);
    ui_knob(ctx, "pan", -1.f, 1.f, 0.f, &showcase_pan);
    ui_knob(ctx, "volume", 0.f, 1.f, .7f, &showcase_volume);
    ui_newline(ctx);

    ui_separator(ctx);
    ui_text(ctx, align_center, "Wrapped paragraph");
    ui_newline(ctx);

    // the view is not NUL-terminated : only the first 103 bytes (the two sentences) are drawn
    static const char paragraph[] =
        "leanUI wraps text on spaces to the window's content width.\n"
        "A hard break comes from a newline character."
        " The rest of the buffer is never drawn.";
    ui_paragraph(ctx, paragraph, 103);

    ui_end_window(ctx);
}

static void case_showcase(struct harness* h)
{
    frame(h, draw_showcase, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    frame(h, draw_showcase, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, "showcase", 0, 0, VIEW_WIDTH, VIEW_HEIGHT);
    settle(h, draw_showcase);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 2 : one window per chrome option, all five in one frame, region capture

static void draw_options(ui_context* ctx)
{
    ui_begin_window(ctx, "opt_default", 40.f, 40.f, 240.f, 140.f, 0);
    ui_text(ctx, align_left, "default chrome");
    ui_newline(ctx);
    ui_end_window(ctx);

    ui_begin_window(ctx, "opt_resizable", 320.f, 40.f, 240.f, 140.f, window_resizable);
    ui_text(ctx, align_left, "resizable");
    ui_newline(ctx);
    ui_end_window(ctx);

    ui_begin_window(ctx, "opt_no_titlebar", 600.f, 40.f, 240.f, 140.f, window_no_titlebar);
    ui_text(ctx, align_left, "no title bar");
    ui_newline(ctx);
    ui_end_window(ctx);

    ui_begin_window(ctx, "opt_no_border", 40.f, 220.f, 240.f, 140.f, window_no_border);
    ui_text(ctx, align_left, "no border");
    ui_newline(ctx);
    ui_end_window(ctx);

    ui_begin_window(ctx, "opt_no_chrome", 320.f, 220.f, 240.f, 140.f, window_no_chrome);
    ui_text(ctx, align_left, "no chrome");
    ui_newline(ctx);
    ui_end_window(ctx);
}

static void case_options(struct harness* h)
{
    frame(h, draw_options, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, "options", 40, 40, 800, 320);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 3 : the toggle tween. A click replaces the whole animation struct, which wipes its t
// back to 0 (ui_begin_frame advanced it a moment earlier), so the click frame itself renders
// t = 0 and t = 1 first renders 12 frames later. The captures are t = 0, 1/4, 1/2, 3/4, 1

static bool toggle_value = false;

static void draw_toggle(ui_context* ctx)
{
    ui_begin_window(ctx, "Toggle anim", 100.f, 100.f, 320.f, 80.f, 0);
    ui_toggle(ctx, "Network", &toggle_value);
    ui_end_window(ctx);
}

static void case_toggle(struct harness* h)
{
    // track rect is (376, 132, 32, 16), the cursor stays on it so the thumb also shows its
    // hover expansion for the whole sequence
    frame(h, draw_toggle, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    tween_frames(h, draw_toggle, 392.f, 140.f, "toggle", 100, 124, 320, 32);
    settle(h, draw_toggle);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 4 : the segmented control slide (ease_out_back overshoots past the target segment)

static uint32_t segmented_selected = 0;
static const char* segmented_entries[] = {"One", "Two", "Three", "Four"};

static void draw_segmented(ui_context* ctx)
{
    ui_begin_window(ctx, "Segmented anim", 100.f, 100.f, 400.f, 80.f, 0);
    ui_segmented(ctx, segmented_entries, 4, &segmented_selected);
    ui_end_window(ctx);
}

static void case_segmented(struct harness* h)
{
    // segments are 96px wide from x = 108, segment 3's center is (444, 140)
    frame(h, draw_segmented, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    tween_frames(h, draw_segmented, 444.f, 140.f, "segmented", 100, 124, 400, 40);
    settle(h, draw_segmented);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 5 : the slider, three sub-cases in its window. Geometry : label row y = 128, track row
// y = 152 with center_y = 164, track from x = 146.4 to 453.6. The value 2 puts the thumb at
// x = 207.84

static float slider_value = 2.f;

static void draw_slider(ui_context* ctx)
{
    ui_begin_window(ctx, "Slider anim", 100.f, 100.f, 400.f, 120.f, 0);
    ui_slider(ctx, "Level", 0.f, 10.f, .5f, &slider_value, "%2.1f");
    ui_end_window(ctx);
}

static void case_slider(struct harness* h)
{
    //-------------------------------------------------------------------------------------------------------------------------
    // track click : the thumb glides to the cursor (the tween of case 3, same frame layout)
    frame(h, draw_slider, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    tween_frames(h, draw_slider, 376.8f, 164.f, "slider_glide", 100, 124, 400, 56);
    settle(h, draw_slider);

    //-------------------------------------------------------------------------------------------------------------------------
    // thumb hover : the thumb grows from 12px to 16px over HOVER_DURATION (6 frames), one step
    // of t per frame starting from the frame the cursor enters
    slider_value = 2.f;
    settle(h, draw_slider);
    frame(h, draw_slider, button_idle, 207.84f, 164.f, 0.f, "slider_hover_f1", 100, 124, 400, 56);
    frame(h, draw_slider, button_idle, 207.84f, 164.f, 0.f, "slider_hover_f2", 100, 124, 400, 56);
    frame(h, draw_slider, button_idle, 207.84f, 164.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_slider, button_idle, 207.84f, 164.f, 0.f, "slider_hover_f4", 100, 124, 400, 56);
    frame(h, draw_slider, button_idle, 207.84f, 164.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_slider, button_idle, 207.84f, 164.f, 0.f, "slider_hover_f6", 100, 124, 400, 56);   // fully grown
    settle(h, draw_slider);

    //-------------------------------------------------------------------------------------------------------------------------
    // thumb drag : the value follows the cursor directly, no tween (the hover grow keeps
    // running under the held thumb)
    slider_value = 2.f;
    settle(h, draw_slider);
    frame(h, draw_slider, button_pressed, 207.84f, 164.f, 0.f, "slider_drag_f1", 100, 124, 400, 56);
    frame(h, draw_slider, button_idle, 250.f, 164.f, 0.f, "slider_drag_f2", 100, 124, 400, 56);
    frame(h, draw_slider, button_idle, 320.f, 164.f, 0.f, "slider_drag_f3", 100, 124, 400, 56);
    frame(h, draw_slider, button_released, 320.f, 164.f, 0.f, "slider_drag_f4", 100, 124, 400, 56);
    settle(h, draw_slider);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 6 : the button, two sub-cases in its window. The label is center-aligned, so the
// button's center is the layout center (250, 138) whatever the font's label width

static void draw_button(ui_context* ctx)
{
    ui_begin_window(ctx, "Button anim", 100.f, 100.f, 300.f, 80.f, 0);
    ui_button(ctx, "Click me", align_center);
    ui_end_window(ctx);
}

static void case_button(struct harness* h)
{
    //-------------------------------------------------------------------------------------------------------------------------
    // click : the press frame starts the flash tween (accent -> window_bg with ease_in_expo),
    // letting go does not cancel it
    frame(h, draw_button, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    tween_frames(h, draw_button, 250.f, 138.f, "button_click", 100, 124, 300, 32);
    settle(h, draw_button);

    //-------------------------------------------------------------------------------------------------------------------------
    // hover : the hover struct is replaced with t = 0 on the entry frame, so the visible
    // steps are t = 1/6, 1/2, 5/6, 1 on the second to seventh frame
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, "button_hover_f2", 100, 124, 300, 32);
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, "button_hover_f4", 100, 124, 300, 32);
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, "button_hover_f6", 100, 124, 300, 32);
    frame(h, draw_button, button_idle, 250.f, 138.f, 0.f, "button_hover_f7", 100, 124, 300, 32);   // t = 1
    settle(h, draw_button);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 7 : the knob, two sub-cases in its window. The knob's row doubles the row height to
// 48, which puts the disc center at (156, 144) with radius 12. Drag sensitivity is
// font_height * 12 = 192 px per full range, so 30 px up moves the value by 1.5625

static float knob_value = 5.f;

static void draw_knob(ui_context* ctx)
{
    ui_begin_window(ctx, "Knob anim", 100.f, 100.f, 200.f, 160.f, 0);
    ui_knob(ctx, "level", 0.f, 10.f, 5.f, &knob_value);
    ui_newline(ctx);
    ui_end_window(ctx);
}

static void case_knob(struct harness* h)
{
    //-------------------------------------------------------------------------------------------------------------------------
    // drag : the value follows the cursor's vertical travel directly, no tween
    frame(h, draw_knob, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    frame(h, draw_knob, button_pressed, 156.f, 144.f, 0.f, "knob_drag_f1", 100, 124, 200, 96);
    frame(h, draw_knob, button_idle, 156.f, 114.f, 0.f, "knob_drag_f2", 100, 124, 200, 96);   // value 6.5625
    frame(h, draw_knob, button_idle, 156.f, 94.f, 0.f, "knob_drag_f3", 100, 124, 200, 96);    // value 7.604
    frame(h, draw_knob, button_released, 156.f, 94.f, 0.f, "knob_drag_f4", 100, 124, 200, 96);
    settle(h, draw_knob);

    //-------------------------------------------------------------------------------------------------------------------------
    // doubleclick reset : a press + release cycle first, which zeroes the doubleclick timer,
    // then a second press inside the 0.25 s window : the value tweens 7 -> default 5, the
    // press frame renders t = 0 and t = 1 lands 12 frames later
    knob_value = 7.f;
    settle(h, draw_knob);
    frame(h, draw_knob, button_pressed, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_released, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_pressed, 156.f, 144.f, 0.f, "knob_reset_0", 100, 124, 200, 96);
    frame(h, draw_knob, button_released, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, "knob_reset_25", 100, 124, 200, 96);   // value 6.5
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, "knob_reset_50", 100, 124, 200, 96);   // value 6
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, "knob_reset_75", 100, 124, 200, 96);   // value 5.5
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_knob, button_idle, 156.f, 144.f, 0.f, "knob_reset_100", 100, 124, 200, 96);  // value 5, t = 1
    settle(h, draw_knob);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 8 : the progress bar's eased fill. Bar states are global and keyed by label hash, so
// "Loading" is unique to this case. The fill closes 1 - exp(-15 * dt) ~ 22% of the gap to
// the target per frame, so a jump 0.1 -> 0.8 is still visibly moving 26 frames later and
// snaps to the target once the gap drops under 0.001

static float progress_fraction = .1f;

static void draw_progress_anim(ui_context* ctx)
{
    ui_begin_window(ctx, "Progress anim", 100.f, 100.f, 360.f, 100.f, 0);
    ui_progress(ctx, "Loading", progress_fraction, "%.0f%%", progress_fraction * 100.f);
    ui_end_window(ctx);
}

static void case_progress(struct harness* h)
{
    frame(h, draw_progress_anim, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // "Loading" state created in place at 0.1
    settle(h, draw_progress_anim);

    progress_fraction = .8f;
    frame(h, draw_progress_anim, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, "progress_f1", 100, 124, 360, 56);
    tail_frames(h, draw_progress_anim, MOUSE_FAR, MOUSE_FAR, "progress", (const uint32_t[]){3, 6, 12, 26}, 4, 26, 100, 124, 360, 56);
    settle(h, draw_progress_anim);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 9 : scrolling, three sub-cases in its window. The content is 12 lines = 288 px in a
// 208 px viewport, so max_scroll is 80 and the thumb is visible_h^2 / content_height =
// 150.2 px. The window learns its content height one frame late, so two warm-up frames
// (no capture) come first. Wheel notches multiply by 2 * row_height per notch and the
// target clamps to max_scroll on the wheel frame itself

static void draw_scroll(ui_context* ctx)
{
    ui_begin_window(ctx, "Scroll anim", 100.f, 100.f, 400.f, 240.f, 0);
    for (int i = 0; i < 12; ++i)
    {
        ui_text(ctx, align_left, "line %02d", i);
        ui_newline(ctx);
    }
    ui_end_window(ctx);
}

static void case_scroll(struct harness* h)
{
    frame(h, draw_scroll, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // creation frame, content height unknown yet
    frame(h, draw_scroll, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // the scrollbar appears now

    //-------------------------------------------------------------------------------------------------------------------------
    // wheel : three notches down while the cursor sits in the window (the target clamps to
    // max_scroll), then the scroll eases down at 1 - exp(-20 * dt) ~ 28% per frame
    frame(h, draw_scroll, button_idle, 300.f, 200.f, -3.f, "scroll_wheel_f1", 100, 100, 400, 240);
    tail_frames(h, draw_scroll, 300.f, 200.f, "scroll_wheel", (const uint32_t[]){3, 6, 12, 24}, 4, 24, 100, 100, 400, 240);
    settle(h, draw_scroll);

    //-------------------------------------------------------------------------------------------------------------------------
    // scrollbar hover : the cursor enters the full-height strip x 490..496, the bar eases
    // from its idle width (4 px) to the hover width (6 px) at 1 - exp(-15 * dt) per frame
    frame(h, draw_scroll, button_idle, 493.f, 200.f, 0.f, "scrollbar_hover_f1", 488, 128, 16, 208);
    tail_frames(h, draw_scroll, 493.f, 200.f, "scrollbar_hover", (const uint32_t[]){3, 6, 12, 24}, 4, 24, 488, 128, 16, 208);
    settle(h, draw_scroll);

    //-------------------------------------------------------------------------------------------------------------------------
    // scrollbar drag : press the thumb (at scroll = max it sits at y 185.8..336), the scroll
    // follows the cursor 1:1 through (visible_h - thumb_h) = 57.8 px of travel
    frame(h, draw_scroll, button_pressed, 494.f, 250.f, 0.f, "scrollbar_drag_f1", 100, 100, 400, 240);
    frame(h, draw_scroll, button_idle, 494.f, 150.f, 0.f, "scrollbar_drag_f2", 100, 100, 400, 240);   // clamped to 0
    frame(h, draw_scroll, button_idle, 494.f, 200.f, 0.f, "scrollbar_drag_f3", 100, 100, 400, 240);   // ~10.77
    frame(h, draw_scroll, button_released, 494.f, 200.f, 0.f, "scrollbar_drag_f4", 100, 100, 400, 240);
    settle(h, draw_scroll);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 10 : dragging a window by its title bar. The title strip is (104, 104, 292, 20), so
// the grab at (250, 114) stores an offset of (150, 14) and the window follows the cursor 1:1

static void draw_drag_window(ui_context* ctx)
{
    ui_begin_window(ctx, "Drag me anim", 100.f, 100.f, 300.f, 200.f, window_resizable);
    ui_text(ctx, align_left, "content");
    ui_newline(ctx);
    ui_end_window(ctx);
}

static void case_window_drag(struct harness* h)
{
    frame(h, draw_drag_window, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    frame(h, draw_drag_window, button_pressed, 250.f, 114.f, 0.f, "window_drag_f1", 100, 100, 460, 280);
    frame(h, draw_drag_window, button_idle, 330.f, 154.f, 0.f, "window_drag_f2", 100, 100, 460, 280);   // pos (180, 140)
    frame(h, draw_drag_window, button_idle, 410.f, 194.f, 0.f, "window_drag_f3", 100, 100, 460, 280);   // pos (260, 180)
    frame(h, draw_drag_window, button_released, 410.f, 194.f, 0.f, "window_drag_f4", 100, 100, 460, 280);
    settle(h, draw_drag_window);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 11 : resizing from the bottom-right corner. The handle's hit rect is (392, 292, 8, 8)
// for the initial window, so the grab at (396, 296) stores an offset of (4, 4) between the
// cursor and the window's bottom-right corner

static void draw_resize_window(ui_context* ctx)
{
    ui_begin_window(ctx, "Resize me anim", 100.f, 100.f, 300.f, 200.f, window_resizable);
    ui_text(ctx, align_left, "content");
    ui_newline(ctx);
    ui_end_window(ctx);
}

static void case_window_resize(struct harness* h)
{
    frame(h, draw_resize_window, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    frame(h, draw_resize_window, button_pressed, 396.f, 296.f, 0.f, "window_resize_f1", 100, 100, 460, 300);
    frame(h, draw_resize_window, button_idle, 496.f, 356.f, 0.f, "window_resize_f2", 100, 100, 460, 300);   // 400 x 260
    frame(h, draw_resize_window, button_idle, 546.f, 386.f, 0.f, "window_resize_f3", 100, 100, 460, 300);   // 450 x 290
    frame(h, draw_resize_window, button_released, 546.f, 386.f, 0.f, "window_resize_f4", 100, 100, 460, 300);
    settle(h, draw_resize_window);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 12 : a window_pinned window ignores title bar drags. Same grab and move as case 10 :
// nothing may move

static void draw_pinned_window(ui_context* ctx)
{
    ui_begin_window(ctx, "Pinned anim", 100.f, 100.f, 300.f, 200.f, window_pinned);
    ui_text(ctx, align_left, "content");
    ui_newline(ctx);
    ui_end_window(ctx);
}

static void case_pinned(struct harness* h)
{
    frame(h, draw_pinned_window, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, NULL, 0, 0, 0, 0);   // window creation frame
    frame(h, draw_pinned_window, button_pressed, 250.f, 114.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_pinned_window, button_idle, 350.f, 150.f, 0.f, NULL, 0, 0, 0, 0);
    frame(h, draw_pinned_window, button_released, 350.f, 150.f, 0.f, "pinned", 100, 100, 300, 200);
    settle(h, draw_pinned_window);
}

//-----------------------------------------------------------------------------------------------------------------------------
// case 13 : the meter's hover highlight and summary follow the cursor instantly (no tween),
// so one frame per position. It re-draws the already-created "Showcase" window instead of
// opening a 17th one (MAX_WINDOWS is 16). Bar geometry in the showcase : x 138..882,
// y 509.6..522.4 ; System 138..277.5, Apps 277.5..603, Data 603..696, Free 696..882

static void case_meter(struct harness* h)
{
    frame(h, draw_showcase, button_idle, MOUSE_FAR, MOUSE_FAR, 0.f, "meter_default", 130, 476, 748, 80);
    frame(h, draw_showcase, button_idle, 207.75f, 516.f, 0.f, "meter_hover_system", 130, 476, 748, 80);
    frame(h, draw_showcase, button_idle, 789.f, 516.f, 0.f, "meter_hover_free", 130, 476, 748, 80);
    settle(h, draw_showcase);
}

//-----------------------------------------------------------------------------------------------------------------------------
// main

static void test_log(const char* string)
{
    fprintf(stdout, "%s\n", string);
}

int main(int argc, char* argv[])
{
    // flag.h matches single-dash names; normalize "--foo" to "-foo" so both forms work
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] != '\0')
            memmove(argv[i] + 1, argv[i] + 2, strlen(argv[i]) - 1);

    struct harness h = {0};
    flag_bool_var(&h.update_refs, "update-refs", false, "rewrite test/references from the current output");
    if (!flag_parse(argc, argv))
    {
        flag_print_error(stderr);
        flag_print_options(stderr);
        return 1;
    }

    if (!od_is_supported())
    {
        fprintf(stderr, "no m1/a14 (or later) gpu, graphic test skipped\n");
        return 0;
    }

    if (mkdir(OUT_DIR, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "cannot create %s\n", OUT_DIR);
        return 1;
    }
    if (mkdir(REF_DIR, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "cannot create %s\n", REF_DIR);
        return 1;
    }

    // one linear arena holds every long-lived buffer of the test : allocated once at startup,
    // never freed individually, the whole reservation is unmapped at exit. vmem rounds every
    // block up to 16 bytes, so the arena is sized with the rounded sizes
    const size_t od_size = (od_min_memory_size() + 15) & ~(size_t)15;
    const size_t pixel_size = ((size_t)VIEW_WIDTH * VIEW_HEIGHT * 4 + 15) & ~(size_t)15;
    const size_t ui_size = (ui_min_memory_size() + 15) & ~(size_t)15;
    if (!linear_init(&h.arena, od_size + pixel_size + ui_size))
    {
        fprintf(stderr, "cannot reserve the allocator arena\n");
        return 1;
    }

    void* od_buffer = linear_alloc(&h.arena, od_min_memory_size());
    h.pixels = linear_alloc(&h.arena, (size_t)VIEW_WIDTH * VIEW_HEIGHT * 4);
    void* ui_buffer = linear_alloc(&h.arena, ui_min_memory_size());
    if ((od_buffer == NULL) || (h.pixels == NULL) || (ui_buffer == NULL))
    {
        fprintf(stderr, "arena exhausted\n");
        linear_free(&h.arena);
        return 1;
    }

    h.r = od_init(&(onedraw_def)
    {
        .preallocated_buffer = od_buffer,
        .viewport_width = VIEW_WIDTH,
        .viewport_height = VIEW_HEIGHT,
        .log_func = test_log,
        .srgb_backbuffer = false,
        .allow_screenshot = true,
        .allow_offscreen = true
    });
    if (h.r == NULL)
    {
        linear_free(&h.arena);
        return 1;
    }

    struct ui_backend backend = { .r = h.r, .font_size = FONT_HEIGHT };
    ui_def def =
    {
        .preallocated_buffer = ui_buffer,
        .font_height = FONT_HEIGHT,
        .renderer_callbacks =
        {
            .draw_box = ui_draw_box,
            .draw_text = ui_draw_text,
            .draw_line = ui_draw_line,
            .set_clip_rect = ui_set_clip_rect,
            .text_width = ui_text_width,
            .user = &backend
        }
    };
    h.ctx = ui_init(&def);

    case_showcase(&h);
    case_options(&h);
    case_toggle(&h);
    case_segmented(&h);
    case_slider(&h);
    case_button(&h);
    case_knob(&h);
    case_progress(&h);
    case_scroll(&h);
    case_window_drag(&h);
    case_window_resize(&h);
    case_pinned(&h);
    case_meter(&h);

    printf("%u captures, %u failure(s)\n", h.num_captures, h.num_failures);

    od_terminate(h.r);
    linear_free(&h.arena);
    return h.num_failures ? 1 : 0;
}
