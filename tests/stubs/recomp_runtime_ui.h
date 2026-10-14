#pragma once
// Minimal stand-in for recomp-ui's "recomp_runtime_ui.h": only the declarations that
// gbaimp/runtime_ui_settings.h uses, so the adapter can be compiled and tested without the
// recomp-ui checkout. The layouts mirror the real header.
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RecompRuntimeUiItemType {
    RECOMP_RUNTIME_UI_BOOL,
    RECOMP_RUNTIME_UI_INT,
    RECOMP_RUNTIME_UI_CHOICE,
    RECOMP_RUNTIME_UI_ACTION,
    RECOMP_RUNTIME_UI_TEXT,
} RecompRuntimeUiItemType;

typedef struct RecompRuntimeUiItem {
    const char *key;
    const char *section;
    const char *label;
    const char *description;
    RecompRuntimeUiItemType type;
    int minimum;
    int maximum;
    int step;
    const char *const *choices;
    size_t choice_count;
    const int *choice_values;
} RecompRuntimeUiItem;

typedef struct RecompRuntimeUi RecompRuntimeUi;

int recomp_runtime_ui_is_open(const RecompRuntimeUi *ui);
void recomp_runtime_ui_open(RecompRuntimeUi *ui);
void recomp_runtime_ui_close(RecompRuntimeUi *ui);

#ifdef __cplusplus
}
#endif
