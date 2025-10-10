#include "../lean_ui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//-----------------------------------------------------------------------------------------------------------------------------
void draw_box(float x, float y, float width, float height, float radius, uint32_t srgb_color, void* user)
{
    (void) user;
    fprintf(stdout, "draw_box(%f, %f, %f, %f, %f, %x)\n", x, y, width, height, radius, srgb_color);
}

//-----------------------------------------------------------------------------------------------------------------------------
void  draw_text(float x, float y, const char* text, uint32_t srgb_color, void* user)
{
    (void) user;
    fprintf(stdout, "draw_text(%f, %f, %s, %x)\n", x, y, text, srgb_color);
}

//-----------------------------------------------------------------------------------------------------------------------------
void set_clip_rect(uint16_t min_x, uint16_t min_y, uint16_t max_x, uint16_t max_y, void* user)
{
    (void) user;
    fprintf(stdout, "set_clip_rect(%u, %u, %u, %u)\n", min_x, min_y, max_x, max_y);
}

//-----------------------------------------------------------------------------------------------------------------------------
float text_width(const char* text, void* user)
{
    (void) user;
    return 16.f * strlen(text);
}


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
            .set_clip_rect = set_clip_rect,
            .text_width = text_width
        }
    };

    ui_context* ctx = ui_init(&def);

    ui_update_mouse_button(ctx, button_pressed);
    ui_update_mouse_pos(ctx, 187.f, 847.f);
    ui_begin_window(ctx, "Alright let's play!", 800, 100, 600, 1200);

    ui_text(ctx, align_left, "to the left");
    ui_text(ctx, align_right, "right");
    ui_newline(ctx);

    ui_value(ctx, "app time", "%2.2f sec", 45.f);
    ui_value(ctx, "delta time", "%2.2f ms", 16.f);
    ui_text(ctx, align_center, "au centre");
    ui_newline(ctx);
    static bool test_bool, en_dessous;
    ui_toggle(ctx, "test toggle plein de texte", &test_bool);
    ui_toggle(ctx, "un autre", &en_dessous);

    ui_separator(ctx);
    ui_text(ctx, align_center, "Choisit le tempo!");
    ui_newline(ctx);
    const char* list[] = {"Un", "Deux", "Trois", "Quatre"};
    static uint32_t selected = 2;

    ui_segmented(ctx, list, 4, &selected);
    ui_text(ctx, align_left, "C'est le ");
    ui_text(ctx, align_center,  list[selected]);
    ui_newline(ctx);
    ui_separator(ctx);
    static float quantity = 5.f;
    ui_slider(ctx, "Distance", 0.f, 100.f, 1.f, &quantity, "%3.2fkm");
    
    ui_separator(ctx);
    ui_button(ctx, "Left Button", align_left);
    ui_button(ctx, "Center Button", align_center);

    ui_button(ctx, "Right Button", align_right);

    ui_end_window(ctx);

    free(def.preallocated_buffer);

    return 0;
}

