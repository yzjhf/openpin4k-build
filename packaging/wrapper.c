/*
 * OpenPin4K -- VPX launch harness v6 (GL / OpenGL ES renderer).  -> "vpx.elf".
 *
 * STORY SO FAR (cabinet logs, 2026-06-27):
 *   VPX runs fully from the USB (table, joystick, audio, Mali GPU, 4K window) but
 *   the BGFX renderer could not init ANY of its compiled backends on-device
 *   (Vulkan/OpenGL/auto all failed; OpenGLES wasn't even compiled into BGFX).
 *   The cabinet's Mali GPU on KMSDRM (no X11) needs OpenGL ES. So we switched the
 *   build to VPX's own GL renderer, which on aarch64 is OpenGL ES via SDL3/KMSDRM
 *   -> binary is now VPinballX_GL (+ a shaders-* dir), NOT VPinballX_BGFX.
 *
 * This harness runs VPinballX_GL from the USB bundle, mirrors the log to the USB
 * after every step (cabinet may kill us mid-run), and keeps a working render up
 * long enough to be seen/photographed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <strings.h>

#define BIN "VPinballX_GL"
#define JSTEST "jstest.elf"     /* SDL3 joystick-discovery logger, run before VPX */
#define DISCOVER_SECONDS 40     /* upper bound to wait for the logger (it self-exits ~30s) */
/* SHOWCASE_SECONDS retired in the ship build (v17): no auto-close timeout -- VPX runs
 * until the player presses Exit (button 12). Kept as a no-op note for history. */

/* Cabinet controls device, taken from VPX's OWN log ("VPX UID: ..."), NOT from the
 * jstest logger (which computed the index as _0; VPX uses _1 -- always trust VPX's). */
#define ATGDEV "SDLJoy_190060e2380800001988000011010000_1"

static char LOGP[PATH_MAX];

static void unescape(char *s)
{
    char *o = s;
    for (char *p = s; *p; ) {
        if (p[0]=='\\' && p[1]>='0'&&p[1]<='7' && p[2]>='0'&&p[2]<='7' && p[3]>='0'&&p[3]<='7') {
            *o++ = (char)((p[1]-'0')*64 + (p[2]-'0')*8 + (p[3]-'0')); p += 4;
        } else *o++ = *p++;
    }
    *o = 0;
}

static int dir_writable(const char *d)
{
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/.opw", d);
    int fd = open(p, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) return 0;
    close(fd); unlink(p); return 1;
}

static void copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY); if (in < 0) return;
    int out = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644); if (out < 0) { close(in); return; }
    char b[65536]; ssize_t n;
    while ((n = read(in, b, sizeof b)) > 0) { ssize_t w=0; while (w<n){ssize_t k=write(out,b+w,n-w); if(k<=0)break; w+=k;} }
    close(in); close(out);
}

/* Copy the current log onto every FAT/exFAT volume holding this app. */
static void sync_log_to_usb(void)
{
    FILE *m = fopen("/proc/mounts", "r"); if (!m) return;
    char line[2048], dev[256], mp[PATH_MAX], fs[64];
    while (fgets(line, sizeof line, m)) {
        if (sscanf(line, "%255s %4095s %63s", dev, mp, fs) != 3) continue;
        if (strcmp(fs,"vfat")&&strcmp(fs,"exfat")&&strcmp(fs,"msdos")&&strcmp(fs,"fuseblk")&&strcmp(fs,"texfat")) continue;
        unescape(mp);
        const char *subs[] = { "external/vpx", "vpx", ".", NULL };
        for (int i = 0; subs[i]; i++) {
            char chk[PATH_MAX], dst[PATH_MAX];
            snprintf(chk, sizeof chk, "%s/%s/vpx.elf", mp, subs[i]);
            if (access(chk, F_OK) == 0) { snprintf(dst, sizeof dst, "%s/%s/vpx-log.txt", mp, subs[i]); copy_file(LOGP, dst); }
        }
    }
    fclose(m);
}

static void logln(const char *fmt, ...)
{
    FILE *f = fopen(LOGP, "a");
    if (f) { va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f); }
    sync_log_to_usb();
}

