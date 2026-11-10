/* Copyright (c) 2026 Andy (Yong) Liu <liuyong@kaka888.com> */
/* SPDX-License-Identifier: MIT */

#include <REG52.H>

/*
 * Sedentary reminder: 11.0592 MHz crystal, 8051 in 12T mode.
 * Default sitting limit: 30 minutes. Triple-click P1.1 in standby
 * to toggle the one-minute test mode.
 * P1.0 and P1.1 use momentary switches to GND and internal pull-ups.
 */
#define SIT_LIMIT_NORMAL  1800U
#define SIT_LIMIT_TEST    60U
#define DEBOUNCE_MS       20
#define HOLD_START_MS     250
#define TRIPLE_WINDOW_MS  1000
#define MODE_SHOW_MS      2000
#define SHORT_BEEP_MS     50
#define READY_BEEP_MS     1000

#define STATE_READY       0
#define STATE_SITTING     1
#define STATE_BREAK       2
#define STATE_MUST_REST   3

sbit BEEP = P2^0;                 /* Active high */
sbit DIG1 = P2^4;
sbit DIG2 = P2^3;
sbit DIG3 = P2^2;
sbit DIG4 = P2^1;
sbit SET_KEY = P1^0;              /* Active low: adjust rest minutes */
sbit SIT_KEY = P1^1;              /* Active low: sitting */

/* P0 bits 7..0 = G, C, DP, D, E, B, F, A. Common anode; 0 lights a segment. */
unsigned char code SEG_CODE[10] =
{
    0xA0, 0xBB, 0x62, 0x2A, 0x39,
    0x2C, 0x24, 0xBA, 0x20, 0x28
};

unsigned char state = STATE_READY;
unsigned char rest_minutes = 10;
unsigned int sit_limit_seconds = SIT_LIMIT_NORMAL;
unsigned int sit_seconds = 0;
unsigned int sit_ms = 0;
unsigned int rest_seconds = 0;
unsigned int rest_ms = 0;
unsigned int ready_beep_ms = 0;
unsigned char short_beep_ms = 0;
unsigned char beep_times = 0;
unsigned char beep_done = 0;
unsigned int beep_interval_ms = 0;
unsigned int colon_ms = 0;
unsigned int tap_window_ms = 0;
unsigned int pending_press_ms = 0;
unsigned int mode_show_ms = 0;
unsigned char tap_count = 0;
bit press_pending = 0;
bit colon_on = 1;

unsigned char scan_pos = 0;
unsigned char display_digits[4] = {0, 0, 0, 0};
bit display_dirty = 1;

bit sit_pressed = 0;
bit set_pressed = 0;
unsigned char sit_debounce = 0;
unsigned char set_debounce = 0;

void Digits_Off(void)
{
    DIG1 = 0;
    DIG2 = 0;
    DIG3 = 0;
    DIG4 = 0;
}

void Select_Digit(unsigned char n)
{
    switch(n)
    {
        case 0: DIG1 = 1; break;
        case 1: DIG2 = 1; break;
        case 2: DIG3 = 1; break;
        default: DIG4 = 1; break;
    }
}

/* Recalculate digits only when the time or mode changes, not on each scan. */
void Update_Display(void)
{
    unsigned int value;
    unsigned char minutes;
    unsigned char seconds;

    if(state == STATE_SITTING)
    {
        value = sit_seconds;
    }
    else if(state == STATE_BREAK || state == STATE_MUST_REST)
    {
        value = rest_seconds;
    }
    else if(mode_show_ms != 0)
    {
        /* Briefly show the selected sitting limit, then the rest setting. */
        value = sit_limit_seconds;
    }
    else
    {
        /* Show the configured rest time in standby; default is 10:00. */
        value = (unsigned int)rest_minutes * 60;
    }

    minutes = (unsigned char)(value / 60);
    seconds = (unsigned char)(value % 60);
    display_digits[0] = minutes / 10;
    display_digits[1] = minutes % 10;
    display_digits[2] = seconds / 10;
    display_digits[3] = seconds % 10;
    display_dirty = 0;
}

void Display_Scan(void)
{
    unsigned char seg;

    Digits_Off();
    seg = SEG_CODE[display_digits[scan_pos]];

    /* Keep the colon lit while showing the rest setting in standby. */
    if(colon_on || state == STATE_READY)
    {
        if(scan_pos == 1 || scan_pos == 2)
        {
            seg &= 0xDF;
        }
    }

    P0 = seg;
    Select_Digit(scan_pos);
    scan_pos++;
    if(scan_pos >= 4)
    {
        scan_pos = 0;
    }
}

/* Start short beeps with 30 seconds left; add one per second in the last 10. */
void Start_Second_Beeps(unsigned int remaining)
{
    if(remaining == 0 || remaining > 30)
    {
        beep_times = 0;
        beep_done = 0;
        return;
    }

    beep_times = (remaining > 10) ? 1 : (unsigned char)(11 - remaining);
    beep_done = 1;
    beep_interval_ms = 0;
    short_beep_ms = SHORT_BEEP_MS;
}

void Finish_Rest(void)
{
    state = STATE_READY;
    sit_seconds = 0;
    sit_ms = 0;
    rest_seconds = 0;
    rest_ms = 0;
    beep_times = 0;
    beep_done = 0;
    short_beep_ms = 0;
    ready_beep_ms = READY_BEEP_MS;
    display_dirty = 1;
}

void Timer0_Init(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x01;                 /* Timer0 mode 1 */
    TH0 = 0xFC;
    TL0 = 0x66;                  /* 922 machine cycles, about 1 ms */
    ET0 = 1;
    EA = 1;
    TR0 = 1;
}

