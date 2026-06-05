#include "vox-audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

//
// Static state — this module manages a single capture stream.
// There is only one audio sink to monitor (the Gqrx null-sink), so one
// pipe is sufficient.
//
static FILE *audio_fp = NULL;

//
// Saved SIGPIPE handler so we can restore it on shutdown.
// popen() gives us a pipe to a child process (pw-cat).  If pw-cat crashes
// or disconnects while we are trying to fread(), the kernel delivers
// SIGPIPE to us.  The default action for SIGPIPE is to terminate the
// process — which would kill the scanner.  We ignore it instead so that
// fread() simply returns 0 / EOF and we can detect the loss of audio
// gracefully.
//
static struct sigaction old_sa;

// ---------------------------------------------------------------------------
// Primary implementation — PipeWire (pw-cat)
//
// Dependency: pipewire-bin (pw-cat, pw-cli, pw-link, pw-loopback)
//   sudo apt install pipewire-bin
//
// The monitor source "gqrx-scanner-intercept.monitor" must exist before
// calling this — run gqrx-scan-setup-audio.sh attach first.
// ---------------------------------------------------------------------------

bool VoxAudioInit(void)
{
    struct sigaction sa;
    int rc;

    // Idempotent — if the pipe is already open there is nothing to do.
    if (audio_fp != NULL)
        return true;

    // ------ SIGPIPE handling ------
    //
    // 1. Query current SIGPIPE disposition (don't assume SIG_DFL).
    // 2. Override with SIG_IGN so that pw-cat dying does not take us down.
    // 3. Save the old handler for restoration in VoxAudioShutdown().
    //
    // The query can fail (e.g. EINVAL), but that is non-fatal — we log
    // and continue; the sigaction that follows will still work because
    // old_sa is filled with whatever the query wrote (undefined on error,
    // but SIG_IGN is still safer than the default).
    if (sigaction(SIGPIPE, NULL, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not query SIGPIPE handler: %s\n",
                strerror(errno));

    sa = old_sa;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not ignore SIGPIPE: %s\n",
                strerror(errno));

    // ------ Probe for pw-cat ------
    //
    // We use "command -v pw-cat" (POSIX) instead of "which" because
    // "which" is not guaranteed to be present on every system.  The
    // exit status tells us whether pw-cat is reachable in $PATH.
    //
    // We check separately rather than letting the real popen fail
    // because popen() itself succeeds as long as fork() works — the
    // shell will just print "not found" to stderr and exit 127.  By
    // probing first we can print a specific, helpful error message.
    //
    FILE *probe = popen("command -v pw-cat >/dev/null 2>&1", "r");
    if (probe == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to probe for pw-cat: %s\n", strerror(errno));
        goto fail_sig;
    }
    rc = pclose(probe);
    if (rc != 0)
    {
        fprintf(stderr, "[ ERROR ] pw-cat not found in PATH.\n"
                "         Install pipewire-bin:\n"
                "           sudo apt install pipewire-bin\n");
        goto fail_sig;
    }

    // ------ Open capture pipe ------
    //
    // Command: pw-cat --record -d <monitor> --channels=1 --format=u8
    //   --rate=8000 --raw
    //
    // -d gqrx-scanner-intercept.monitor
    //   The monitor source of the null sink created by
    //   gqrx-scan-setup-audio.sh.  The monitor carries only the audio
    //   that was routed to that null sink, i.e. the target app's output.
    //
    // --channels=1 --format=u8 --rate=8000 --raw
    //   Raw unsigned 8-bit PCM at 8 kHz, mono.  This is good enough for
    //   voice-activity detection and keeps the data rate low (~8 kB/s).
    //   pw-cat decodes from PipeWire's internal format into u8 for us.
    //
    // 2>/dev/null
    //   Suppress pw-cat's own error messages (e.g. "Connection refused"
    //   when PipeWire is not running).  We detect failure via fread()
    //   returning 0/EOF instead.
    //
    audio_fp = popen("pw-cat --record -d gqrx-scanner-intercept.monitor "
                     "--channels=1 --format=u8 --rate=8000 "
                     "--raw 2>/dev/null", "r");
    if (audio_fp == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to start audio capture: %s\n"
                "         Make sure gqrx-scan-setup-audio.sh is running\n",
                strerror(errno));
        goto fail_sig;
    }

    return true;

    // ------ Error exit ------
    //
    // If anything went wrong we must undo the SIGPIPE override before
    // returning, otherwise the caller could later crash on a SIGPIPE
    // that they were not expecting.
