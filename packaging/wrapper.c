/*
 * OpenPin4K -- VPX launch harness v4.  Compiled to "vpx.elf".
 *
 * STORY SO FAR (cabinet logs, 2026-06-27):
 *   v3 got VPX actually running from the USB: table loaded, cabinet joystick +
 *   audio detected, Mali GPU (libmali g6p0) up, a 3840x2160@60 window created --
 *   then "BGFX initialization failed" -> exit(-1) (255), black screen.
 *   Cause: VPX's Player.GfxBackend defaulted to "Default" (auto), and BGFX
 *   auto-picked a backend the Mali GPU can't drive. Reported valid backends were
 *   OpenGL + Vulkan (NO OpenGLES compiled in). Mali wants GLES/Vulkan, so desktop
 *   OpenGL fails -> Vulkan is the likely winner.
 *
 * This harness runs VPX from the USB bundle and TRIES BACKENDS IN ORDER
 * (Vulkan, OpenGL, OpenGLES) by writing Player.GfxBackend into VPinballX.ini
 * before each attempt. A backend that fails BGFX init dies in ~3s (we move on);
 * a backend that works keeps rendering past the probe window (we detect that and
 * leave the table up so it can be seen). Everything is logged back to the USB.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PROBE_SECONDS   10   /* survive this long with a backend => it works */
#define SHOWCASE_SECONDS 45  /* once it works, keep it on screen this long */

static FILE *LOG;
static char  LOGP[PATH_MAX];

static void unescape(char *s)  /* /proc/mounts encodes spaces as \040 */
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

static void distribute_log(void)
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
            snprintf(chk, sizeof chk, "%s/%s/VPinballX_BGFX", mp, subs[i]);
            if (access(chk, F_OK) == 0) { snprintf(dst, sizeof dst, "%s/%s/vpx-log.txt", mp, subs[i]); copy_file(LOGP, dst); }
        }
    }
    fclose(m);
}

