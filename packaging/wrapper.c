/*
 * OpenPin4K -- VPX launch harness.  Compiled to "vpx.elf".
 *
 * The AtGames "External Applications" launcher runs external/<name>/<name>.elf.
 * First on-cabinet run (2026-06-27): the app launched but showed a BLACK SCREEN
 * and NO vpx-log.txt appeared on the USB -- meaning the launcher almost certainly
 * COPIES the app to internal storage and runs it there (so our log landed on
 * internal, unreachable), or the USB is mounted read-only.
 *
 * So this harness is built to ALWAYS get a log back to the user:
 *   1. Resolve our own folder (D) and chdir there so bundled libs/table are found.
 *   2. Pick a guaranteed-writable scratch dir for the log + VPX's HOME/config
 *      (handles a read-only USB).
 *   3. Run VPinballX_BGFX with output captured, under a TIMEOUT (so a black-screen
 *      hang doesn't block forever -- it auto-closes and we still get the log).
 *   4. Copy the log onto every FAT/exFAT USB stick that holds this app (found via
 *      /proc/mounts), so vpx-log.txt shows up on the stick regardless of where the
 *      app actually executed from.
 *
 * NOTE: because of the timeout, this diagnostic build auto-closes after ~60s even
 * if the table renders fine -- that's expected; it's enough time to photograph
 * success, and we remove the timeout once it's working.
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

#define RUN_TIMEOUT_SECONDS 60

static int dir_writable(const char *d)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/.op_wtest", d);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    close(fd); unlink(p);
    return 1;
}

static void copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return; }
    char buf[65536]; ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        ssize_t w = 0;
        while (w < n) { ssize_t k = write(out, buf + w, n - w); if (k <= 0) break; w += k; }
    }
    close(in); close(out);
}

/* Copy the log onto any FAT/exFAT volume that contains this app. */
static void distribute_to_usb(const char *logp)
{
    FILE *m = fopen("/proc/mounts", "r");
    if (!m) return;
    char line[1024], dev[256], mp[PATH_MAX], fs[64];
    while (fgets(line, sizeof line, m)) {
        if (sscanf(line, "%255s %4095s %63s", dev, mp, fs) != 3) continue;
        if (strcmp(fs, "vfat") && strcmp(fs, "exfat") && strcmp(fs, "msdos") &&
            strcmp(fs, "fuseblk") && strcmp(fs, "texfat")) continue;
        const char *subs[] = { "external/vpx", "vpx", ".", NULL };
        for (int i = 0; subs[i]; i++) {
            char chk[PATH_MAX], dst[PATH_MAX];
            snprintf(chk, sizeof chk, "%s/%s/VPinballX_BGFX", mp, subs[i]);
            if (access(chk, F_OK) == 0) {
                snprintf(dst, sizeof dst, "%s/%s/vpx-log.txt", mp, subs[i]);
                copy_file(logp, dst);
            }
        }
    }
    fclose(m);
}

int main(int argc, char **argv)
{
    char exe[PATH_MAX], D[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n] = 0; strncpy(D, exe, sizeof D - 1); D[sizeof D - 1] = 0;
                 char *s = strrchr(D, '/'); if (s) *s = 0; }
    else if (!getcwd(D, sizeof D)) strcpy(D, ".");
    if (chdir(D) != 0) { /* cwd may already be correct */ }

    /* Guaranteed-writable scratch for the log + VPX's HOME (covers read-only USB). */
    const char *scratch = D;
    const char *cands[] = { "/tmp", "/dev/shm", "/data/local/tmp", "/var/tmp", D, NULL };
    for (int i = 0; cands[i]; i++) if (dir_writable(cands[i])) { scratch = cands[i]; break; }

    char logp[PATH_MAX], home[PATH_MAX];
    snprintf(logp, sizeof logp, "%s/vpx-log.txt", scratch);
    snprintf(home, sizeof home, "%s/op-home", scratch);
    mkdir(home, 0755);
    setenv("HOME", home, 1);
    setenv("LD_LIBRARY_PATH", D, 1);
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);

    /* Header: prove the harness ran and record the environment. */
    FILE *lf = fopen(logp, "w");
    if (lf) {
        time_t t = time(NULL);
        fprintf(lf, "==== OpenPin4K VPX launch harness ====\n");
        fprintf(lf, "time            : %s", ctime(&t));
        fprintf(lf, "app dir (D)     : %s (writable=%d)\n", D, dir_writable(D));
        fprintf(lf, "scratch/HOME    : %s | %s\n", scratch, home);
        fprintf(lf, "VPinballX_BGFX  : %s\n", access("VPinballX_BGFX", F_OK) == 0 ? "present" : "MISSING");
        fprintf(lf, "exampleTable.vpx: %s\n", access("exampleTable.vpx", F_OK) == 0 ? "present" : "MISSING");
        fprintf(lf, "----- VPX output below -----\n");
        fclose(lf);
    }

    /* Run VPX with stdout/stderr appended to the log. */
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(logp, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        const char *table = (argc > 1) ? argv[argc - 1] : "exampleTable.vpx";
        execl("./VPinballX_BGFX", "VPinballX_BGFX", "-play", table, (char *)NULL);
        fprintf(stderr, "\n[harness] exec of ./VPinballX_BGFX failed: %s\n", strerror(errno));
        _exit(127);
    }

    int status = 0, exited = 0;
    for (int s = 0; s < RUN_TIMEOUT_SECONDS; s++) {
        if (waitpid(pid, &status, WNOHANG) == pid) { exited = 1; break; }
        sleep(1);
    }

    FILE *af = fopen(logp, "a");
    if (af) {
        if (exited) {
            if (WIFEXITED(status))  fprintf(af, "\n[harness] VPX exited, code %d\n", WEXITSTATUS(status));
            else if (WIFSIGNALED(status)) fprintf(af, "\n[harness] VPX killed by signal %d\n", WTERMSIG(status));
        } else {
            fprintf(af, "\n[harness] VPX still running after %ds -> black-screen HANG (not a crash). Killing.\n",
                    RUN_TIMEOUT_SECONDS);
        }
        fclose(af);
    }
    if (!exited) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }

    /* Get the log back to the user: app dir (if writable) + any USB stick. */
    if (dir_writable(D)) { char dst[PATH_MAX]; snprintf(dst, sizeof dst, "%s/vpx-log.txt", D); copy_file(logp, dst); }
    distribute_to_usb(logp);
    return 0;
}
