/*
 * OpenPin4K -- VPX launch harness v3.  Compiled to "vpx.elf".
 *
 * DISCOVERY from cabinet run #2 (vpx-log.txt, 2026-06-27):
 *   The AtGames "External Applications" launcher (RetroFE-based) COPIES the app
 *   to an internal temp dir (/tmp/retrofe) and runs it FROM THERE -- but only the
 *   small vpx.elf was copied; VPinballX_BGFX / the table / the .so libs were NOT.
 *   So `./VPinballX_BGFX` didn't exist next to us -> exec failed (127), 5s black.
 *   The full ~580MB bundle is still on the USB, just not where we execute.
 *
 * This harness therefore:
 *   1. Logs diagnostics (what got copied, free space per location, USB mount opts).
 *   2. Finds the full bundle on the USB via /proc/mounts.
 *   3. Runs VPinballX_BGFX from a location that actually HAS it:
 *        - in place from the USB if that mount allows exec, otherwise
 *        - copy the whole bundle to an exec-able dir with space, then run.
 *   4. Captures output (60s timeout) and copies the log back onto the USB.
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
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

#define RUN_TIMEOUT 60

static FILE *LOG;
static char  LOGP[PATH_MAX];

static void unescape(char *s)  /* /proc/mounts encodes spaces as \040 etc. */
{
    char *o = s;
    for (char *p = s; *p; ) {
        if (p[0] == '\\' && p[1]>='0'&&p[1]<='7' && p[2]>='0'&&p[2]<='7' && p[3]>='0'&&p[3]<='7') {
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

static long free_mb(const char *d)
{
    struct statvfs v;
    if (statvfs(d, &v) != 0) return -1;
    return (long)((v.f_bavail * (unsigned long long)v.f_frsize) / (1024*1024));
}

static void list_dir(const char *d)
{
    DIR *dp = opendir(d);
    if (!dp) { fprintf(LOG, "  (cannot open %s: %s)\n", d, strerror(errno)); return; }
    struct dirent *e;
    while ((e = readdir(dp))) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", d, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0)
            fprintf(LOG, S_ISDIR(st.st_mode) ? "  [DIR]      %s\n" : "  %9lld  %s\n",
                    S_ISDIR(st.st_mode) ? (long long)0 : (long long)st.st_size, e->d_name);
        else fprintf(LOG, "  [?]        %s\n", e->d_name);
    }
    closedir(dp);
}

static void copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY); if (in < 0) return;
    int out = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644); if (out < 0) { close(in); return; }
    char b[65536]; ssize_t n;
    while ((n = read(in, b, sizeof b)) > 0) { ssize_t w=0; while (w<n){ssize_t k=write(out,b+w,n-w); if(k<=0)break; w+=k;} }
    close(in); close(out);
}

