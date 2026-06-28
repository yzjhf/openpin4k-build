# OpenPin4K — input discovery + mapping reference

Ground truth pulled verbatim from `vpinball/vpinball` @ `c446c2f` (`src/input/`), so the
mapping lines we write are exact, not guessed.

## How VPX identifies a controller and its buttons

VPX opens the pad as a **raw `SDL_Joystick`** (NOT as an `SDL_Gamepad`) and handles
`SDL_EVENT_JOYSTICK_BUTTON_DOWN/UP`, `..._AXIS_MOTION`, `..._HAT_MOTION`
(`SDLInputHandler.h`). Numeric ids it stores:

| input | id encoding |
|-------|-------------|
| button | raw `e.jbutton.button` (0,1,2,…) |
| axis   | `0x0200 \| axis`  (e.g. axis2 → `0x0202` = 514) |
| hat    | `0x0100 \| (hat*4 + dir)`, dir order **L=0,R=1,U=2,D=3** |

Device settings-id string (built in `SDLInputHandler.h`):
```
SDLJoy_<33-char-GUID>_<enumIndex>          e.g. SDLJoy_0300...0000_0
```
GUID from `SDL_GUIDToString`; index = enumeration order.

## Mapping string format (`InputAction.cpp`)

Setting key: `Mapping.<actionId>` in the **`[Input]`** section.
Value grammar:
```
alt1 | alt2                      # '|' = OR (either binding triggers the action)
binding & binding                # '&' = AND (chord, both at once)
binding =  <deviceSettingsId>;<id>[;<reversal>;<threshold>]
                                 #  reversal: 'x' reversed / 'o' normal ; threshold float
```
Example: `Mapping.LeftFlipper = SDLJoy_0300...0000_0;4`
Analog plunger on a reversed axis: `...;0x0202;x;0.5` (logger flags negative axes).

## Action setting-ids (from `InputManager.cpp`)

| physical control | actionId | keyboard default |
|---|---|---|
| left flipper | `LeftFlipper` | LShift |
| right flipper | `RightFlipper` | RShift |
| (staged) | `LeftStagedFlipper` / `RightStagedFlipper` | |
| plunger / launch ball | `LaunchBall` | Enter |
| start | `Start` | 1 |
| add credit / coin | `Credit1` (5), `Credit2` (4) | |
| coin door | `CoinDoor` | End |
| left/right/center nudge | `LeftNudge` `RightNudge` `CenterNudge` | Z / / / Space |
| left/right magna-save | `LeftMagna` `RightMagna` | LCtrl / RCtrl |

Disable VPX's (wrong) auto-layout for the device once we map it ourselves:
```
[Input]
Device.SDLJoy_<GUID>_<idx>.NoAutoLayout = 1
```
(read in `InputManager::ProcessInput` as `Device.<settingsId>.NoAutoLayout`.)

## Discovery tool — `jstest.c`

Standalone SDL3 program (this folder). Opens all joysticks, prints each device's
exact `SDLJoy_<GUID>_<idx>` line, then for ~30s logs every button/axis/hat with the
**exact id to paste after the `;`**. Output → stdout (wrapper captures it to
`vpx-log.txt`). Operator presses controls in a known order; we read ids from the log.

## Wiring it into the combined orientation+input build (apply when shipping #13)

1. **Workflow** (`build-vpx.yml`) — after the "Build launch wrapper" step, compile the
   logger against the in-tree SDL3 headers + built lib, and stage it:
   ```yaml
   - name: Build joystick discovery logger (-> jstest.elf)
     run: cc -O2 -I vpinball/third-party/include -o tooling/packaging/jstest.elf \
            tooling/packaging/jstest.c -L vpinball/build -lSDL3
   ```
   and in "Assemble External App": `cp tooling/packaging/jstest.elf "$OUT/" && chmod +x "$OUT/jstest.elf"`.
   (Verify `vpinball/third-party/include/SDL3/SDL.h` + `libSDL3.so` paths in the build log first.)

2. **Wrapper** (`wrapper.c`) — before the VPX launch block, run the logger from RUN with
   `LD_LIBRARY_PATH=RUN`, stdout→log, ~30s, eager `sync_log_to_usb()` so we get it even if
   the cabinet kills us. Then proceed to VPX exactly as now (same run also confirms rotation).
   Also: after VPX exits, copy `$HOME/.local/share/VPinballX/10.8/VPinballX.ini` to the USB
   as `vpx-ini-after.txt` — VPX may persist the device's auto-layout there, which independently
   confirms the real `SDLJoy_..._idx` string and shows VPX's default Mapping.* lines.

3. **CityHands guide** (one trip, both goals): "Launch vpx. In the FIRST 30s press, holding
   ~1s with a pause between: LEFT flipper, RIGHT flipper, PLUNGER/LAUNCH, START, LEFT nudge,
   RIGHT nudge. THEN the table appears — is it upright now? Send photo + vpx-log.txt
   (+ vpx-ini-after.txt)."

4. Read ids from the log → write `Mapping.*` + `NoAutoLayout` into `wrapper.c`'s pre-seeded
   ini → ship #14 (mapped). Remove the logger once mapping is confirmed.
