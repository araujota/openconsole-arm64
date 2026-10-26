#include "openconsole/dashboard.h"

static char ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') return (char)(value - 'A' + 'a');
    return value;
}

static int title_contains(const char *title, const char *query) {
    size_t start;
    if (query[0] == '\0') return 1;
    for (start = 0u; title[start] != '\0'; ++start) {
        size_t offset = 0u;
        while (query[offset] != '\0' && title[start + offset] != '\0' &&
               ascii_lower(title[start + offset]) == ascii_lower(query[offset]))
            ++offset;
        if (query[offset] == '\0') return 1;
    }
    return 0;
}

static int targets_equal(oc_dashboard_target a, oc_dashboard_target b) {
    return a.kind == b.kind && a.result_index == b.result_index;
}

static int point_in_rect(size_t x, size_t y, const oc_dashboard_rect *rect) {
    return rect->width != 0u && rect->height != 0u && x >= rect->x && y >= rect->y &&
           x - rect->x < rect->width && y - rect->y < rect->height;
}

static void clear_pointer_targets(oc_dashboard_view *view) {
    view->hover.kind = OC_DASHBOARD_TARGET_NONE;
    view->hover.result_index = OC_DASHBOARD_NO_INDEX;
    view->pressed.kind = OC_DASHBOARD_TARGET_NONE;
    view->pressed.result_index = OC_DASHBOARD_NO_INDEX;
    view->left_button = 0u;
    view->details_game = OC_DASHBOARD_NO_INDEX;
}

static void reconcile_page(oc_dashboard_view *view) {
    if (view->page_capacity == 0u) view->page_capacity = 1u;
    view->page_count = view->result_count == 0u ? 0u :
        (view->result_count + view->page_capacity - 1u) / view->page_capacity;
    if (view->page_count == 0u) {
        view->page = 0u;
        view->selected_result = 0u;
        view->visible_count = 0u;
    } else {
        size_t start;
        if (view->page >= view->page_count) view->page = view->page_count - 1u;
        start = view->page * view->page_capacity;
        view->visible_count = view->result_count - start;
        if (view->visible_count > view->page_capacity)
            view->visible_count = view->page_capacity;
        if (view->selected_result < start ||
            view->selected_result >= start + view->visible_count)
            view->selected_result = start;
    }
}

void oc_dashboard_rebuild(oc_dashboard_view *view,
                          const char *const *titles, size_t title_count) {
    size_t index;
    size_t previous_game = oc_dashboard_selected_game(view);
    view->result_count = 0u;
    if (title_count > OC_DASHBOARD_MAX_GAMES) title_count = OC_DASHBOARD_MAX_GAMES;
    for (index = 0u; index < title_count; ++index) {
        if (title_contains(titles[index], view->query))
            view->result_indices[view->result_count++] = index;
    }
    view->selected_result = 0u;
    for (index = 0u; index < view->result_count; ++index) {
        if (view->result_indices[index] == previous_game) {
            view->selected_result = index;
            break;
        }
    }
    reconcile_page(view);
}

int oc_dashboard_edit(oc_dashboard_view *view, uint16_t character,
                      const char *const *titles, size_t title_count) {
    if (character == 8u) {
        if (view->query_length == 0u) return 0;
        view->query[--view->query_length] = '\0';
    } else if (character == 27u) {
        if (view->query_length == 0u) return 0;
        view->query_length = 0u;
        view->query[0] = '\0';
    } else if (character >= 32u && character <= 126u) {
        if (view->query_length >= OC_DASHBOARD_QUERY_MAX) return 0;
        view->query[view->query_length++] = (char)character;
        view->query[view->query_length] = '\0';
    } else {
        return 0;
    }
    oc_dashboard_rebuild(view, titles, title_count);
    view->page = 0u;
    view->selected_result = 0u;
    clear_pointer_targets(view);
    reconcile_page(view);
    return 1;
}

void oc_dashboard_move(oc_dashboard_view *view, int direction) {
    if (view->result_count == 0u || direction == 0) return;
    if (direction < 0)
        view->selected_result = view->selected_result == 0u ?
            view->result_count - 1u : view->selected_result - 1u;
    else
        view->selected_result = (view->selected_result + 1u) % view->result_count;
    view->page = view->selected_result / view->page_capacity;
    reconcile_page(view);
}