/* Copy the log onto every FAT/exFAT volume that holds this app. */
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

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char exe[PATH_MAX], D[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n]=0; strncpy(D, exe, sizeof D-1); D[sizeof D-1]=0; char *s=strrchr(D,'/'); if (s) *s=0; }
    else if (!getcwd(D, sizeof D)) strcpy(D, ".");

    const char *scratch = D;
    const char *sc[] = { "/tmp", "/dev/shm", "/data/local/tmp", "/var/tmp", D, NULL };
    for (int i = 0; sc[i]; i++) if (dir_writable(sc[i])) { scratch = sc[i]; break; }
    snprintf(LOGP, sizeof LOGP, "%s/vpx-log.txt", scratch);
    LOG = fopen(LOGP, "w"); if (!LOG) LOG = stderr;

    char cwd[PATH_MAX]; if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "(getcwd failed)");
    time_t t = time(NULL);
    fprintf(LOG, "==== OpenPin4K VPX harness v3 ====\n");
    fprintf(LOG, "time: %suid=%d\n", ctime(&t), (int)getuid());
    fprintf(LOG, "own dir D (/proc/self/exe) = %s (writable=%d)\n", D, dir_writable(D));
    fprintf(LOG, "launcher cwd               = %s (writable=%d)\n", cwd, dir_writable(cwd));
    fprintf(LOG, "  -> VPinballX_BGFX in cwd? %d\n", access("VPinballX_BGFX", F_OK) == 0);

    fprintf(LOG, "\n-- contents of own dir D (what the launcher copied here) --\n"); list_dir(D);

    fprintf(LOG, "\n-- free space (MB) --\n");
    const char *spc[] = { "/tmp", "/data/local/tmp", "/var/tmp", "/dev/shm", D, "/storage", "/mnt", NULL };
    for (int i = 0; spc[i]; i++) fprintf(LOG, "  %16s : %ld MB\n", spc[i], free_mb(spc[i]));

    char USB[PATH_MAX] = "", USBopts[256] = "";
    fprintf(LOG, "\n-- FAT/exFAT mounts --\n");
    FILE *m = fopen("/proc/mounts", "r");
    if (m) {
        char line[2048], dev[256], mp[PATH_MAX], fs[64], opts[256];
        while (fgets(line, sizeof line, m)) {
            if (sscanf(line, "%255s %4095s %63s %255s", dev, mp, fs, opts) != 4) continue;
            if (strcmp(fs,"vfat")&&strcmp(fs,"exfat")&&strcmp(fs,"msdos")&&strcmp(fs,"fuseblk")&&strcmp(fs,"texfat")) continue;
            unescape(mp);
            fprintf(LOG, "  %s  [%s]  %s\n", mp, fs, opts);
            const char *subs[] = { "external/vpx", "vpx", ".", NULL };
            for (int i = 0; subs[i]; i++) {
                char chk[PATH_MAX]; snprintf(chk, sizeof chk, "%s/%s/VPinballX_BGFX", mp, subs[i]);
                if (access(chk, F_OK) == 0 && !USB[0]) {
                    snprintf(USB, sizeof USB, "%s/%s", mp, subs[i]);
                    strncpy(USBopts, opts, sizeof USBopts - 1);
                }
            }
        }
        fclose(m);
    }
    fprintf(LOG, "\nUSB bundle dir: %s  (opts: %s)\n", USB[0] ? USB : "NOT FOUND", USBopts);
    if (USB[0]) { fprintf(LOG, "-- USB bundle contents --\n"); list_dir(USB); }

    /* Pick where to run the engine from. */
    char RUN[PATH_MAX] = ""; int from_usb = 0;
    if (USB[0] && !strstr(USBopts, "noexec")) {
        strncpy(RUN, USB, sizeof RUN - 1); from_usb = 1;
        fprintf(LOG, "\nPlan: run IN PLACE from USB (mount is not noexec).\n");
    } else if (USB[0]) {
        const char *cand[] = { D, "/data/local/tmp", "/tmp", "/var/tmp", NULL };
        for (int i = 0; cand[i]; i++) {
            if (!dir_writable(cand[i])) { fprintf(LOG, "  stage skip %s (not writable)\n", cand[i]); continue; }
            long mb = free_mb(cand[i]);
            if (mb < 700) { fprintf(LOG, "  stage skip %s (only %ld MB free)\n", cand[i], mb); continue; }
            char rd[PATH_MAX]; snprintf(rd, sizeof rd, "%s/vpxrun", cand[i]); mkdir(rd, 0755);
            char cmd[PATH_MAX*2 + 64];
            snprintf(cmd, sizeof cmd, "cp -a '%s/.' '%s/' 2>>'%s'", USB, rd, LOGP);
            fprintf(LOG, "\nStaging bundle to %s ...\n", rd); fflush(LOG);
            int rc = system(cmd);
            char bin[PATH_MAX]; snprintf(bin, sizeof bin, "%s/VPinballX_BGFX", rd);
            int ok = access(bin, F_OK) == 0;
            fprintf(LOG, "  cp rc=%d  VPinballX_BGFX present=%d\n", rc, ok);
            if (ok) { chmod(bin, 0755); strncpy(RUN, rd, sizeof RUN - 1); break; }
        }
        if (!RUN[0]) fprintf(LOG, "\nFATAL: no exec-able dir with >=700MB free to stage the bundle.\n");
    } else {
        fprintf(LOG, "\nFATAL: USB bundle (external/vpx/VPinballX_BGFX) not found on any mount.\n");
    }

    if (!RUN[0]) { if (LOG && LOG != stderr) fclose(LOG); distribute_log(); return 1; }

    char home[PATH_MAX]; snprintf(home, sizeof home, "%s/op-home", scratch); mkdir(home, 0755);
    setenv("HOME", home, 1);
    setenv("LD_LIBRARY_PATH", RUN, 1);
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);
    fprintf(LOG, "\nRUNDIR = %s  (from_usb=%d)\nHOME = %s\n----- VPX output below -----\n", RUN, from_usb, home);
    if (LOG && LOG != stderr) fclose(LOG);

    pid_t pid = fork();
    if (pid == 0) {
        chdir(RUN);
        int fd = open(LOGP, O_WRONLY|O_CREAT|O_APPEND, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execl("./VPinballX_BGFX", "VPinballX_BGFX", "-play", "exampleTable.vpx", (char *)NULL);
        fprintf(stderr, "\n[harness] exec of %s/VPinballX_BGFX failed: %s\n", RUN, strerror(errno));
        _exit(127);
    }

    int status = 0, exited = 0;
    for (int s = 0; s < RUN_TIMEOUT; s++) { if (waitpid(pid, &status, WNOHANG) == pid) { exited = 1; break; } sleep(1); }

    FILE *af = fopen(LOGP, "a");
    if (af) {
        if (exited) {
            if (WIFEXITED(status))  fprintf(af, "\n[harness] VPX exited, code %d\n", WEXITSTATUS(status));
            else if (WIFSIGNALED(status)) fprintf(af, "\n[harness] VPX killed by signal %d\n", WTERMSIG(status));
        } else fprintf(af, "\n[harness] VPX still running after %ds -> black-screen hang. Killing.\n", RUN_TIMEOUT);
        fclose(af);
    }
    if (!exited) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }

    distribute_log();
    return 0;
}
