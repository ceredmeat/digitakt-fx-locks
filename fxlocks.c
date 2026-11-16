/* SPDX-License-Identifier: GPL-2.0-or-later */
/* FX Locks for the Digitakt mk1 (OS 1.54): parameter locks on the delay and the reverb, from the
 * audio track itself. Hold a trig (or several) and turn the knob on the delay or reverb page, as for
 * any lock.
 *
 *   Delay  TIME (in note values: 1/16, 1/8., 1/4 ...), WID (stereo width), FDBK (feedback)
 *   Reverb DEC (decay)
 *
 * When the step plays, the shared delay/reverb takes the locked value; it keeps it until that track's
 * next trig (as every Digitakt lock does), then goes back to the pattern's own setting. When two
 * tracks hold a lock on the same FX knob at once, the higher track wins.
 *
 * How it works (OS 1.54, read from its code):
 *   - a step's locks are 53 words, one a sound slot (-1: none): 0x40023c7a(track pattern, step, slot)
 *     reads one, 0x40024768(track pattern, step, slot, value) writes one; the track pattern of track t
 *     is 0x40012a02(0x40015786(app), t). Sound slots 48-51 are used by no audio machine: they hold
 *     the FX locks (TIME, WID, FDBK, DEC). The firmware's own trig code then plays them like any lock:
 *     all 53 slots are collected (0x4006f46e), so nothing else is needed for copying, saving or
 *     clearing them.
 *   - playing: as each trig fires, seq-events' ev_fire_locks gives its track and step; fxl_locks
 *     reads our four slots of that step and keeps them per track until the track's next trig. (0.1
 *     waited for the firmware's lock flags, 0x439a9e14 + 8 t: on the unit they never showed these
 *     slots.) Once a render block (0x40077fc2, after the tracks are rendered) fxl_render writes the
 *     winning lock into the FX's own values, both the Q31 one (0x80002b2c + 4 (53 x 8 + 9 + FX slot))
 *     and the 8.8 one (0x800014f0 + 2 (...)), as the firmware's parameter write 0x40074aea does, or
 *     puts back the pattern's own value (the pattern's FX words, *0x800019ac + 1328).
 *   - the knob: with steps held, a page's encoder handler (0x40032a78) calls the view's
 *     "turn for the held steps" (0x40031586, the view's 31st method). fxlocks.s jumps to fxl_enc
 *     first; for our four knobs it writes the locks itself (the stock code would refuse: these are
 *     not sound parameters). The held steps are a 64-bit mask in the step-hold object
 *     (0x40138928(): +344 high, +348 low, +352 "any held").
 *   - what the knob shows: 0x4003171c(view, id, &locked) is the value a page view shows, a held
 *     step's lock first; fxl_shown answers for our four with the first held step's FX lock. */
#include <stdint.h>

typedef uint32_t (*fn0)(void);
typedef uint32_t (*fn1)(uint32_t);
typedef uint32_t (*fn2)(uint32_t, uint32_t);
typedef int32_t (*lock_read_fn)(uint32_t pat, int32_t step, int32_t slot);
typedef int32_t (*lock_write_fn)(uint32_t pat, int32_t step, int32_t slot, int32_t value);
#define APP()          (((fn0)0x40138882)())
#define PROJECT(a)     (((fn1)0x40014d86)(a))
#define ACTIVE(p)      (((fn1)0x4001d24e)(p))
#define PATTERNS(a)    (((fn1)0x40015786)(a))
#define TRACK_PAT(s,t) (((fn2)0x40012a02)((s), (t)))
#define HELD()         (((fn0)0x40138928)())
#define LOCK_READ      ((lock_read_fn)0x40023c7a)
#define LOCK_WRITE     ((lock_write_fn)0x40024768)

#define Q31(k)         ((volatile int32_t *)(0x80002b2cu + 4u * (uint32_t)(k)))
#define FX_SRC         (*(const uint8_t *const volatile *)0x800019acu)
#define P88(k)         ((volatile int16_t *)(0x800014f0u + 2u * (uint32_t)(k)))   /* the render's 8.8 copy */

#define N 4
static const int16_t fx_id[N]   = {70, 72, 73, 79};    /* Delay TIME, WID, FDBK; Reverb DEC */
static const uint8_t fx_slot[N] = {17, 19, 20, 26};    /* their slots in the FX record */
static const uint8_t lk_slot[N] = {48, 49, 50, 51};    /* the sound slots their locks use */
static uint8_t overridden[N];

/* 0.2: delay TIME locks move in note values only. TIME counts 128th notes (value + 1); the stock
 * pop-up names these values (its table at 0x400662fe): 1/128, 1/64, 1/64., 1/32, 1/32., 1/16, 1/16.,
 * 1/8, 1/8., 1/4, 1/4., 1/2, 1/2., 1/1. */
static const uint8_t notes[] = {0, 1, 2, 3, 5, 7, 11, 15, 23, 31, 47, 63, 95, 127};
#define NOTES ((int32_t)sizeof notes)

static int32_t snap_step(int32_t v, int32_t delta)      /* v 8.8 -> the next note value, 8.8 */
{
    int32_t i, best = 0, d, bd = 0x7fffffff, x = v >> 8;
    for (i = 0; i < NOTES; i++) {
        d = x > notes[i] ? x - notes[i] : notes[i] - x;
        if (d < bd) { bd = d; best = i; }
    }
    if (delta > 0 && best < NOTES - 1 && (bd == 0 || notes[best] <= x)) best++;
    if (delta < 0 && best > 0 && (bd == 0 || notes[best] >= x)) best--;
    return notes[best] << 8;
}

