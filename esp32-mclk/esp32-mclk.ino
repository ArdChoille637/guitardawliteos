// esp32-mclk.ino
//
// Standalone 12.288 MHz I2S MCLK generator for the PCM1808 ADC on the
// GuitarDAWLiteOS Pi 5 build (Spike B). Runs on an Arduino Nano ESP32
// (ESP32-S3), fully decoupled from the Pi 5's own pll_audio/clk_i2s --
// see docs/claim-verification.md ("Update 2026-07-09") for why the Pi 5
// can't generate this itself while its I2S peripheral is running BCLK.
//
// Only MCLK is used. BCLK/WS/DATA stay unconnected -- since the
// 2026-07-10 clock pivot the PCM1808 divides this MCLK into BCLK/LRCLK
// (ADC straps as bus master, Pi 5 runs I2S slave on GPIO18/19/20/21);
// this board's job is to hold GPIO5 (Nano ESP32 pin "D2") at a clean
// 256*48kHz tone.
//
// SELF-TEST / HEALTH READOUT: no scope on hand, so frequency is verified in
// hardware using the chip's own PCNT (pulse counter) peripheral instead of a
// logic analyzer. Link D2 -> D3 (GPIO6) and the serial monitor prints the
// measured frequency once a second.
//
// On the PERMANENT soldered build the D2 -> D3 link STAYS (see
// docs/solder-build.md 5), tapped on the Nano side of the 33 ohm series
// resistor so the counter reads the source while the resistor still damps the
// run to the ADC. With no scope on the bench, this 1 Hz print is the only
// continuous proof that the root of the whole audio clock domain is alive.
// (Earlier docs said to remove the jumper after bring-up -- that was right for
// a temporary bench jumper and is wrong for a soldered assembly.)
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "driver/i2s_std.h"
#include "driver/pulse_cnt.h"

namespace {

constexpr gpio_num_t kMclkPin = GPIO_NUM_5;      // Nano ESP32 silkscreen "D2"
constexpr gpio_num_t kCounterPin = GPIO_NUM_6;   // "D3" -- jumper to D2 for self-test
constexpr uint32_t kSampleRateHz = 48000;        // x256 = 12.288 MHz
constexpr int kPcntHighLimit = 30000;            // overflow point; accum_count folds these
                                                  // together so the 16-bit HW counter can
                                                  // track a 12.288 MHz input

i2s_chan_handle_t g_tx_chan = nullptr;
pcnt_unit_handle_t g_pcnt_unit = nullptr;

void StartMclk() {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &g_tx_chan, nullptr));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = kMclkPin,
              .bclk = I2S_GPIO_UNUSED,
              .ws = I2S_GPIO_UNUSED,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  // NOTE (2026-07-09): ESP32-S3 has no APLL -- soc/clk_tree_defs.h's
  // soc_periph_i2s_clk_src_t only offers PLL_160M/PLL_240M (PLL_D2,
  // shared general-purpose SoC PLL)/XTAL/EXTERNAL. APLL is original-
  // ESP32-only; an earlier version of this file wrongly assumed S3 had
  // one too. I2S_STD_CLK_DEFAULT_CONFIG already selects I2S_CLK_SRC_DEFAULT
  // (PLL_160M) and mclk_multiple=256, so nothing to override here.

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(g_tx_chan));  // MCLK starts now
}

void StartFrequencyCounter() {
  pcnt_unit_config_t unit_cfg = {
      .low_limit = -1,
      .high_limit = kPcntHighLimit,
      .flags = {.accum_count = true},  // pcnt_unit_get_count() returns the
                                        // running total across HW overflows,
                                        // not just the raw 16-bit register.
  };
  ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &g_pcnt_unit));

  pcnt_chan_config_t chan_cfg = {
      .edge_gpio_num = kCounterPin,
      .level_gpio_num = -1,
  };
  pcnt_channel_handle_t chan = nullptr;
  ESP_ERROR_CHECK(pcnt_new_channel(g_pcnt_unit, &chan_cfg, &chan));
  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
      chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
  ESP_ERROR_CHECK(pcnt_channel_set_level_action(
      chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP));

  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(g_pcnt_unit, kPcntHighLimit));
  ESP_ERROR_CHECK(pcnt_unit_enable(g_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(g_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_start(g_pcnt_unit));
}

}  // namespace

void setup() {
  // MCLK first, before anything that blocks. This board is the root of the whole
  // audio clock domain: until SCKI runs, the PCM1808 is powered down and the Pi's
  // I2S bus is dead. Starting the clock after Serial.begin()+delay(2000) left a
  // 2 s silent window on every reset, brownout and USB re-enumeration.
  StartMclk();

  Serial.begin(115200);
  delay(2000);  // give the serial monitor time to attach before we print

  StartFrequencyCounter();

  Serial.println();
  Serial.println("MCLK generator running: target 12.288 MHz (256x48kHz) on GPIO5 / D2.");
  Serial.println("Self-test: jumper D2 -> D3 (GPIO6), then watch the readings below.");
  Serial.println("No jumper = the count stays near 0, which is expected -- that's not a fault.");
}

void loop() {
  delay(1000);  // ~1s gate window: pulse count this window == frequency in Hz

  // Soft-fail, never ESP_ERROR_CHECK here: that panics and reboots the board, which
  // would drop MCLK and mute the whole audio chain because a *diagnostic* counter
  // hiccuped. The counter is expendable; the clock is not.
  int count = 0;
  esp_err_t err = pcnt_unit_get_count(g_pcnt_unit, &count);
  if (err != ESP_OK) {
    Serial.printf("PCNT read failed (%s) -- counter only; MCLK on D2 is unaffected.\n",
                  esp_err_to_name(err));
    return;
  }
  err = pcnt_unit_clear_count(g_pcnt_unit);
  if (err != ESP_OK) {
    Serial.printf("PCNT clear failed (%s) -- counter only; MCLK on D2 is unaffected.\n",
                  esp_err_to_name(err));
    return;
  }

  Serial.printf("Measured MCLK: %.4f MHz (target 12.288000 MHz)\n", count / 1.0e6);
}