static void on_term(int sig) { logln("[harness] received signal %d -> syncing log and exiting.", sig); _exit(0); }

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char D[PATH_MAX], cwd[PATH_MAX], exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n]=0; strncpy(D, exe, sizeof D-1); D[sizeof D-1]=0; char *s=strrchr(D,'/'); if (s) *s=0; }
    else strcpy(D, ".");
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");

    const char *scratch = D;
    const char *sc[] = { "/tmp", "/dev/shm", "/var/tmp", D, NULL };
    for (int i = 0; sc[i]; i++) if (dir_writable(sc[i])) { scratch = sc[i]; break; }
    snprintf(LOGP, sizeof LOGP, "%s/vpx-log.txt", scratch);

    { FILE *f = fopen(LOGP, "w"); time_t t = time(NULL);
      if (f) { fprintf(f, "==== OpenPin4K VPX harness v18 (auto-detect real table; full controls; no auto-close; Exit=12) ====\ntime: %sexe dir=%s  cwd=%s\n", ctime(&t), D, cwd); fclose(f); } }
    sync_log_to_usb();

    signal(SIGTERM, on_term); signal(SIGINT, on_term); signal(SIGHUP, on_term);

    /* RUNDIR = where the full bundle is (the launcher sets cwd to the USB folder). */
    char RUN[PATH_MAX] = "";
    if (access(BIN, F_OK) == 0) strncpy(RUN, cwd, sizeof RUN - 1);
    else {
        FILE *m = fopen("/proc/mounts", "r");
        if (m) { char line[2048], dv[256], mp[PATH_MAX], fs[64];
            while (fgets(line, sizeof line, m)) {
                if (sscanf(line, "%255s %4095s %63s", dv, mp, fs) != 3) continue;
                if (strcmp(fs,"vfat")&&strcmp(fs,"exfat")&&strcmp(fs,"msdos")&&strcmp(fs,"fuseblk")&&strcmp(fs,"texfat")) continue;
                unescape(mp);
                const char *subs[] = { "external/vpx", "vpx", NULL };
                for (int i = 0; subs[i]; i++) { char c[PATH_MAX]; snprintf(c,sizeof c,"%s/%s/" BIN,mp,subs[i]);
                    if (access(c,F_OK)==0 && !RUN[0]) snprintf(RUN,sizeof RUN,"%s/%s",mp,subs[i]); }
            }
            fclose(m);
        }
    }
    if (!RUN[0]) { logln("FATAL: could not locate " BIN " bundle."); return 1; }
    logln("RUNDIR = %s", RUN);

    /* VPX writes a per-table texture cache next to the .vpx (on the USB). A cache
     * left half-written when we close the app makes the NEXT run hang at "Loading
     * Textures". Wipe it before every launch so each run is a clean first run. */
    { char cmd[PATH_MAX + 32]; snprintf(cmd, sizeof cmd, "rm -rf '%s/cache'", RUN);
      int rc = system(cmd); logln("[harness] cleared stale texture cache (rc=%d)", rc); }

    char home[PATH_MAX]; snprintf(home, sizeof home, "%s/op-home", scratch);
    { const char *parts[] = { "", ".local", ".local/share", ".local/share/VPinballX", ".local/share/VPinballX/10.8", NULL };
      char p[PATH_MAX]; for (int i = 0; parts[i]; i++) { snprintf(p, sizeof p, "%s/%s", home, parts[i]); mkdir(p, 0755); } }
    setenv("HOME", home, 1);
    setenv("LD_LIBRARY_PATH", RUN, 1);
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);

    /* Pre-seed VPinballX.ini with cabinet settings (VPX reads this path -- proven):
     *   SyncMode=0  : No Sync. Default 3 (Frame Pacing) fails on the GL/ES renderer
     *                 ("Failed to create the synchronization device") -> frozen frame.
     *   ShowFPS=1   : on-screen FPS counter, to prove the frame loop is actually running.
     *   BGSet=1     : Cabinet view (drops the desktop grey frame, fills the playfield).
     *   ViewCabRotation=180 : rotate upright on the portrait playfield. BRACKETED on the
     *                 cabinet: #11 used 90 -> off, needed +90 CW; #12 used 270 -> off,
     *                 needed 90 CCW ("over-rotated"). Correct value is between them = 180
     *                 (90+90CW = 180; 270-90 = 180). 0 was the only other candidate, ruled
     *                 out by "over-rotated past the right value going 90->270".
     *   ShowFPS=1 stays on to confirm the loop runs (test #11: 20fps, loop alive).
     *
     * [Input]: map the two flippers we captured with confidence in test #13 (button 13
     *   = LEFT flipper, 5 = RIGHT, pressed 1st/2nd in order). NoAutoLayout=1 stops VPX
     *   layering its (wrong) auto-guesses on top -- confirmed in InputManager.cpp:
     *   ProcessInput only calls ApplyDefaultDeviceMapping when NoAutoLayout is false;
     *   explicit Mapping.<id> is loaded regardless (InputAction::LoadMapping). Mapping
     *   string format = "<deviceUID>;<buttonId>" (InputAction.cpp). FULL control set,
     *   discovered on-cabinet (tests #13 + #14): button 13=Left flipper, 5=Right flipper
     *   (confirmed working, correct sides), 9=Launch/plunger (seen in both tests' plunger
     *   slot), 14=Start, 7=Left nudge (press-order + count protocol). Right nudge not yet
     *   captured -> left unmapped. NoAutoLayout=1 keeps VPX's gamepad auto-guess (which
     *   mislabels these as A/B/Stick/Shoulder) from being layered on. This is the first
     *   genuinely PLAYABLE build: start a game (14) -> launch (9) -> flip (13/5). */
    { char inipath[PATH_MAX]; snprintf(inipath, sizeof inipath, "%s/.local/share/VPinballX/10.8/VPinballX.ini", home);
      FILE *ini = fopen(inipath, "w");
      if (ini) { fputs("[Player]\nSyncMode = 0\nShowFPS = 1\nBGSet = 1\n"
                       /* AAFactor scales the OFFSCREEN render buffer: renderW/H * AAFactor
                        * (Renderer.cpp ~L135). Default 1.0 = render full 4K (3840x2160)
                        * every frame -> ~45ms present on Mali = the 20fps bottleneck (test
                        * #11 profiler: Flip 90% of frame). 0.5 = render 1920x1080 then
                        * upscale to the 4K window -> ~1/4 the fill work. Range is 0.5..2.0
                        * (Settings_properties.inl), so 0.5 is the most perf this lever gives. */
                       "AAFactor = 0.5\n"
                       /* DisableAO=1: ambient occlusion is DYNAMIC by default (DynamicAO=true,
                        * Settings_properties.inl L161) = a per-frame GPU pass. AO is subtle
                        * crevice shading, near-invisible on a moving table -> drop it for a
                        * small FPS bump with minimal visible cost. REVERT = remove this one
                        * line to return to build #16 (the confirmed-good baseline). */
                       "DisableAO = 1\n\n"
                       "[TableOverride]\nViewCabRotation = 180\n\n"
                       "[Input]\n"
                       "Device." ATGDEV ".NoAutoLayout = 1\n"
                       "Mapping.LeftFlipper = " ATGDEV ";13\n"
                       "Mapping.RightFlipper = " ATGDEV ";5\n"
                       "Mapping.LaunchBall = " ATGDEV ";9\n"
                       "Mapping.Start = " ATGDEV ";14\n"
                       /* HARDWARE OVERLAP (user confirmed 100%): the physical LEFT-NUDGE button and
                        * the START button are two separate buttons but the cabinet sends the SAME
                        * code (14) for both -> indistinguishable to VPX. So DUAL-MAP 14: it stays
                        * Start AND also becomes the left nudge. By the 180deg flip (physical-right
                        * button 7 -> LeftNudge -> nudges right), the physical-LEFT button maps to
                        * RightNudge so it nudges left. Side effect to test: pressing it to nudge
                        * also fires Start (may serve a ball / add a player). Easily reverted. */
                       "Mapping.RightNudge = " ATGDEV ";14\n"
                       /* Button 7 = the WORKING (right) nudge -- name "LeftNudge" is a misnomer due
                        * to the 180deg flip, but behaviour is correct, so LEAVE IT (user confirmed). */
                       "Mapping.LeftNudge = " ATGDEV ";7\n"
                       /* ExitGame = button 12 (the MENU button, captured in test #18). On a
                        * STANDALONE build, ExitGame -> SetCloseState(CS_CLOSE_APP) = single
                        * press closes VPX cleanly back to the cabinet menu, no confirm dialog
                        * (InputManager.cpp ~L776; Exitconfirm only affects a long-ESC hold). */
                       "Mapping.ExitGame = " ATGDEV ";12\n", ini); fclose(ini); }
      logln("[harness] wrote VPinballX.ini: AAFactor=0.5 + DisableAO=1 + rotation 180 + map L-flip=13 R-flip=5 launch=9 start=14 (+RightNudge=14 dual) Lnudge-action=7 EXIT=12 on " ATGDEV " (NoAutoLayout)"); }

    /* Detector removed -- full control set is mapped (incl. Exit=12 and the dual-mapped
     * left nudge on 14). Straight to VPX; jstest.elf stays bundled for any future use. */

    /* TABLE SELECTION (v18): community .vpx tables are NOT redistributable, so they are
     * NOT baked into our public engine bundle -- the player (CityHands) drops a real .vpx
     * into the bundle folder on the USB. Scan RUNDIR for any *.vpx and play the FIRST one
     * that isn't our bundled demo (exampleTable.vpx); if none is present, fall back to the
     * demo so the engine still does something. This keeps the GPL engine and the (3rd-party)
     * table cleanly separate -- the same split the table-manager will produce later. We do
     * NOT yet apply the catalogue's per-table table.ini: the global VPinballX.ini we seed
     * (rotation 180 + perf + input map, all cabinet-proven) governs every table, so this
     * first real-table run changes exactly ONE variable -- the table file itself. */
    char table[PATH_MAX]; strcpy(table, "exampleTable.vpx");
    { DIR *d = opendir(RUN);
      if (d) { struct dirent *e;
          while ((e = readdir(d))) {
              const char *nm = e->d_name; size_t L = strlen(nm);
              if (L > 4 && strcasecmp(nm + L - 4, ".vpx") == 0
                        && strcmp(nm, "exampleTable.vpx") != 0) {
                  strncpy(table, nm, sizeof table - 1); table[sizeof table - 1] = 0; break;
              }
          }
          closedir(d);
      }
    }
    if (strcmp(table, "exampleTable.vpx") == 0)
        logln("[harness] no community .vpx found in RUNDIR -> playing bundled demo exampleTable.vpx");
    else
        logln("[harness] community table found -> playing '%s'", table);

    logln("\n================= LAUNCHING %s (%s) =================", BIN, table);
    pid_t pid = fork();
    if (pid == 0) {
        chdir(RUN);
        int fd = open(LOGP, O_WRONLY|O_CREAT|O_APPEND, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execl("./" BIN, BIN, "-play", table, (char *)NULL);
        fprintf(stderr, "\n[harness] exec of %s failed: %s\n", BIN, strerror(errno));
        _exit(127);
    }

    /* SHIP behavior: NO auto-close timeout. Run until VPX exits on its own -- the player
     * quits with the Exit button (12 -> ExitGame -> clean close, confirmed test #20). We
     * just wait, syncing the log each second so we still get diagnostics if anything dies. */
    int status = 0;
    for (;;) {
        if (waitpid(pid, &status, WNOHANG) == pid) {
            logln("[harness] %s exited (code %d).", BIN,
                  WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
            break;
        }
        sleep(1);
        sync_log_to_usb();
    }

    /* Copy the VPinballX.ini VPX leaves behind onto the USB. VPX may persist its
     * (wrong) auto-layout Mapping.* lines + the real device id here, which lets us
     * cross-check the device UID and see exactly which buttons it guessed. */
    { char inipath[PATH_MAX]; snprintf(inipath, sizeof inipath, "%s/.local/share/VPinballX/10.8/VPinballX.ini", home);
      FILE *m = fopen("/proc/mounts", "r");
      if (m) { char line[2048], dv[256], mp[PATH_MAX], fs[64];
          while (fgets(line, sizeof line, m)) {
              if (sscanf(line, "%255s %4095s %63s", dv, mp, fs) != 3) continue;
              if (strcmp(fs,"vfat")&&strcmp(fs,"exfat")&&strcmp(fs,"msdos")&&strcmp(fs,"fuseblk")&&strcmp(fs,"texfat")) continue;
              unescape(mp);
              const char *subs[] = { "external/vpx", "vpx", NULL };
              for (int i = 0; subs[i]; i++) { char chk[PATH_MAX], dst[PATH_MAX];
                  snprintf(chk, sizeof chk, "%s/%s/vpx.elf", mp, subs[i]);
                  if (access(chk, F_OK) == 0) { snprintf(dst, sizeof dst, "%s/%s/vpx-ini-after.txt", mp, subs[i]); copy_file(inipath, dst); } }
          }
          fclose(m);
      }
      logln("[harness] copied VPX's VPinballX.ini to USB as vpx-ini-after.txt"); }

    sync_log_to_usb();
    return 0;
}
