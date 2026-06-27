/*
 * OpenPin4K -- VPX launch harness v5.  Compiled to "vpx.elf".
 *
 * STORY SO FAR (cabinet logs, 2026-06-27):
 *   v3: VPX ran fully from the USB (table, joystick, audio, Mali GPU, 4K window)
 *       then "BGFX initialization failed" (Player.GfxBackend="Default" auto-pick
 *       can't drive the Mali GPU; backends compiled = OpenGL + Vulkan, no GLES).
 *   v4: tried backends in a loop but produced NO log on the USB -- it ran longer
 *       and the cabinet launcher killed our process BEFORE the end-of-run step
 *       that copies the log to the USB.
 *
 * v5 therefore SYNCS THE LOG TO THE USB AFTER EVERY STEP (and on SIGTERM), so we
 * always capture progress even if we're killed mid-run. It still tries
 * Player.GfxBackend = Vulkan -> OpenGL -> OpenGLES and reports the winner.
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

#define PROBE_SECONDS    10  /* survive this long with a backend => it works */
#define SHOWCASE_SECONDS 40  /* once it works, keep it on screen this long */

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
            snprintf(chk, sizeof chk, "%s/%s/VPinballX_BGFX", mp, subs[i]);
            if (access(chk, F_OK) == 0) { snprintf(dst, sizeof dst, "%s/%s/vpx-log.txt", mp, subs[i]); copy_file(LOGP, dst); }
        }
    }
    fclose(m);
}

/* Append a line to the log (flushed) and immediately mirror it to the USB. */
static void logln(const char *fmt, ...)
{
    FILE *f = fopen(LOGP, "a");
    if (f) { va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f); }
    sync_log_to_usb();
}

static void on_term(int sig) { logln("[harness] received signal %d -> syncing log and exiting.", sig); _exit(0); }

static void write_ini(const char *inipath, const char *backend)
{
    FILE *f = fopen(inipath, "w");
    if (f) { fprintf(f, "[Player]\nGfxBackend = %s\n", backend); fclose(f); }
}

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

    /* Truncate/start the log, then mirror immediately. */
    { FILE *f = fopen(LOGP, "w"); time_t t = time(NULL);
      if (f) { fprintf(f, "==== OpenPin4K VPX harness v5 (backend probe, eager log) ====\ntime: %sexe dir=%s  cwd=%s\n", ctime(&t), D, cwd); fclose(f); } }
    sync_log_to_usb();

    signal(SIGTERM, on_term);
    signal(SIGINT,  on_term);
    signal(SIGHUP,  on_term);

    /* RUNDIR = where the full bundle is (cwd worked in v3). */
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
    if (!RUN[0]) { logln("FATAL: could not locate VPinballX_BGFX bundle."); return 1; }
    logln("RUNDIR = %s", RUN);

    char home[PATH_MAX]; snprintf(home, sizeof home, "%s/op-home", scratch);
    { const char *parts[] = { "", ".local", ".local/share", ".local/share/VPinballX", ".local/share/VPinballX/10.8", NULL };
      char p[PATH_MAX]; for (int i = 0; parts[i]; i++) { snprintf(p, sizeof p, "%s/%s", home, parts[i]); mkdir(p, 0755); } }
    char inipath[PATH_MAX]; snprintf(inipath, sizeof inipath, "%s/.local/share/VPinballX/10.8/VPinballX.ini", home);

    setenv("HOME", home, 1);
    setenv("LD_LIBRARY_PATH", RUN, 1);
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);

    const char *backends[] = { "Vulkan", "OpenGL", "OpenGLES", NULL };
    int success = 0;

    for (int b = 0; backends[b] && !success; b++) {
        write_ini(inipath, backends[b]);
        logln("\n================= TRYING BACKEND: %s =================", backends[b]);

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
        for (; secs < PROBE_SECONDS; secs++) {
            if (waitpid(pid, &status, WNOHANG) == pid) { exited = 1; break; }
            sleep(1);
            sync_log_to_usb();   /* keep mirroring VPX's own output as it appears */
        }

        if (exited) {
            logln("[harness] backend %s FAILED/exited after ~%ds (code %d) -> next",
                  backends[b], secs, WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
        } else {
            success = 1;
            logln("[harness] backend %s SURVIVED %ds -> RENDERING. Keeping it up ~%ds.", backends[b], PROBE_SECONDS, SHOWCASE_SECONDS);
            for (int s = 0; s < SHOWCASE_SECONDS; s++) { if (waitpid(pid, &status, WNOHANG) == pid) break; sleep(1); }
            kill(pid, SIGTERM); sleep(2); kill(pid, SIGKILL); waitpid(pid, &status, 0);
            logln("[harness] *** WORKING BACKEND: %s ***", backends[b]);
        }
    }

    if (!success) logln("[harness] No backend initialized BGFX -> likely need a GLES-enabled BGFX build.");
    sync_log_to_usb();
    return 0;
}
