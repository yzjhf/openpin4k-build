/*
 * OpenPin4K -- joystick discovery logger ("jstest").  -> "jstest.elf".
 *
 * WHY THIS EXISTS
 *   On the cabinet VPX detects the joystick "ATG game console #1" (16 buttons) but
 *   flippers / plunger / Start do NOTHING: VPX's auto-layout doesn't match the
 *   cabinet's physical buttons, and we do NOT know which SDL button INDEX each
 *   physical control is. Guessing blind wastes scarce cabinet trips, so this tool
 *   captures the indices directly.
 *
 * WHAT IT DOES
 *   Opens every SDL joystick and, for ~DISCOVER_SECONDS, logs each button / axis /
 *   hat event using the *exact same numeric encoding VPX uses* (verified against
 *   vpinball src/input/SDLInputHandler.h @ c446c2f):
 *       button id = raw button index
 *       axis   id = 0x0200 | axis
 *       hat    id = 0x0100 | (hat*4 + dir)   dir order: 0=L 1=R 2=U 3=D
 *   and prints, per device, the VPX device-settings-id string
 *       SDLJoy_<33charGUID>_<enumIndex>
 *   (VPX builds this in SDLInputHandler.h as "SDLJoy_" + SDL_GUIDToString + '_' + index).
 *
 *   So the operator presses the physical controls in a known order and we read the
 *   ids straight out of vpx-log.txt, then write the matching
 *       [Input]
 *       Mapping.LeftFlipper  = SDLJoy_<guid>_<idx>;<id>
 *       Mapping.RightFlipper = ...
 *   lines into VPinballX.ini. No guessing.
 *
 * Output goes to stdout (the launch wrapper redirects that into vpx-log.txt and
 * mirrors it to the USB). This program prints, it does not render -- the operator
 * follows a written press-order, and we correlate by the order/timestamps logged.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define DISCOVER_SECONDS 30
#define AXIS_ON  20000   /* |value| over this = "pressed/engaged" (SDL axis is -32768..32767) */
#define AXIS_OFF 12000   /* drop below this = "released" (hysteresis so we log one event, not many) */

int main(void)
{
    if (!SDL_Init(SDL_INIT_JOYSTICK)) {
        printf("[jstest] SDL_Init(JOYSTICK) FAILED: %s\n", SDL_GetError());
        fflush(stdout);
        return 1;
    }

    int v = SDL_GetVersion();
    printf("==== OpenPin4K joystick discovery (jstest) ====\n");
    printf("[jstest] SDL runtime version %d.%d.%d\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));

    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    printf("[jstest] found %d joystick(s):\n", count);

    /* axis "engaged" state per (device-enum-index, axis) so we log edges only */
    static unsigned char axis_engaged[16][32];
    memset(axis_engaged, 0, sizeof axis_engaged);

    if (ids) {
        for (int i = 0; i < count; i++) {
            SDL_Joystick *js = SDL_OpenJoystick(ids[i]);
            if (!js) { printf("  [enumIndex=%d] OPEN FAILED: %s\n", i, SDL_GetError()); continue; }
            const char *name = SDL_GetJoystickName(js);
            SDL_GUID guid = SDL_GetJoystickGUID(js);
            char gs[33]; gs[0] = 0;
            SDL_GUIDToString(guid, gs, sizeof gs);
            int nb = SDL_GetNumJoystickButtons(js);
            int na = SDL_GetNumJoystickAxes(js);
            int nh = SDL_GetNumJoystickHats(js);
            printf("  [enumIndex=%d] instanceId=%u  name=\"%s\"  buttons=%d axes=%d hats=%d\n",
                   i, (unsigned)ids[i], name ? name : "(null)", nb, na, nh);
            printf("                VPX deviceSettingsId =  SDLJoy_%s_%d\n", gs, i);
        }
    }

    printf("\n[jstest] PRESS THE CABINET CONTROLS NOW -- you have %d seconds.\n", DISCOVER_SECONDS);
    printf("[jstest] Suggested order (hold ~1s, pause ~2s between each):\n");
    printf("[jstest]   1) LEFT flipper   2) RIGHT flipper   3) PLUNGER / LAUNCH\n");
    printf("[jstest]   4) START   5) LEFT nudge   6) RIGHT nudge   7) any others (coin, menu...)\n");
    printf("---- events (id values below are exactly what goes after the ';' in a Mapping line) ----\n");
    fflush(stdout);

    Uint64 start = SDL_GetTicks();
    SDL_Event e;
    for (;;) {
        Uint64 now = SDL_GetTicks();
        double t = (now - start) / 1000.0;
        if (t >= DISCOVER_SECONDS) break;

        while (SDL_PollEvent(&e)) {
            /* map instance id -> our enum index (for the axis-state table + clarity) */
            switch (e.type) {
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                printf("[t=%5.1fs] which=%u  BUTTON DOWN  button=%d   ->  Mapping id: %d\n",
                       t, (unsigned)e.jbutton.which, e.jbutton.button, e.jbutton.button);
                fflush(stdout);
                break;
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                printf("[t=%5.1fs] which=%u  button UP    button=%d\n",
                       t, (unsigned)e.jbutton.which, e.jbutton.button);
                fflush(stdout);
                break;
            case SDL_EVENT_JOYSTICK_HAT_MOTION: {
                int hat = e.jhat.hat, val = e.jhat.value;
                /* SDL hat: UP=1 RIGHT=2 DOWN=4 LEFT=8 ; VPX dir order L,R,U,D = +0,+1,+2,+3 */
                const struct { int bit; int dir; const char *nm; } D[] = {
                    {SDL_HAT_LEFT,0,"LEFT"},{SDL_HAT_RIGHT,1,"RIGHT"},{SDL_HAT_UP,2,"UP"},{SDL_HAT_DOWN,3,"DOWN"} };
                for (int k = 0; k < 4; k++) if (val & D[k].bit) {
                    int id = 0x0100 | (hat*4 + D[k].dir);
                    printf("[t=%5.1fs] which=%u  HAT %s (hat=%d)  ->  Mapping id: %d (0x%04X)\n",
                           t, (unsigned)e.jhat.which, D[k].nm, hat, id, id);
                }
                fflush(stdout);
                break;
            }
            case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
                int axis = e.jaxis.axis, val = e.jaxis.value;
                if (axis < 0 || axis >= 32) break;
                int eng = axis_engaged[0][axis];   /* single-device-common table; fine for our 1-2 pads */
                int id = 0x0200 | axis;
                if (!eng && (val > AXIS_ON || val < -AXIS_ON)) {
                    axis_engaged[0][axis] = 1;
                    printf("[t=%5.1fs] which=%u  AXIS axis=%d ENGAGED value=%d  ->  Mapping id: %d (0x%04X)%s\n",
                           t, (unsigned)e.jaxis.which, axis, val, id, id, (val < 0) ? "  (negative -> reversed: append ;x;0.5)" : "");
                    fflush(stdout);
                } else if (eng && val < AXIS_OFF && val > -AXIS_OFF) {
                    axis_engaged[0][axis] = 0;   /* released; stay quiet */
                }
                break;
            }
            case SDL_EVENT_JOYSTICK_ADDED:
                printf("[t=%5.1fs] (joystick added, instanceId=%u)\n", t, (unsigned)e.jdevice.which);
                fflush(stdout);
                break;
            default: break;
            }
        }
        SDL_Delay(10);
    }

    printf("---- end of discovery window ----\n");
    fflush(stdout);
    if (ids) SDL_free(ids);
    SDL_Quit();
    return 0;
}
