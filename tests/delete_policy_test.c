#include "openconsole/delete_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_fault(oc_delete_stage failed_stage) {
    oc_delete_guard guard;
    oc_delete_stage stage;
    oc_delete_guard_init(&guard, 8u, 1024u);
    for (stage = OC_DELETE_REVALIDATE; stage < OC_DELETE_STAGE_COUNT; ++stage) {
        int okay = stage != failed_stage;
        if (!oc_delete_guard_stage(&guard, stage, okay)) break;
    }
    assert(guard.failed && !guard.completed);
}

int main(void) {
    oc_delete_guard guard;
    oc_delete_stage stage;
    assert(oc_delete_identity_valid("game", "game", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid(".", ".", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid("..", "..", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid("../game", "../game", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid("a/b", "a/b", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid("a\\b", "a\\b", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid("game", "other", 7u, 7u, 1, 0));
    assert(!oc_delete_identity_valid("game", "game", 7u, 8u, 1, 0));
    assert(!oc_delete_identity_valid("game", "game", 7u, 7u, 0, 0));
    assert(!oc_delete_identity_valid("game", "game", 7u, 7u, 1, 1));

    oc_delete_guard_init(&guard, 1u, 2u);
    assert(oc_delete_guard_visit(&guard, 0u));
    assert(oc_delete_guard_visit(&guard, 1u));
    assert(!oc_delete_guard_visit(&guard, 1u));
    oc_delete_guard_init(&guard, 1u, 3u);
    assert(!oc_delete_guard_visit(&guard, 2u));

    for (stage = OC_DELETE_REVALIDATE; stage < OC_DELETE_STAGE_COUNT; ++stage)
        test_fault(stage);
    oc_delete_guard_init(&guard, 8u, 1024u);
    for (stage = OC_DELETE_REVALIDATE; stage < OC_DELETE_STAGE_COUNT; ++stage)
        assert(oc_delete_guard_stage(&guard, stage, 1));
    assert(guard.completed && !guard.failed);
    puts("PASS: confined delete identity, bounds, and injected-stage failures");
    return 0;
}
