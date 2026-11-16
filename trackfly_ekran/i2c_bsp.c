#include "i2c_bsp.h"
#include "freertos/FreeRTOS.h"
#include "user_config.h"
#include <stdio.h>

static i2c_master_bus_handle_t user_i2c_port0_handle = NULL;
static i2c_master_bus_handle_t user_i2c_port1_handle = NULL;
i2c_master_dev_handle_t disp_touch_dev_handle = NULL;
i2c_master_dev_handle_t rtc_dev_handle = NULL;
i2c_master_dev_handle_t imu_dev_handle = NULL;
i2c_master_dev_handle_t ext_io_dev_handle = NULL;

static uint32_t i2c_data_pdMS_TICKS = 0;
static uint32_t i2c_done_pdMS_TICKS = 0;

// Helper to add external devices to Port 0
esp_err_t i2c_master_add_device_port0(i2c_device_config_t *dev_cfg,
                                      i2c_master_dev_handle_t *dev_handle) {
  return i2c_master_bus_add_device(user_i2c_port0_handle, dev_cfg, dev_handle);
}

void i2c_master_Init(void) {
  i2c_data_pdMS_TICKS = pdMS_TO_TICKS(5000);
  i2c_done_pdMS_TICKS = pdMS_TO_TICKS(1000);
  /*i2c_port 0 init*/
  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = ESP_SCL_NUM,
      .sda_io_num = ESP_SDA_NUM,
      .glitch_ignore_cnt = 7,
      .flags =
          {
              .enable_internal_pullup = true,
          },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port0_handle));
  i2c_bus_config.scl_io_num = Touch_SCL_NUM;
  i2c_bus_config.sda_io_num = Touch_SDA_NUM;
  i2c_bus_config.i2c_port = I2C_NUM_1;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port1_handle));

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .scl_speed_hz = 300000,
  };
  dev_cfg.device_address = EXAMPLE_RTC_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg,
                                            &rtc_dev_handle));

  dev_cfg.device_address = EXAMPLE_IMU_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg,
                                            &imu_dev_handle));

  dev_cfg.device_address = I2C_TOUCH_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port1_handle, &dev_cfg,
                                            &disp_touch_dev_handle));

  // Init External IO Expander (TCA9554) at 0x20
  dev_cfg.device_address = 0x20;
  // IMPORTANT: Re-use port0 handle because IO Expander is on same bus as
  // RTC/IMU
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg,
                                            &ext_io_dev_handle));
}

// IO Expander Handle
// IO Expander Handle Moved to top

void i2c_set_power_pin(bool on) {
  if (!ext_io_dev_handle)
    return;

  // 1. Configure Pin 6 as Output (Reg 0x03)
  // We need to read existing config first to not mess up other pins?
  // For now, let's assume we can just overwrite or read-modify-write if we had
  // read func. Simpler: Just set it. Reg 0x03 (Config): 1=Input, 0=Output.
  // Default is 0xFF (All Input). We want Pin 6 (0x40) to be 0.
  // So 0xFF & ~0x40 = 0xBF.
  // Note: This resets other pins to Input. If they are used, this is bad.
  // But this is setup.
  // SAFEST: Read 0x03, modify, write back.

  uint8_t cfg = 0xFF;
  i2c_read_buff(ext_io_dev_handle, 0x03, &cfg, 1);
  cfg &= ~(1 << 6); // Set Bit 6 to 0 (Output)
  i2c_write_buff(ext_io_dev_handle, 0x03, &cfg, 1);

  // 2. Set Output Value (Reg 0x01)
  uint8_t val = 0;
  i2c_read_buff(ext_io_dev_handle, 0x01, &val, 1);
  if (on) {
    val |= (1 << 6); // Set Bit 6 High
  } else {
    val &= ~(1 << 6); // Set Bit 6 Low
  }
  i2c_write_buff(ext_io_dev_handle, 0x01, &val, 1);
}

uint8_t i2c_write_buff(i2c_master_dev_handle_t dev_handle, int reg,
                       uint8_t *buf, uint8_t len) {
  uint8_t ret;
  uint8_t *pbuf = NULL;
  ret =
      i2c_master_bus_wait_all_done(user_i2c_port0_handle, i2c_done_pdMS_TICKS);
  if (ret != ESP_OK)
    return ret;
  if (reg == -1) {
    ret = i2c_master_transmit(dev_handle, buf, len, i2c_data_pdMS_TICKS);
  } else {
    pbuf = (uint8_t *)malloc(len + 1);
    pbuf[0] = reg;
    for (uint8_t i = 0; i < len; i++) {
      pbuf[i + 1] = buf[i];
    }
    ret = i2c_master_transmit(dev_handle, pbuf, len + 1, i2c_data_pdMS_TICKS);
    free(pbuf);
    pbuf = NULL;
  }
  return ret;
}
uint8_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,
                                  uint8_t *writeBuf, uint8_t writeLen,
                                  uint8_t *readBuf, uint8_t readLen) {
  uint8_t ret;
  ret =
      i2c_master_bus_wait_all_done(user_i2c_port0_handle, i2c_done_pdMS_TICKS);
  if (ret != ESP_OK)
    return ret;
  ret = i2c_master_transmit_receive(dev_handle, writeBuf, writeLen, readBuf,
                                    readLen, i2c_data_pdMS_TICKS);
  return ret;
}
uint8_t i2c_read_buff(i2c_master_dev_handle_t dev_handle, int reg, uint8_t *buf,
                      uint8_t len) {
  uint8_t ret;
  uint8_t addr = 0;
  ret =
      i2c_master_bus_wait_all_done(user_i2c_port0_handle, i2c_done_pdMS_TICKS);
  if (ret != ESP_OK)
    return ret;
  if (reg == -1) {
    ret = i2c_master_receive(dev_handle, buf, len, i2c_data_pdMS_TICKS);
  } else {
    addr = (uint8_t)reg;
    ret = i2c_master_transmit_receive(dev_handle, &addr, 1, buf, len,
                                      i2c_data_pdMS_TICKS);
  }
  return ret;
}

uint8_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,
                                    uint8_t *writeBuf, uint8_t writeLen,
                                    uint8_t *readBuf, uint8_t readLen) {
  uint8_t ret;
  ret =
      i2c_master_bus_wait_all_done(user_i2c_port1_handle, i2c_done_pdMS_TICKS);
  if (ret != ESP_OK)
    return ret;
  ret = i2c_master_transmit_receive(dev_handle, writeBuf, writeLen, readBuf,
                                    readLen, i2c_data_pdMS_TICKS);
  return ret;
}
