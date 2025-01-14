/*
 * Copyright (c) 2014 Broadcom Corporation
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

#include "of.h"

#include "common.h"
#include "core.h"
#include "debug.h"
#include "defs.h"
#include "device.h"
#include "linuxisms.h"

void brcmf_of_probe(struct brcmf_device* dev, enum brcmf_bus_type bus_type,
                    struct brcmf_mp_device* settings) {
  struct brcmfmac_sdio_pd* sdio = &settings->bus.sdio;
  struct device_node* np = static_cast<decltype(np)>(dev->of_node);
  int irq;
  uint32_t irqf;
  uint32_t val;

  if (!np || bus_type != BRCMF_BUS_TYPE_SDIO || !of_device_is_compatible(np, "brcm,bcm4329-fmac")) {
    return;
  }

  if (of_property_read_u32(np, "brcm,drive-strength", &val) == 0) {
    sdio->drive_strength = val;
  }

  /* make sure there are interrupts defined in the node */
  if (!of_find_property(np, "interrupts", NULL)) {
    return;
  }

  irq = irq_of_parse_and_map(np, 0);
  if (!irq) {
    BRCMF_ERR("interrupt could not be mapped\n");
    return;
  }
  irqf = irqd_get_trigger_type(irq_get_irq_data(irq));

  sdio->oob_irq_supported = true;
}
