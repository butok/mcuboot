/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2019 JUUL Labs
 * Copyright (c) 2020 Arm Limited
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>

#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

/*
 * Initialize a TLV iterator.
 *
 * @param it An iterator struct
 * @param hdr image_header of the slot's image
 * @param fap flash_area of the slot which is storing the image
 * @param type Type of TLV to look for
 * @param prot true if TLV has to be stored in the protected area, false otherwise
 *
 * @returns 0 if the TLV iterator was successfully started
 *          -1 on errors
 */
int
bootutil_tlv_iter_begin(struct image_tlv_iter *it, const struct image_header *hdr,
                        const struct flash_area *fap, uint16_t type, bool prot)
{
    uint32_t off_;
    struct image_tlv_info info;

    BOOT_LOG_DBG("bootutil_tlv_iter_begin: type %d, prot == %d", type, (int)prot);

    if (it == NULL || hdr == NULL || fap == NULL) {
        return -1;
    }

#if defined(MCUBOOT_SWAP_USING_OFFSET)
    off_ = BOOT_TLV_OFF(hdr) + it->start_off;
#else
    off_ = BOOT_TLV_OFF(hdr);
#endif

    if (LOAD_IMAGE_DATA(hdr, fap, off_, &info, sizeof(info))) {
        return -1;
    }

    if (info.it_magic == IMAGE_TLV_PROT_INFO_MAGIC) {
        if (hdr->ih_protect_tlv_size != info.it_tlv_tot) {
            return -1;
        }

        if (LOAD_IMAGE_DATA(hdr, fap, off_ + info.it_tlv_tot,
                            &info, sizeof(info))) {
            return -1;
        }
    } else if (hdr->ih_protect_tlv_size != 0) {
        return -1;
    }

    if (info.it_magic != IMAGE_TLV_INFO_MAGIC) {
        return -1;
    }

    it->hdr = hdr;
    it->fap = fap;
    it->type = type;
    it->prot = prot;
    it->prot_end = off_ + it->hdr->ih_protect_tlv_size;
    it->tlv_end = off_ + it->hdr->ih_protect_tlv_size + info.it_tlv_tot;
    // position on first TLV
    it->tlv_off = off_ + sizeof(info);
    return 0;
}

/*
 * Find next TLV
 *
 * @param it The image TLV iterator struct
 * @param off The offset of the TLV's payload in flash
 * @param len The length of the TLV's payload
 * @param type If not NULL returns the type of TLV found
 *
 * @returns 0 if a TLV with with matching type was found
 *          1 if no more TLVs with matching type are available
 *          -1 on errors
 */
int
bootutil_tlv_iter_next(struct image_tlv_iter *it, uint32_t *off, uint16_t *len,
                       uint16_t *type)
{
    struct image_tlv tlv;
    int rc;

    if (it == NULL || it->hdr == NULL || it->fap == NULL) {
        return -1;
    }

    BOOT_LOG_DBG("bootutil_tlv_iter_next: searching for %d (%d is any) starting at %d ending at %d",
                 it->type, IMAGE_TLV_ANY, it->tlv_off, it->tlv_end);

    while (it->tlv_off < it->tlv_end) {
        if (it->hdr->ih_protect_tlv_size > 0 && it->tlv_off == it->prot_end) {
            it->tlv_off += sizeof(struct image_tlv_info);
        }

        rc = LOAD_IMAGE_DATA(it->hdr, it->fap, it->tlv_off, &tlv, sizeof tlv);
        if (rc) {
            BOOT_LOG_DBG("bootutil_tlv_iter_next: load failed with %d for %p %d",
                         rc, it->fap, it->tlv_off);
            return -1;
        }

        /* No more TLVs in the protected area */
        if (it->prot && it->tlv_off >= it->prot_end) {
            BOOT_LOG_DBG("bootutil_tlv_iter_next: protected TLV %d not found", it->type);
            return 1;
        }

        if (it->type == IMAGE_TLV_ANY || tlv.it_type == it->type) {
            if (type != NULL) {
                *type = tlv.it_type;
            }
            *off = it->tlv_off + sizeof(tlv);
            *len = tlv.it_len;
            it->tlv_off += sizeof(tlv) + tlv.it_len;
            BOOT_LOG_DBG("bootutil_tlv_iter_next: TLV %d found at %d (size %d)",
                         *type, *off, *len);
            return 0;
        }

        it->tlv_off += sizeof(tlv) + tlv.it_len;
    }

    BOOT_LOG_DBG("bootutil_tlv_iter_next: TLV %d not found", it->type);
    return 1;
}

/*
 * Return if a TLV entry is in the protected area.
 *
 * @param it The image TLV iterator struct
 * @param off The offset of the entry to check.
 *
 * @return 0 if this TLV iterator entry is not protected.
 *         1 if this TLV iterator entry is in the protected region
 *         -1 if the iterator is invalid.
 */
int
bootutil_tlv_iter_is_prot(struct image_tlv_iter *it, uint32_t off)
{
    if (it == NULL || it->hdr == NULL || it->fap == NULL) {
        return -1;
    }

    return off < it->prot_end;
}
