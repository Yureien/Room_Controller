#ifndef POWER_CONFIG_H_
#define POWER_CONFIG_H_

// CPU scaling/automatic light sleep are opt-in; radio coexistence is separate.
// AC-powered default. Opt in with -DROOM_CONTROLLER_POWER_SAVING=1.
#ifndef ROOM_CONTROLLER_POWER_SAVING
#define ROOM_CONTROLLER_POWER_SAVING 0
#endif
#if ROOM_CONTROLLER_POWER_SAVING != 0 && ROOM_CONTROLLER_POWER_SAVING != 1
#error "ROOM_CONTROLLER_POWER_SAVING must be 0 or 1"
#endif

#endif
