#include "ay8910.h"
#include <string.h>

/* The DAC is logarithmic: each step is about 1.5 dB, near enough to a factor of sqrt(2)
 * every two steps. These are MAME's measured levels for the 8910, scaled to 0..8191. */
static const uint16_t vol_table[16] = {
        0,   40,   59,   87,  128,  184,  269,  388,
      555,  813, 1152, 1671, 2360, 3406, 4870, 6982
};

static void update_periods(ay8910_t *ay);

void ay_init(ay8910_t *ay, uint32_t clock, ay_port_read_fn port_read, void *ctx)
{
    memset(ay, 0, sizeof(*ay));
    ay->clock = clock;
    ay->port_read = port_read;
    ay->port_ctx = ctx;
    ay_reset(ay);
}

void ay_reset(ay8910_t *ay)
{
    memset(ay->reg, 0, sizeof(ay->reg));
    ay->reg[7] = 0xff;                       /* everything disabled */
    memset(ay->tone_count, 0, sizeof(ay->tone_count));
    memset(ay->tone_out, 0, sizeof(ay->tone_out));
    ay->noise_count = 0; ay->noise_out = 0; ay->noise_rng = 1;
    ay->env_count = 0; ay->env_step = 0; ay->env_out = 0; ay->env_hold = 0;
    ay->latch = 0; ay->acc = 0;
    update_periods(ay);
}

void ay_address_w(ay8910_t *ay, uint8_t reg) { ay->latch = reg & 0x0f; }

void ay_data_w(ay8910_t *ay, uint8_t data)
{
    int r = ay->latch;
    ay->reg[r] = data;
    if (r <= 6 || r == 11 || r == 12) update_periods(ay);
    if (r == 13) {                           /* writing the shape restarts the envelope */
        ay->env_step = 0; ay->env_hold = 0; ay->env_count = 0;
    }
}

uint8_t ay_data_r(ay8910_t *ay)
{
    int r = ay->latch;
    if (r == 14) return ay->port_read ? ay->port_read(ay->port_ctx, 0) : 0xff;
    if (r == 15) return ay->port_read ? ay->port_read(ay->port_ctx, 1) : 0xff;
    return ay->reg[r];
}

/* the periods are recomputed on write rather than on every step, which is a hot loop */
static void update_periods(ay8910_t *ay)
{
    for (int ch = 0; ch < 3; ch++) {
        uint16_t p = (uint16_t)(ay->reg[ch * 2] | ((ay->reg[ch * 2 + 1] & 0x0f) << 8));
        ay->period[ch] = p ? p : 1;          /* a period of 0 behaves as 1 */
    }
    uint16_t np = (uint16_t)(ay->reg[6] & 0x1f);
    ay->noise_period = (uint16_t)((np ? np : 1) * 2);
    uint16_t ep = (uint16_t)(ay->reg[11] | (ay->reg[12] << 8));
    ay->env_period = ep ? ep : 1;
}

/* advance the chip by one step of its clock/8 timebase */
static void ay_step(ay8910_t *ay)
{
    for (int ch = 0; ch < 3; ch++) {
        if (++ay->tone_count[ch] >= ay->period[ch]) {
            ay->tone_count[ch] = 0;
            ay->tone_out[ch] ^= 1;
        }
    }
    if (++ay->noise_count >= ay->noise_period) {   /* the noise generator runs at half the tone rate */
        ay->noise_count = 0;
        /* 17-bit LFSR, taps at bits 0 and 3 */
        uint32_t bit = ((ay->noise_rng >> 0) ^ (ay->noise_rng >> 3)) & 1;
        ay->noise_rng = (ay->noise_rng >> 1) | (bit << 16);
        ay->noise_out = (uint8_t)(ay->noise_rng & 1);
    }
    if (++ay->env_count >= ay->env_period) {
        ay->env_count = 0;
        if (!ay->env_hold) {
            ay->env_step++;
            uint8_t shape = ay->reg[13] & 0x0f;
            if (ay->env_step > 31) {
                if (!(shape & 0x08)) {        /* one ramp, then silence */
                    ay->env_step = 31; ay->env_hold = 1; ay->env_out = 0;
                } else if (shape & 0x01) {    /* hold at the end of the ramp */
                    ay->env_step = 31; ay->env_hold = 1;
                    ay->env_out = (shape & 0x02) ? ((shape & 0x04) ? 0 : 15) : ((shape & 0x04) ? 15 : 0);
                } else {
                    ay->env_step = 0;         /* free running */
                }
            }
            if (!ay->env_hold) {
                uint8_t pos = (uint8_t)(ay->env_step & 0x0f);
                int rising = (shape & 0x04) ? 1 : 0;
                if ((shape & 0x08) && (shape & 0x02) && (ay->env_step & 0x10)) rising = !rising;
                if (!(shape & 0x08) && (ay->env_step & 0x10)) rising = rising;   /* single ramp */
                ay->env_out = (uint8_t)(rising ? pos : 15 - pos);
            }
        }
    }
}

void ay_render(ay8910_t *ay, int16_t *buf, int samples, int rate)
{
    /* the tone counters are clocked at clock/8 */
    uint32_t step_per_sample = (uint32_t)(((uint64_t)(ay->clock / 8) << 16) / (uint32_t)rate);
    for (int i = 0; i < samples; i++) {
        ay->acc += step_per_sample;
        uint32_t steps = ay->acc >> 16;
        ay->acc &= 0xffff;
        if (steps > 512) steps = 512;         /* never let a stall turn into a freeze */
        for (uint32_t s = 0; s < steps; s++) ay_step(ay);

        int32_t out = 0;
        for (int ch = 0; ch < 3; ch++) {
            uint8_t tone_dis  = (uint8_t)((ay->reg[7] >> ch) & 1);
            uint8_t noise_dis = (uint8_t)((ay->reg[7] >> (ch + 3)) & 1);
            /* a disabled source is held high, so the channel is the AND of the two */
            uint8_t level = (uint8_t)((tone_dis | ay->tone_out[ch]) & (noise_dis | ay->noise_out));
            if (!level) continue;
            uint8_t v = ay->reg[8 + ch];
            out += vol_table[(v & 0x10) ? ay->env_out : (v & 0x0f)];
        }
        int32_t mixed = buf[i] + out;
        buf[i] = (int16_t)(mixed > 32767 ? 32767 : (mixed < -32768 ? -32768 : mixed));
    }
}
