#include "openconsole/dashboard.h"
#include "openconsole/uefi.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

_Static_assert(sizeof(EFI_SIMPLE_POINTER_STATE) == 16u, "pointer state ABI");
_Static_assert(offsetof(EFI_SIMPLE_POINTER_STATE, left_button) == 12u, "state buttons ABI");
_Static_assert(sizeof(EFI_SIMPLE_POINTER_MODE) == 32u, "pointer mode ABI");
_Static_assert(offsetof(EFI_SIMPLE_POINTER_MODE, left_button) == 24u, "mode buttons ABI");
_Static_assert(sizeof(EFI_SIMPLE_POINTER_PROTOCOL) == 32u, "pointer protocol ABI");
_Static_assert(offsetof(EFI_SIMPLE_POINTER_PROTOCOL, wait_for_input) == 16u, "event ABI");
_Static_assert(offsetof(EFI_SIMPLE_POINTER_PROTOCOL, mode) == 24u, "mode pointer ABI");

static void place_pointer(oc_dashboard_view *view, size_t x, size_t y) {
    view->cursor_visible = 1u;
    view->cursor_x = x;
    view->cursor_y = y;
    view->hover = oc_dashboard_hit_test(view);
}

int main(void) {
    const char *titles[] = {
        "Library Game 00", "Library Game 01", "Library Game 02",
        "Library Game 03", "Library Game 04", "Library Game 05",
        "Library Game 06", "Library Game 07", "Library Game 08",
        "Library Game 09", "Library Game 10", "Library Game 11"
    };
    oc_dashboard_view view = {0};
    oc_dashboard_layout layout;
    size_t activated;
    int page_changed;

    oc_dashboard_rebuild(&view, titles, 12u);
    assert(view.result_count == 12u);
    assert(oc_dashboard_apply_layout(&view, 1024u, 768u, &layout));
    assert(layout.capacity == 10u);
    assert(view.page_count == 2u && view.visible_count == 10u);
    assert(oc_dashboard_visible_game(&view, 0u) == 0u);
    assert(oc_dashboard_visible_game(&view, 9u) == 9u);
    assert(oc_dashboard_visible_game(&view, 10u) == OC_DASHBOARD_NO_INDEX);

    oc_dashboard_move(&view, -1);
    assert(view.selected_result == 11u && view.page == 1u);
    oc_dashboard_move(&view, 1);
    assert(view.selected_result == 0u && view.page == 0u);
    assert(oc_dashboard_change_page(&view, 1));
    assert(view.page == 1u && view.selected_result == 10u);
    assert(oc_dashboard_visible_game(&view, 0u) == 10u);

    assert(oc_dashboard_edit(&view, '0', titles, 12u));
    assert(oc_dashboard_edit(&view, '9', titles, 12u));
    assert(view.page == 0u && view.page_count == 1u && view.result_count == 1u);
    assert(oc_dashboard_selected_game(&view) == 9u);
    assert(view.pressed.kind == OC_DASHBOARD_TARGET_NONE);
    assert(oc_dashboard_edit(&view, 27u, titles, 12u));
    assert(view.result_count == 12u && view.page == 0u);
    assert(oc_dashboard_apply_layout(&view, 1024u, 768u, &layout));

    place_pointer(&view, view.info_rects[0].x + 2u, view.info_rects[0].y + 2u);
    assert(view.hover.kind == OC_DASHBOARD_TARGET_INFO);
    activated = oc_dashboard_pointer_button(&view, 1, &page_changed);
    assert(activated == OC_DASHBOARD_NO_INDEX);
    activated = oc_dashboard_pointer_button(&view, 0, &page_changed);
    assert(activated == OC_DASHBOARD_NO_INDEX);
    assert(view.details_game == 0u);
    view.details_game = OC_DASHBOARD_NO_INDEX;

    view.cursor_visible = 1u;
    view.cursor_x = 512u;
    view.cursor_y = 384u;
    oc_dashboard_move_pointer(&view, 100, -100, 4u, 4u, 1024u, 768u);
    assert(view.cursor_x == 612u && view.cursor_y == 284u);
    oc_dashboard_move_pointer(&view, 1, 1, 100000u, 100000u, 1024u, 768u);
    assert(view.cursor_x == 613u && view.cursor_y == 285u);
    oc_dashboard_move_pointer(&view, INT32_MAX, INT32_MAX, 0u, 0u, 1024u, 768u);
    assert(view.cursor_x == 1023u && view.cursor_y == 767u);
    oc_dashboard_move_pointer(&view, INT32_MIN, INT32_MIN, 0u, 0u, 1024u, 768u);
    assert(view.cursor_x == 0u && view.cursor_y == 0u);

    place_pointer(&view, view.card_rects[2].x + 8u, view.card_rects[2].y + 8u);
    assert(view.hover.kind == OC_DASHBOARD_TARGET_CARD);
    activated = oc_dashboard_pointer_button(&view, 1, &page_changed);
    assert(activated == OC_DASHBOARD_NO_INDEX && view.selected_result == 2u);
    activated = oc_dashboard_pointer_button(&view, 0, &page_changed);
    assert(activated == 2u && !page_changed);

    place_pointer(&view, view.card_rects[1].x + 8u, view.card_rects[1].y + 8u);
    (void)oc_dashboard_pointer_button(&view, 1, &page_changed);
    place_pointer(&view, 2u, 2u);
    activated = oc_dashboard_pointer_button(&view, 0, &page_changed);
    assert(activated == OC_DASHBOARD_NO_INDEX);

    place_pointer(&view, view.next_rect.x + 4u, view.next_rect.y + 4u);
    (void)oc_dashboard_pointer_button(&view, 1, &page_changed);
    activated = oc_dashboard_pointer_button(&view, 0, &page_changed);
    assert(activated == OC_DASHBOARD_NO_INDEX && page_changed && view.page == 1u);
    assert(oc_dashboard_visible_game(&view, 0u) == 10u);

    assert(oc_dashboard_edit(&view, 'z', titles, 12u));
    assert(view.result_count == 0u && view.page_count == 0u);
    assert(oc_dashboard_selected_game(&view) == OC_DASHBOARD_NO_INDEX);
    place_pointer(&view, view.next_rect.x + 4u, view.next_rect.y + 4u);
    assert(oc_dashboard_pointer_button(&view, 1, &page_changed) == OC_DASHBOARD_NO_INDEX);
    assert(oc_dashboard_pointer_button(&view, 0, &page_changed) == OC_DASHBOARD_NO_INDEX);

    assert(!oc_dashboard_layout_cards(320u, 200u, 1u, &layout));
    puts("PASS: pointer ABI, motion, hit testing, filtered pagination, and mapping");
    return 0;
}