static int32_t which(int32_t id)
{
    int32_t p;
    for (p = 0; p < N; p++)
        if (fx_id[p] == id)
            return p;
    return -1;
}

static int32_t held_step(const uint8_t *h, int32_t n)        /* is step n held */
{
    uint32_t hi = *(const uint32_t *)(h + 344), lo = *(const uint32_t *)(h + 348);
    return n < 32 ? (int32_t)((lo >> n) & 1u) : (int32_t)((hi >> (n - 32)) & 1u);
}

/* the pattern's own value of FX knob p, 8.8 */
static int32_t fx_base(int32_t p)
{
    const uint8_t *src = FX_SRC;
    const uint8_t *w;
    if (!src)
        return 0;
    w = src + 1328 + 2 * fx_slot[p];
    return (int16_t)((w[0] << 8) | w[1]);
}

/* ---- the knob, with steps held (UI task) ---------------------------------------------------------- */
/* -> -1: not ours (the stock code goes on), else what the stock routine returns (1: done) */
int32_t fxl_enc(void *view, int32_t id, int32_t delta)
{
    int32_t p = which(id), n, any = 0;
    uint32_t app, track, pat;
    const uint8_t *h;
    (void)view;
    if (p < 0)
        return -1;
    h = (const uint8_t *)HELD();
    if (!h || !h[352])
        return -1;
    app = APP();
    track = ACTIVE(PROJECT(app));
    if (track >= 8)
        return -1;                              /* audio tracks only */
    pat = TRACK_PAT(PATTERNS(app), track);
    for (n = 0; n < 64; n++) {
        int32_t v;
        if (!held_step(h, n))
            continue;
        v = LOCK_READ(pat, n, lk_slot[p]);
        if (v < 0)
            v = fx_base(p);                     /* a new lock starts from the pattern's value */
        if (p == 0)
            v = snap_step(v, delta);            /* TIME: note values only */
        else
            v += delta;
        if (v < 0) v = 0;
        if (v > 0x7f00) v = 0x7f00;
        LOCK_WRITE(pat, n, lk_slot[p], v);
        any = 1;
    }
    return any;
}

/* what the knob shows: -1 not ours (stock), else the first held step's FX lock, *locked = 1 */
int32_t fxl_shown(void *view, int32_t id, uint8_t *locked)
{
    int32_t p = which(id), n;
    uint32_t app, track, pat;
    const uint8_t *h;
    (void)view;
    if (p < 0)
        return -1;
    h = (const uint8_t *)HELD();
    if (!h || !h[352])
        return -1;
    app = APP();
    track = ACTIVE(PROJECT(app));
    if (track >= 8)
        return -1;
    pat = TRACK_PAT(PATTERNS(app), track);
    for (n = 0; n < 64; n++) {
        int32_t v;
        if (!held_step(h, n))
            continue;
        v = LOCK_READ(pat, n, lk_slot[p]);
        if (v >= 0) {
            if (locked)
                *locked = 1;
            return v;
        }
        break;                                  /* the first held step has none: the FX's own value */
    }
    return -1;
}

/* ---- playing --------------------------------------------------------------------------------------- *
 * 0.2: the firmware's own lock flags never showed our slots on the unit, so the mod reads the locks
 * itself. seq-events calls fxl_locks as each trig fires (interrupt level), with its track and step;
 * we read our four slots of that step (the same lock read as the knob) and keep, per track, what each
 * FX knob is locked to (-1: not). That holds until the track's next trig, as a Digitakt lock does.
 * The track patterns are looked up in the UI tick, where the app's routines are safe to call. */
#include "digitakt-mk1/seq.h"
static volatile int16_t claim[N][8];
static volatile uint32_t pats[8];

int32_t fxl_locks(int32_t track, int32_t step, struct sq_lock *out, int32_t room)
{
    int32_t p;
    uint32_t pat;
    (void)out; (void)room;
    if (track < 0 || track >= 8 || !(pat = pats[track]))
        return 0;
    for (p = 0; p < N; p++)
        claim[p][track] = (int16_t)LOCK_READ(pat, step, lk_slot[p]);
    return 0;
}

static int32_t inited;
static void init_claims(void)
{
    int32_t p, t;
    for (p = 0; p < N; p++)
        for (t = 0; t < 8; t++)
            claim[p][t] = -1;
    inited = 1;
}

void fxl_render(void)
{
    int32_t p, t;
    if (!inited)
        return;
    for (p = 0; p < N; p++) {
        int32_t best = -1, k = 53 * 8 + 9 + fx_slot[p];
        for (t = 0; t < 8; t++)
            if (claim[p][t] >= 0)
                best = t;
        if (best >= 0) {
            int32_t v = claim[p][best];
            *Q31(k) = v << 16;
            *P88(k) = (int16_t)v;
            overridden[p] = 1;
        } else if (overridden[p]) {
            int32_t v = fx_base(p);
            *Q31(k) = v << 16;
            *P88(k) = (int16_t)v;
            overridden[p] = 0;
        }
    }
}

void fxl_tick(void *ctrl)
{
    uint32_t app = APP(), s = PATTERNS(app);
    int32_t t;
    (void)ctrl;
    if (!inited)
        init_claims();
    for (t = 0; t < 8; t++)
        pats[t] = TRACK_PAT(s, (uint32_t)t);
}
