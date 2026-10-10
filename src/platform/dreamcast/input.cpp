// input.cpp - Dreamcast maple controller backend.
//
// Counterpart of src/platform/linux/input.cpp. Publishes the same 32-bit mask
// into joysticks[0] (docs/GAMEPAD_INPUT.md):
//   bits 0-3  = up, down, left, right   (d-pad or analog stick)
//   bits 8-19 = buttons, MARNI_XI_BTN_* - the XInput layout, so the port's
//               InstallPadDefaultBindings applies unchanged
//
// DC pad -> XInput slot (and so -> game function with the default bindings):
//   A      -> A        action / confirm
//   B      -> B        cancel / run
//   X      -> X        aim
//   Y      -> Y        inventory
//   L trig -> LT       run
//   R trig -> RT       aim
//   Start  -> BACK     options screen  (the DC pad has no spare button; Y
//                      already opens the inventory, so Start takes Back's job)
#include "Globals.h"
#include "platform/platform.h"
#include "marni/MarniInput.h"
#include "marni/MarniXInput.h"

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

static const int kStickDeadzone   = 40;    // of +-128
static const int kTriggerThreshold = 77;   // ~30/255 scaled like XInput's

static maple_device_t* FindPad(void)
{
    return maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
}

static DWORD BuildPadMask(maple_device_t* dev)
{
    if (dev == NULL) return 0;
    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (st == NULL) return 0;

    DWORD mask = 0;
    const uint32_t b = st->buttons;

    if ((b & CONT_DPAD_UP)    || st->joyy < -kStickDeadzone) mask |= 1;
    if ((b & CONT_DPAD_DOWN)  || st->joyy >  kStickDeadzone) mask |= 2;
    if ((b & CONT_DPAD_LEFT)  || st->joyx < -kStickDeadzone) mask |= 4;
    if ((b & CONT_DPAD_RIGHT) || st->joyx >  kStickDeadzone) mask |= 8;

    // UP beats DOWN, LEFT beats RIGHT (PlayerPad_Update's post-pass).
    if (mask & 1) mask &= ~2u;
    if (mask & 4) mask &= ~8u;

    if (b & CONT_A)     mask |= MARNI_XI_BTN_A;
    if (b & CONT_B)     mask |= MARNI_XI_BTN_B;
    if (b & CONT_X)     mask |= MARNI_XI_BTN_X;
    if (b & CONT_Y)     mask |= MARNI_XI_BTN_Y;
    if (b & CONT_START) mask |= MARNI_XI_BTN_BACK;

    if (st->ltrig > kTriggerThreshold) mask |= MARNI_XI_BTN_LTRIGGER;
    if (st->rtrig > kTriggerThreshold) mask |= MARNI_XI_BTN_RTRIGGER;

    return mask;
}

// ---------------------------------------------------------------------------
// CMarniDirectInput
// ---------------------------------------------------------------------------

void CMarniDirectInput::UpdateKeyboardInputState(MasterInputState* pState)
{
    // 0x004202f0. A maple keyboard can be added later by feeding
    // plat_key_event(vk, down); until then every key reads up.
    pState->keyboardCurr = 0;
    for (int i = 0; i < 32; i++) {
        if (plat_key_state(pState->keyMap[i]) & 0x8000) {
            pState->keyboardCurr |= (1u << i);
        }
    }
}

void CMarniDirectInput::UpdateAllInputStates(MasterInputState* pState)
{
    UpdateKeyboardInputState(pState);

    DWORD oldPrev = pState->keyboardPrev;
    pState->keyboardRepeat   = oldPrev;
    pState->keyboardPrev     = pState->keyboardCurr;
    pState->frameFlag        = 1;
    pState->keyboardNewPress = (~oldPrev) & pState->keyboardCurr;

    // Maple re-enumerates hot-plugged devices itself; just look every frame.
    maple_device_t* dev = FindPad();
    JoystickEntry* pJoy = &pState->joysticks[0];
    DWORD padMask = BuildPadMask(dev);

    if (dev != NULL) {
        pJoy->enabled   = 1;
        pJoy->prevPress = pJoy->currPress;
        pJoy->currPress = padMask;
        pJoy->newPress  = (~pJoy->prevPress) & padMask;
    } else {
        pJoy->enabled   = 0;
        pJoy->prevPress = 0;
        pJoy->currPress = 0;
        pJoy->newPress  = 0;
    }
}

void CMarniDirectInput::InitJoysticks(MasterInputState* pState)
{
    pState->joystickCount = (FindPad() != NULL) ? 1u : 0u;
}

bool MarniPadIsConnected(void)
{
    return FindPad() != NULL;
}

namespace MarniXInput {
bool IsConnected() { return FindPad() != NULL; }
}