fail_sig:
    sigaction(SIGPIPE, &old_sa, NULL);
    return false;
}

void VoxAudioShutdown(void)
{
    int rc;

    // Guard against double-shutdown or shutdown-without-init.
    if (audio_fp == NULL)
        return;

    // Close the pipe and reap the pw-cat child process.
    //
    // pclose() waits for the child to exit and returns its exit status.
    // A non-zero exit is not necessarily an error — pw-cat exits with
    // code 0 when it receives SIGTERM, but may exit non-zero if it
    // encountered a problem (e.g. monitor source disappeared).  We log
    // a warning in any case so that surprising failures are visible.
    rc = pclose(audio_fp);
    audio_fp = NULL;

    if (rc != 0)
    {
        fprintf(stderr, "[ WARNING ] Audio capture exited unexpectedly");
        if (WIFEXITED(rc))
            fprintf(stderr, " (status %d)", WEXITSTATUS(rc));
        if (WIFSIGNALED(rc))
            fprintf(stderr, " (signal %d)", WTERMSIG(rc));
        fprintf(stderr, ". "
                "Check that PipeWire is running and "
                "gqrx-scan-setup-audio.sh is attached.\n");
    }

    // Restore SIGPIPE to whatever it was before we changed it.
    // This is critical for politeness — other code in the process may
    // rely on the default SIGPIPE behaviour.
    sigaction(SIGPIPE, &old_sa, NULL);
}

// ---------------------------------------------------------------------------
// Reference implementation — PulseAudio (pacat), kept for debug
//
// Alternative to the primary functions above.  Uses pacat instead of
// pw-cat, which requires pulseaudio-utils:
//   sudo apt install pulseaudio-utils
//
// Only difference from the primary version is the tool name, flags, and
// error messages.  The SIGPIPE / state management logic is identical.
// ---------------------------------------------------------------------------

//
// VoxAudioInit_PA
//   Same as VoxAudioInit() but uses pacat instead of pw-cat.
//   Intended as a drop-in alternative for debugging on systems where
//   PulseAudio is used instead of PipeWire.
//
bool VoxAudioInit_PA(void)
{
    struct sigaction sa;
    int rc;

    if (audio_fp != NULL)
        return true;

    if (sigaction(SIGPIPE, NULL, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not query SIGPIPE handler: %s\n",
                strerror(errno));

    sa = old_sa;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not ignore SIGPIPE: %s\n",
                strerror(errno));

    FILE *probe = popen("command -v pacat >/dev/null 2>&1", "r");
    if (probe == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to probe for pacat: %s\n", strerror(errno));
        goto fail_sig;
    }
    rc = pclose(probe);
    if (rc != 0)
    {
        fprintf(stderr, "[ ERROR ] pacat not found in PATH.\n"
                "         Install pulseaudio-utils:\n"
                "           sudo apt install pulseaudio-utils\n");
        goto fail_sig;
    }

    audio_fp = popen("pacat --record -d gqrx-scanner-intercept.monitor "
                     "--channels=1 --format=u8 --rate=8000 "
                     "--file-format=raw 2>/dev/null", "r");
    if (audio_fp == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to start audio capture: %s\n"
                "         Make sure gqrx-scan-setup-audio.sh is running\n",
                strerror(errno));
        goto fail_sig;
    }

    return true;

fail_sig:
    sigaction(SIGPIPE, &old_sa, NULL);
    return false;
}

//
// VoxAudioShutdown_PA
//   Same as VoxAudioShutdown() — shuts down the pacat capture pipe.
//   Since the static FILE pointer is shared with the primary functions,
//   this works regardless of which init variant was called.
//
void VoxAudioShutdown_PA(void)
{
    int rc;

    if (audio_fp == NULL)
        return;

    rc = pclose(audio_fp);
    audio_fp = NULL;

    if (rc != 0)
    {
        fprintf(stderr, "[ WARNING ] Audio capture exited unexpectedly");
        if (WIFEXITED(rc))
            fprintf(stderr, " (status %d)", WEXITSTATUS(rc));
        if (WIFSIGNALED(rc))
            fprintf(stderr, " (signal %d)", WTERMSIG(rc));
        fprintf(stderr, ". "
                "Check that PulseAudio/PipeWire is running and "
                "gqrx-scan-setup-audio.sh is attached.\n");
    }

    sigaction(SIGPIPE, &old_sa, NULL);
}
