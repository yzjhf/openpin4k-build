/*
 * OpenPin4K -- VPX launch wrapper.  Compiled to "vpx.elf".
 *
 * The AtGames "External Applications" launcher execs external/<name>/<name>.elf
 * with the working directory set to that folder (proven in Phase 0: disptest
 * loaded "res/NotoSans-Regular.ttf" by relative path and it worked on-cabinet).
 *
 * VPX's real binary is VPinballX_BGFX, which (a) needs its bundled .so libs on
 * the library path and (b) takes "-play <table>" to load a table.  This tiny
 * native wrapper sets that up, redirects all output to a log file on the USB so
 * a failed run is diagnosable WITHOUT another cabinet trip, then execs VPX.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

int main(int argc, char **argv)
{
    /* Resolve our own folder robustly (don't depend on the launcher's cwd). */
    char dir[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (n > 0) {
        dir[n] = '\0';
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
    } else if (!getcwd(dir, sizeof(dir))) {
        strcpy(dir, ".");
    }

    if (chdir(dir) != 0) { /* keep going; cwd may already be correct */ }

    /* Bundled libraries live next to us; VPX may write config under HOME. */
    setenv("LD_LIBRARY_PATH", dir, 1);
    setenv("HOME", dir, 1);
    /* Match the Phase 0 video path, but let an existing value win. */
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);

    /* Capture everything VPX prints so we can debug off-device. */
    char logp[PATH_MAX];
    snprintf(logp, sizeof(logp), "%s/vpx-log.txt", dir);
    int fd = open(logp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
    fprintf(stderr, "[openpin4k] launching VPX from: %s\n", dir);
    fflush(stderr);

    /* Default to the bundled example table; allow a future launcher arg to override. */
    const char *table = (argc > 1) ? argv[argc - 1] : "exampleTable.vpx";
    execl("./VPinballX_BGFX", "VPinballX_BGFX", "-play", table, (char *)NULL);

    /* Only reached if exec failed. */
    fprintf(stderr, "[openpin4k] exec of ./VPinballX_BGFX failed: %s\n", strerror(errno));
    return 127;
}
