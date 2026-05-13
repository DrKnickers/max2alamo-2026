// Resource IDs for the Alamo Utility command-panel UI.
//
// The legacy Petroglyph max2alamo plugin shipped this UI as the
// authoring side of the Alamo_* node user-property family: a Utility
// in the command panel's Utilities tab with three rollouts (Node
// Export Options, Quick Selection Utility, Animation Settings).
// Selecting a node populates Node Export Options' checkboxes from
// that node's Alamo_* user properties; toggling them writes the
// properties back. The walker reads the same properties at export.
//
// Layout convention: dialog templates are 108 dialog units wide
// (the Max command-panel column width).

#pragma once

// Standard Win32 "no ID needed" placeholder used by static labels.
#ifndef IDC_STATIC
#define IDC_STATIC                      (-1)
#endif

// ---- Dialog templates -----------------------------------------------------

#define IDD_ALAMO_NODE_EXPORT_OPTIONS   3001
#define IDD_ALAMO_QUICK_SELECTION       3002
#define IDD_ALAMO_ANIMATION_SETTINGS    3003

// ---- Node Export Options controls -----------------------------------------

#define IDC_NODE_EXPORT_TRANSFORM       3100
#define IDC_NODE_IS_EXTRA_BONE          3101
#define IDC_BILLBOARD_GROUP             3102
#define IDC_BILLBOARD_HELP              3103
#define IDC_BILLBOARD_DISABLE           3110
#define IDC_BILLBOARD_PARALLEL          3111
#define IDC_BILLBOARD_FACE              3112
#define IDC_BILLBOARD_ZAXIS_VIEW        3113
#define IDC_BILLBOARD_ZAXIS_LIGHT       3114
#define IDC_BILLBOARD_ZAXIS_WIND        3115
#define IDC_BILLBOARD_SUNLIGHT_GLOW     3116
#define IDC_BILLBOARD_SUN               3117

#define IDC_NODE_EXPORT_GEOMETRY        3120
#define IDC_NODE_ENABLE_COLLISION       3121
#define IDC_NODE_HIDDEN                 3122
#define IDC_NODE_ALT_DEC_STAY_HIDDEN    3123

// ---- Quick Selection Utility controls -------------------------------------

#define IDC_QS_EXPORT_TRANSFORM         3200
#define IDC_QS_EXPORT_GEOMETRY          3201
#define IDC_QS_ENABLE_COLLISION         3202
#define IDC_QS_LOD_EDIT                 3210
#define IDC_QS_LOD_SPIN                 3211
#define IDC_QS_LOD_HELP                 3212
#define IDC_QS_ALT_EDIT                 3213
#define IDC_QS_ALT_SPIN                 3214
#define IDC_QS_ALT_HELP                 3215

// ---- Alamo Proxy Modify-panel rollout controls ---------------------------

#define IDD_ALAMO_PROXY_ROLLOUT         3400
#define IDC_PROXY_DESC                  3401
#define IDC_PROXY_HIDDEN                3402
#define IDC_PROXY_ALT_DEC_STAY_HIDDEN   3403

// ---- Animation Settings controls ------------------------------------------

#define IDC_ANIM_NAME_COMBO             3300
#define IDC_ANIM_START_EDIT             3301
#define IDC_ANIM_START_SPIN             3302
#define IDC_ANIM_END_EDIT               3303
#define IDC_ANIM_END_SPIN               3304
#define IDC_ANIM_PREV                   3305
#define IDC_ANIM_ADD                    3306
#define IDC_ANIM_DEL                    3307
#define IDC_ANIM_NEXT                   3308
#define IDC_ANIM_DISPLAY_CURRENT        3309
#define IDC_ANIM_DISPLAY_ALL            3310