/* Write a minimal VPinballX.ini selecting a graphics backend. */
static void write_ini(const char *inipath, const char *backend)
{
    FILE *f = fopen(inipath, "w");
    if (!f) { fprintf(LOG, "  (could not write ini %s: %s)\n", inipath, strerror(errno)); return; }
    fprintf(f, "[Player]\nGfxBackend = %s\n", backend);
    fclose(f);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char D[PATH_MAX], cwd[PATH_MAX], exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n]=0; strncpy(D, exe, sizeof D-1); D[sizeof D-1]=0; char *s=strrchr(D,'/'); if (s) *s=0; }
    else strcpy(D, ".");
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");

    /* writable scratch for log + HOME */
    const char *scratch = D;
    const char *sc[] = { "/tmp", "/dev/shm", "/var/tmp", D, NULL };
    for (int i = 0; sc[i]; i++) if (dir_writable(sc[i])) { scratch = sc[i]; break; }
    snprintf(LOGP, sizeof LOGP, "%s/vpx-log.txt", scratch);
    LOG = fopen(LOGP, "w"); if (!LOG) LOG = stderr;

    time_t t = time(NULL);
    fprintf(LOG, "==== OpenPin4K VPX harness v4 (backend probe) ====\n");
    fprintf(LOG, "time: %s/proc/self/exe dir=%s  cwd=%s\n", ctime(&t), D, cwd);

    /* RUNDIR = wherever the full bundle actually is. cwd worked last time. */
    char RUN[PATH_MAX] = "";
    if (access("VPinballX_BGFX", F_OK) == 0) strncpy(RUN, cwd, sizeof RUN - 1);
    else {
        FILE *m = fopen("/proc/mounts", "r");
        if (m) { char line[2048], dv[256], mp[PATH_MAX], fs[64];
            while (fgets(line, sizeof line, m)) {
                if (sscanf(line, "%255s %4095s %63s", dv, mp, fs) != 3) continue;
                if (strcmp(fs,"vfat")&&strcmp(fs,"exfat")&&strcmp(fs,"msdos")&&strcmp(fs,"fuseblk")&&strcmp(fs,"texfat")) continue;
                unescape(mp);
                const char *subs[] = { "external/vpx", "vpx", NULL };
                for (int i = 0; subs[i]; i++) { char c[PATH_MAX]; snprintf(c,sizeof c,"%s/%s/VPinballX_BGFX",mp,subs[i]);
                    if (access(c,F_OK)==0 && !RUN[0]) snprintf(RUN,sizeof RUN,"%s/%s",mp,subs[i]); }
            }
            fclose(m);
        }
    }
    if (!RUN[0]) { fprintf(LOG, "FATAL: could not locate VPinballX_BGFX bundle.\n"); if (LOG!=stderr) fclose(LOG); distribute_log(); return 1; }
    fprintf(LOG, "RUNDIR = %s\n", RUN);

    /* HOME + the VPinballX.ini path VPX loads from. */
    char home[PATH_MAX]; snprintf(home, sizeof home, "%s/op-home", scratch);
    char inidir[PATH_MAX]; snprintf(inidir, sizeof inidir, "%s/.local/share/VPinballX/10.8", home);
    {
        char p[PATH_MAX]; const char *parts[] = { ".local", ".local/share", ".local/share/VPinballX", ".local/share/VPinballX/10.8", NULL };
        mkdir(home, 0755);
        for (int i = 0; parts[i]; i++) { snprintf(p, sizeof p, "%s/%s", home, parts[i]); mkdir(p, 0755); }
    }
    char inipath[PATH_MAX]; snprintf(inipath, sizeof inipath, "%s/VPinballX.ini", inidir);

    setenv("HOME", home, 1);
    setenv("LD_LIBRARY_PATH", RUN, 1);
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);

    const char *backends[] = { "Vulkan", "OpenGL", "OpenGLES", NULL };
    int success = 0;

    for (int b = 0; backends[b] && !success; b++) {
        write_ini(inipath, backends[b]);
        FILE *af = fopen(LOGP, "a");
        if (af) { fprintf(af, "\n================= TRYING BACKEND: %s =================\n", backends[b]); fclose(af); }

        pid_t pid = fork();
        if (pid == 0) {
            chdir(RUN);
            int fd = open(LOGP, O_WRONLY|O_CREAT|O_APPEND, 0644);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
            execl("./VPinballX_BGFX", "VPinballX_BGFX", "-play", "exampleTable.vpx", (char *)NULL);
            fprintf(stderr, "\n[harness] exec failed: %s\n", strerror(errno));
            _exit(127);
        }

        int status = 0, exited = 0, secs = 0;
        for (; secs < PROBE_SECONDS; secs++) { if (waitpid(pid, &status, WNOHANG) == pid) { exited = 1; break; } sleep(1); }

        af = fopen(LOGP, "a");
        if (exited) {
            if (af) fprintf(af, "\n[harness] backend %s FAILED/exited after ~%ds (code %d) -> trying next\n",
                            backends[b], secs, WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
            if (af) fclose(af);
        } else {
            success = 1;
            if (af) { fprintf(af, "\n[harness] backend %s SURVIVED %ds -> RENDERING. Leaving it up ~%ds so you can see it.\n",
                              backends[b], PROBE_SECONDS, SHOWCASE_SECONDS); fclose(af); }
            sleep(SHOWCASE_SECONDS);
            kill(pid, SIGTERM); sleep(2); kill(pid, SIGKILL); waitpid(pid, &status, 0);
            af = fopen(LOGP, "a");
            if (af) { fprintf(af, "\n[harness] *** WORKING BACKEND: %s *** (closed after showcase)\n", backends[b]); fclose(af); }
        }
    }

    if (!success) { FILE *af = fopen(LOGP, "a"); if (af) { fprintf(af, "\n[harness] No backend initialized BGFX. Need a GLES-enabled BGFX build (see notes).\n"); fclose(af); } }
    distribute_log();
    return 0;
}