int oc_dashboard_change_page(oc_dashboard_view *view, int direction) {
    size_t next;
    if (view->page_count == 0u || direction == 0) return 0;
    next = view->page;
    if (direction < 0) {
        if (next == 0u) return 0;
        --next;
    } else {
        if (next + 1u >= view->page_count) return 0;
        ++next;
    }
    view->page = next;
    view->selected_result = next * view->page_capacity;
    clear_pointer_targets(view);
    reconcile_page(view);
    return 1;
}

size_t oc_dashboard_visible_game(const oc_dashboard_view *view, size_t slot) {
    size_t result;
    if (view == 0 || slot >= view->visible_count) return OC_DASHBOARD_NO_INDEX;
    result = view->page * view->page_capacity + slot;
    if (result >= view->result_count) return OC_DASHBOARD_NO_INDEX;
    return view->result_indices[result];
}

size_t oc_dashboard_selected_game(const oc_dashboard_view *view) {
    if (view == 0 || view->result_count == 0u ||
        view->selected_result >= view->result_count) return (size_t)-1;
    return view->result_indices[view->selected_result];
}

int oc_dashboard_layout_cards(size_t width, size_t height, size_t result_count,
                              oc_dashboard_layout *layout) {
    size_t available;
    size_t total;
    if (layout == 0 || width < 640u || height < 480u) return 0;
    layout->gap = width >= 900u ? 20u : 14u;
    layout->row_gap = height >= 700u ? 48u : 38u;
    layout->card_side = height >= 700u ? 132u : 104u;
    layout->card_y = height >= 700u ? 252u : 220u;
    available = width > 160u ? width - 160u : width;
    layout->capacity = (available + layout->gap) /
        (layout->card_side + layout->gap);
    if (layout->capacity == 0u) layout->capacity = 1u;
    layout->columns = layout->capacity;
    if (layout->columns > 5u) layout->columns = 5u;
    layout->capacity = layout->columns * 2u;
    if (layout->capacity > OC_DASHBOARD_MAX_VISIBLE) layout->capacity = OC_DASHBOARD_MAX_VISIBLE;
    if (result_count > layout->columns) result_count = layout->columns;
    if (result_count == 0u) {
        layout->start_x = width / 2u;
        return 1;
    }
    available = width > 80u ? width - 80u : width;
    if (result_count * layout->card_side + (result_count - 1u) * layout->gap > available) {
        layout->card_side = (available - (result_count - 1u) * layout->gap) / result_count;
        if (layout->card_side < 72u) layout->card_side = 72u;
    }
    total = result_count * layout->card_side + (result_count - 1u) * layout->gap;
    layout->start_x = total < width ? (width - total) / 2u : 0u;
    return layout->card_y + 2u * layout->card_side + layout->row_gap + 54u < height;
}

int oc_dashboard_apply_layout(oc_dashboard_view *view, size_t width, size_t height,
                              oc_dashboard_layout *layout) {
    size_t slot;
    size_t old_capacity;
    size_t old_page;
    if (view == 0 || !oc_dashboard_layout_cards(width, height,
                                                 view->result_count, layout)) return 0;
    old_capacity = view->page_capacity;
    old_page = view->page;
    view->page_capacity = layout->capacity;
    reconcile_page(view);
    for (slot = 0u; slot < OC_DASHBOARD_MAX_VISIBLE; ++slot) {
        view->card_rects[slot].width = 0u;
        view->card_rects[slot].height = 0u;
        view->info_rects[slot].width = 0u;
        view->info_rects[slot].height = 0u;
    }
    for (slot = 0u; slot < view->visible_count; ++slot) {
        size_t column = slot % layout->columns;
        size_t row = slot / layout->columns;
        view->card_rects[slot].x = layout->start_x +
            column * (layout->card_side + layout->gap) - 6u;
        view->card_rects[slot].y = layout->card_y +
            row * (layout->card_side + layout->row_gap) - 6u;
        view->card_rects[slot].width = layout->card_side + 12u;
        view->card_rects[slot].height = layout->card_side + 44u;
        view->info_rects[slot].x = view->card_rects[slot].x +
            view->card_rects[slot].width - 26u;
        view->info_rects[slot].y = view->card_rects[slot].y +
            view->card_rects[slot].height - 27u;
        view->info_rects[slot].width = 22u;
        view->info_rects[slot].height = 22u;
    }
    view->previous_rect = (oc_dashboard_rect){36u, 184u, 92u, 36u};
    view->next_rect = (oc_dashboard_rect){width > 128u ? width - 128u : 0u,
                                          184u, 92u, 36u};
    if (old_capacity != view->page_capacity || old_page != view->page)
        clear_pointer_targets(view);
    view->hover = oc_dashboard_hit_test(view);
    return 1;
}