void Timer0_ISR(void) interrupt 1
{
    unsigned char sit_changed = 0;
    unsigned char set_clicked = 0;
    unsigned char raw;
    unsigned int interval;
    unsigned int remaining;

    TH0 = 0xFC;
    TL0 = 0x66;

    if(tap_window_ms != 0)
    {
        tap_window_ms--;
        if(tap_window_ms == 0) tap_count = 0;
    }
    if(mode_show_ms != 0)
    {
        mode_show_ms--;
        if(mode_show_ms == 0) display_dirty = 1;
    }
    if(short_beep_ms != 0) short_beep_ms--;
    if(ready_beep_ms != 0) ready_beep_ms--;

    /* Both inputs are active low; accept changes stable for 20 ms. */
    raw = (SIT_KEY == 0);
    if(raw == sit_pressed)
    {
        sit_debounce = 0;
    }
    else if(++sit_debounce >= DEBOUNCE_MS)
    {
        sit_pressed = raw;
        sit_debounce = 0;
        sit_changed = 1;
    }

    raw = (SET_KEY == 0);
    if(raw == set_pressed)
    {
        set_debounce = 0;
    }
    else if(++set_debounce >= DEBOUNCE_MS)
    {
        set_pressed = raw;
        set_debounce = 0;
        if(set_pressed) set_clicked = 1;
    }

    if(sit_changed)
    {
        if(sit_pressed)
        {
            if(state == STATE_READY)
            {
                /* Defer sitting until a hold is confirmed so taps can toggle modes. */
                if(tap_window_ms == 0)
                {
                    tap_window_ms = TRIPLE_WINDOW_MS;
                    tap_count = 0;
                }
                press_pending = 1;
                pending_press_ms = 0;
            }
            else if(state == STATE_BREAK)
            {
                /* Cancel this unfinished rest and resume accumulated sitting time. */
                state = STATE_SITTING;
                rest_seconds = 0;
                rest_ms = 0;
                display_dirty = 1;
            }
            /* Keep STATE_MUST_REST; sitting again resumes the continuous alarm. */
        }
        else if(state == STATE_READY && press_pending)
        {
            press_pending = 0;
            if(pending_press_ms < HOLD_START_MS && tap_window_ms != 0)
            {
                tap_count++;
                if(tap_count >= 3)
                {
                    sit_limit_seconds = (sit_limit_seconds == SIT_LIMIT_NORMAL)
                                        ? SIT_LIMIT_TEST : SIT_LIMIT_NORMAL;
                    tap_count = 0;
                    tap_window_ms = 0;
                    mode_show_ms = MODE_SHOW_MS;
                    display_dirty = 1;
                }
            }
        }
        else if(state == STATE_SITTING)
        {
            /* Pause sitting before the limit and start a full rest period. */
            state = STATE_BREAK;
            rest_seconds = (unsigned int)rest_minutes * 60;
            rest_ms = 0;
            short_beep_ms = 0;
            display_dirty = 1;
        }
    }

    /* Settings are locked during sitting and both rest states. */
    if(set_clicked && state == STATE_READY && !sit_pressed)
    {
        rest_minutes++;
        if(rest_minutes > 10) rest_minutes = 1;
        mode_show_ms = 0;
        display_dirty = 1;
    }

    if(state == STATE_READY && sit_pressed && press_pending)
    {
        pending_press_ms++;
        if(pending_press_ms >= HOLD_START_MS)
        {
            /* Count the hold-detection interval toward sitting time. */
            state = STATE_SITTING;
            sit_seconds = 0;
            sit_ms = pending_press_ms;
            beep_times = 0;
            beep_done = 0;
            press_pending = 0;
            tap_count = 0;
            tap_window_ms = 0;
            mode_show_ms = 0;
            display_dirty = 1;
        }
    }

    if(state == STATE_SITTING)
    {
        /* Beep at each second's start; the tenth final beep occurs at 900 ms. */
        if(beep_done < beep_times)
        {
            interval = 1000 / beep_times;
            beep_interval_ms++;
            if(beep_interval_ms >= interval)
            {
                beep_interval_ms = 0;
                beep_done++;
                short_beep_ms = SHORT_BEEP_MS;
            }
        }

        sit_ms++;
        if(sit_ms >= 1000)
        {
            sit_ms = 0;
            sit_seconds++;
            display_dirty = 1;

            if(sit_seconds >= sit_limit_seconds)
            {
                state = STATE_MUST_REST;
                rest_seconds = (unsigned int)rest_minutes * 60;
                rest_ms = 0;
                short_beep_ms = 0;
                beep_times = 0;
                beep_done = 0;
                display_dirty = 1;
            }
            else
            {
                remaining = sit_limit_seconds - sit_seconds;
                Start_Second_Beeps(remaining);
            }
        }
    }
    else if((state == STATE_BREAK || state == STATE_MUST_REST) && !sit_pressed)
    {
        /* Count down rest only while standing; sitting pauses it. */
        rest_ms++;
        if(rest_ms >= 1000)
        {
            rest_ms = 0;
            if(rest_seconds > 0) rest_seconds--;
            display_dirty = 1;
            if(rest_seconds == 0) Finish_Rest();
        }
    }

    colon_ms++;
    if(colon_ms >= 500)
    {
        colon_ms = 0;
        colon_on = !colon_on;
    }

    BEEP = ((state == STATE_MUST_REST && sit_pressed) ||
            ready_beep_ms != 0 || short_beep_ms != 0);

    if(display_dirty) Update_Display();
    Display_Scan();
}

void main(void)
{
    BEEP = 0;
    P1 |= 0x03;                   /* Release P1.0 and P1.1 for input */
    P0 = 0xFF;
    Digits_Off();
    Update_Display();
    Timer0_Init();

    while(1)
    {
        /* Timer0 handles inputs, states, timing, display, and buzzer. */
    }
}
