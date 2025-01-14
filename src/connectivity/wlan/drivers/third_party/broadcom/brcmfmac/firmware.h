/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FIRMWARE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FIRMWARE_H_

#include "common.h"
#include "device.h"
#include "linuxisms.h"

// clang-format off

#define BRCMF_FW_REQUEST         0x000F
#define BRCMF_FW_REQUEST_NVRAM   0x0001
#define BRCMF_FW_REQ_FLAGS       0x00F0
#define BRCMF_FW_REQ_NV_OPTIONAL 0x0010

// clang-format on

#define BRCMF_FW_DEFAULT_PATH ""

struct brcmf_firmware {
  size_t size;
  void* data;
};

/**
 * struct brcmf_firmware_mapping - Used to map chipid/revmask to firmware
 *  filename and nvram filename. Each bus type implementation should create
 *  a table of firmware mappings (using the macros defined below).
 *
 * @chipid: ID of chip.
 * @revmask: bitmask of revisions, e.g. 0x10 means rev 4 only, 0xf means rev 0-3
 * @fw: name of the firmware file.
 * @nvram: name of nvram file.
 */
struct brcmf_firmware_mapping {
  uint32_t chipid;
  uint32_t revmask;
  const char* fw;
  const char* nvram;
};

#define BRCMF_FW_NVRAM_DEF(fw_nvram_name, fw, nvram)                                   \
  static const char BRCM_##fw_nvram_name##_FIRMWARE_NAME[] = BRCMF_FW_DEFAULT_PATH fw; \
  static const char BRCM_##fw_nvram_name##_NVRAM_NAME[] = BRCMF_FW_DEFAULT_PATH nvram; \
  MODULE_FIRMWARE(BRCMF_FW_DEFAULT_PATH fw)

#define BRCMF_FW_DEF(fw_name, fw)                                                \
  static const char BRCM_##fw_name##_FIRMWARE_NAME[] = BRCMF_FW_DEFAULT_PATH fw; \
  MODULE_FIRMWARE(BRCMF_FW_DEFAULT_PATH fw)

#define BRCMF_FW_NVRAM_ENTRY(chipid, mask, name) \
  { chipid, mask, BRCM_##name##_FIRMWARE_NAME, BRCM_##name##_NVRAM_NAME }

#define BRCMF_FW_ENTRY(chipid, mask, name) \
  { chipid, mask, BRCM_##name##_FIRMWARE_NAME, NULL }

zx_status_t brcmf_fw_map_chip_to_name(uint32_t chip, uint32_t chiprev,
                                      struct brcmf_firmware_mapping mapping_table[],
                                      uint32_t table_size, char fw_name[BRCMF_FW_NAME_LEN],
                                      char nvram_name[BRCMF_FW_NAME_LEN]);
void brcmf_fw_nvram_free(void* nvram);
/*
 * Request firmware(s) asynchronously. When the asynchronous request
 * fails it will not use the callback, but call device_release_driver()
 * instead which will call the driver .remove() callback.
 */
zx_status_t brcmf_fw_get_firmwares_pcie(struct brcmf_device* dev, uint16_t flags, const char* code,
                                        const char* nvram,
                                        void (*fw_cb)(struct brcmf_device* dev, zx_status_t err,
                                                      const struct brcmf_firmware* fw,
                                                      void* nvram_image, uint32_t nvram_len),
                                        uint16_t domain_nr, uint16_t bus_nr);
zx_status_t brcmf_fw_get_firmwares(struct brcmf_device* dev, uint16_t flags, const char* code,
                                   const char* nvram,
                                   void (*fw_cb)(struct brcmf_device* dev, zx_status_t err,
                                                 const struct brcmf_firmware* fw, void* nvram_image,
                                                 uint32_t nvram_len));

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FIRMWARE_H_
