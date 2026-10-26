#ifndef OPENCONSOLE_DASHBOARD_H
#define OPENCONSOLE_DASHBOARD_H

#include <stddef.h>
#include <stdint.h>

#define OC_DASHBOARD_MAX_GAMES 24u
#define OC_DASHBOARD_MAX_VISIBLE 10u
#define OC_DASHBOARD_QUERY_MAX 31u
#define OC_DASHBOARD_NO_INDEX ((size_t)-1)

typedef struct {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
} oc_dashboard_rect;

typedef enum {
    OC_DASHBOARD_TARGET_NONE = 0,
    OC_DASHBOARD_TARGET_CARD,
    OC_DASHBOARD_TARGET_INFO,
    OC_DASHBOARD_TARGET_PREVIOUS,
    OC_DASHBOARD_TARGET_NEXT
} oc_dashboard_target_kind;

typedef struct {
    oc_dashboard_target_kind kind;
    size_t result_index;
} oc_dashboard_target;

typedef struct {
    char query[OC_DASHBOARD_QUERY_MAX + 1u];
    size_t query_length;
    size_t result_indices[OC_DASHBOARD_MAX_GAMES];
    size_t result_count;
    size_t selected_result;
    size_t page;
    size_t page_capacity;
    size_t page_count;
    size_t visible_count;
    oc_dashboard_rect card_rects[OC_DASHBOARD_MAX_VISIBLE];
    oc_dashboard_rect info_rects[OC_DASHBOARD_MAX_VISIBLE];
    oc_dashboard_rect previous_rect;
    oc_dashboard_rect next_rect;
    size_t cursor_x;
    size_t cursor_y;
    uint8_t cursor_visible;
    uint8_t left_button;
    oc_dashboard_target hover;
    oc_dashboard_target pressed;
    size_t details_game;
} oc_dashboard_view;

typedef struct {
    size_t card_side;
    size_t gap;
    size_t start_x;
    size_t card_y;
    size_t row_gap;
    size_t columns;
    size_t capacity;
} oc_dashboard_layout;

void oc_dashboard_rebuild(oc_dashboard_view *view,
                          const char *const *titles, size_t title_count);
int oc_dashboard_edit(oc_dashboard_view *view, uint16_t character,
                      const char *const *titles, size_t title_count);
void oc_dashboard_move(oc_dashboard_view *view, int direction);
int oc_dashboard_change_page(oc_dashboard_view *view, int direction);
size_t oc_dashboard_visible_game(const oc_dashboard_view *view, size_t slot);
size_t oc_dashboard_selected_game(const oc_dashboard_view *view);
int oc_dashboard_layout_cards(size_t width, size_t height, size_t result_count,
                              oc_dashboard_layout *layout);
int oc_dashboard_apply_layout(oc_dashboard_view *view, size_t width, size_t height,
                              oc_dashboard_layout *layout);
void oc_dashboard_move_pointer(oc_dashboard_view *view, int32_t relative_x,
                               int32_t relative_y, uint64_t resolution_x,
                               uint64_t resolution_y, size_t width, size_t height);
oc_dashboard_target oc_dashboard_hit_test(const oc_dashboard_view *view);
size_t oc_dashboard_pointer_button(oc_dashboard_view *view, int left_button,
                                   int *page_changed);

#endif