static int64_t scale_motion(int32_t motion, uint64_t resolution) {
    int64_t scaled;
    if (motion == 0) return 0;
    if (resolution == 0u || resolution > 1000000000u) return motion;
    scaled = ((int64_t)motion * 4ll) / (int64_t)resolution;
    if (scaled == 0) scaled = motion < 0 ? -1 : 1;
    return scaled;
}

void oc_dashboard_move_pointer(oc_dashboard_view *view, int32_t relative_x,
                               int32_t relative_y, uint64_t resolution_x,
                               uint64_t resolution_y, size_t width, size_t height) {
    int64_t x, y, dx, dy;
    if (view == 0 || width == 0u || height == 0u) return;
    dx = scale_motion(relative_x, resolution_x);
    dy = scale_motion(relative_y, resolution_y);
    x = (int64_t)view->cursor_x + dx;
    y = (int64_t)view->cursor_y + dy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint64_t)x >= width) x = (int64_t)(width - 1u);
    if ((uint64_t)y >= height) y = (int64_t)(height - 1u);
    view->cursor_x = (size_t)x;
    view->cursor_y = (size_t)y;
    view->hover = oc_dashboard_hit_test(view);
}

oc_dashboard_target oc_dashboard_hit_test(const oc_dashboard_view *view) {
    oc_dashboard_target target = {OC_DASHBOARD_TARGET_NONE, OC_DASHBOARD_NO_INDEX};
    size_t slot;
    if (view == 0 || !view->cursor_visible) return target;
    for (slot = 0u; slot < view->visible_count; ++slot) {
        if (point_in_rect(view->cursor_x, view->cursor_y, &view->info_rects[slot])) {
            target.kind = OC_DASHBOARD_TARGET_INFO;
            target.result_index = view->page * view->page_capacity + slot;
            return target;
        }
        if (point_in_rect(view->cursor_x, view->cursor_y, &view->card_rects[slot])) {
            target.kind = OC_DASHBOARD_TARGET_CARD;
            target.result_index = view->page * view->page_capacity + slot;
            return target;
        }
    }
    if (view->page > 0u && point_in_rect(view->cursor_x, view->cursor_y,
                                          &view->previous_rect))
        target.kind = OC_DASHBOARD_TARGET_PREVIOUS;
    else if (view->page + 1u < view->page_count &&
             point_in_rect(view->cursor_x, view->cursor_y, &view->next_rect))
        target.kind = OC_DASHBOARD_TARGET_NEXT;
    return target;
}

size_t oc_dashboard_pointer_button(oc_dashboard_view *view, int left_button,
                                   int *page_changed) {
    oc_dashboard_target current;
    size_t game = OC_DASHBOARD_NO_INDEX;
    if (page_changed != 0) *page_changed = 0;
    if (view == 0) return game;
    current = oc_dashboard_hit_test(view);
    view->hover = current;
    if (left_button && !view->left_button) {
        view->pressed = current;
        if (current.kind == OC_DASHBOARD_TARGET_CARD &&
            current.result_index < view->result_count)
            view->selected_result = current.result_index;
    } else if (!left_button && view->left_button) {
        if (targets_equal(current, view->pressed)) {
            if (current.kind == OC_DASHBOARD_TARGET_CARD &&
                current.result_index < view->result_count)
                game = view->result_indices[current.result_index];
            else if (current.kind == OC_DASHBOARD_TARGET_INFO &&
                     current.result_index < view->result_count)
                view->details_game = view->result_indices[current.result_index];
            else if (current.kind == OC_DASHBOARD_TARGET_PREVIOUS) {
                if (oc_dashboard_change_page(view, -1) && page_changed != 0)
                    *page_changed = 1;
            } else if (current.kind == OC_DASHBOARD_TARGET_NEXT) {
                if (oc_dashboard_change_page(view, 1) && page_changed != 0)
                    *page_changed = 1;
            }
        }
        view->pressed.kind = OC_DASHBOARD_TARGET_NONE;
        view->pressed.result_index = OC_DASHBOARD_NO_INDEX;
    }
    view->left_button = left_button ? 1u : 0u;
    return game;
}
