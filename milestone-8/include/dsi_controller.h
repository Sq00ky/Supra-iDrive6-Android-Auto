// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef DSI_CONTROLLER_H
#define DSI_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reverse-Engineered DSI Central Control System Definitions
 * Source: CHmiCCSController.cpp / CPxyCentralControlSystem35up
 * Process: HmiMain3D (QNX Neutrino)
 */

/* DSI Service and Instance Identifiers */
#define DSI_SERVICE_ID_CENTRAL_CONTROL_35UP  0x00E5  /* 229 */
#define DSI_INSTANCE_ID_NBT_EVO_TC_HU        0x10    /* 16 - Touch Controller Head Unit */
#define DSI_TRANSPORT_MODE_IPC               8       /* QNX Message Passing to servicebroker */

/* Physical ZBE Controller KeyCodes */
typedef enum {
    /* Peripheral Hard Keys */
    ZBE_KEY_NONE                = 0x00,
    ZBE_KEY_NAV                 = 0x01, /* Navigation / Map */
    ZBE_KEY_MEDIA               = 0x02, /* Media Source */
    ZBE_KEY_RADIO               = 0x03, /* Radio Source */
    ZBE_KEY_TEL                 = 0x04, /* Telephone / Call */
    ZBE_KEY_MENU_HOME           = 0x05, /* Main Menu / Home */
    ZBE_KEY_BACK                = 0x06, /* Back */
    ZBE_KEY_OPTION              = 0x07, /* Option / Context Menu */

    /* 4-Way Joystick Nudges / Tilts */
    ZBE_KEY_TILT_UP             = 0x09, /* Joystick Tilt North / Up */
    ZBE_KEY_TILT_DOWN           = 0x0A, /* Joystick Tilt South / Down */
    ZBE_KEY_TILT_LEFT           = 0x0B, /* Joystick Tilt West / Left */
    ZBE_KEY_TILT_RIGHT          = 0x0C, /* Joystick Tilt East / Right */

    /* Rotary Encoder Turns */
    ZBE_KEY_ROTATE_CW           = 0x0D, /* Rotate Clockwise (Turn Right) */
    ZBE_KEY_ROTATE_CCW          = 0x0E, /* Rotate Counter-Clockwise (Turn Left) */

    /* Center Push */
    ZBE_KEY_CENTER_PUSH         = 0x0F, /* Knob Press Down (Enter / Select) */

    /* Additional Function Keys */
    ZBE_KEY_SPECIAL_1           = 0x11,
    ZBE_KEY_SPECIAL_2           = 0x12
} zbe_keycode_t;

/* Touchpad Coordinate Constraints */
#define ZBE_TOUCH_MAX_X         4095
#define ZBE_TOUCH_MAX_Y         4095

/* Event Payload Structure */
typedef struct {
    uint32_t key_code;          /* zbe_keycode_t */
    bool     pressed;           /* true = pressed, false = released */
    bool     long_pressed;      /* true = held down */
    int32_t  repeat_count;      /* Rotation steps / tick count for CW/CCW */
} zbe_button_event_t;

typedef struct {
    uint16_t x;                 /* 0..4095 */
    uint16_t y;                 /* 0..4095 */
    uint8_t  finger_id;         /* 0 or 1 */
    uint8_t  gesture_phase;     /* Touch phase */
} zbe_touch_event_t;

/* Callback function pointer types */
typedef void (*zbe_button_callback_t)(const zbe_button_event_t* event, void* user_data);
typedef void (*zbe_touch_callback_t)(const zbe_touch_event_t* event, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* DSI_CONTROLLER_H */
