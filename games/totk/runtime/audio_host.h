/* Host output (CoreAudio) interruption. The audren renderer keeps running. */
#pragma once

void audio_diag(const char *fmt, ...);
/* began=1 stops the output unit and drops queued samples. began=0 starts it again and re-primes. */
int audio_host_set_interrupted(int began);
int audio_host_is_paused(void);
/* 1 when the live output should be started again (stopped, interrupted, or callbacks have stalled). */
int audio_host_needs_restart(void);
/* rebuild=1 disposes the AudioUnit and opens a new one (media services reset). */
int audio_host_restart(int rebuild);
/* iOS session layer: activate AVAudioSession, then restart. NULL uses audio_host_restart directly. */
void audio_host_set_restart_hook(void (*fn)(void));
/* "flag audiorestart 1". Returns 1 if the name was handled. */
int audio_host_restart_flag(const char *name, int v);
