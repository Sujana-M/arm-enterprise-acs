/** @file
 * Copyright (c) 2018, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
**/

#include <val_interface.h>
#include <val_sdei_interface.h>

#define TEST_DESC "Verify check always availability-Event Status  "

static int32_t g_wd_num;
static uint64_t g_test_status= SDEI_TEST_PASS;
static uint64_t *g_wd_addr = NULL;
static volatile int32_t g_handler_flag = 1;
static struct sdei_event g_event;

/* Check the SDEI_EVENT_STATUS API in below states
 *          1. Handler-Unregistered State
 *          2. Handler-registered State
 *          3. Handler-Enabled State
 *          4. Handler-Enabled & Running State
 *          5. Handler-Unregister Pending State
 */
static void event_handler(void)
{
    uint64_t result;
    int32_t err;

    val_wd_set_ws0(g_wd_addr, g_wd_num, 0);

    /* Check 4 - Handler-Enabled & Running State */
    err = val_sdei_event_status(g_event.event_num, &result);
    if (err) {
        val_print(ACS_LOG_ERR, "\n        Handler-Enabled & Running check failed with err %d", err);
        g_test_status = SDEI_TEST_FAIL;
    } else {
        if (!(result & EVENT_STATUS_RUNNING_BIT)) {
            val_print(ACS_LOG_ERR, "\n        SDEI_EVENT_STATUS running bit mismatch,"
                                   "found value = %llx", (result & EVENT_STATUS_RUNNING_BIT));
            g_test_status = SDEI_TEST_FAIL;
        }
    }

    err = val_sdei_event_unregister(g_event.event_num);
    if (err != SDEI_STATUS_PENDING) {
        val_print(ACS_LOG_ERR, "\n        Unregister-Pending check failed");
        g_test_status = SDEI_TEST_FAIL;
    }

    /* Check 5 - Handler-Unregister Pending State */
    err = val_sdei_event_status(g_event.event_num, &result);
    if (err) {
        val_print(ACS_LOG_ERR, "\n        Handler-Enabled & Running check failed with err %d", err);
        g_test_status = SDEI_TEST_FAIL;
    } else {
        if (!(result & EVENT_STATUS_RUNNING_BIT)) {
            val_print(ACS_LOG_ERR, "\n        SDEI_EVENT_STATUS running bit mismatch,"
                                   "found value = %llx", (result & EVENT_STATUS_RUNNING_BIT));
            g_test_status = SDEI_TEST_FAIL;
        }
    }

    g_handler_flag = 0;
}

static void test_entry(void) {
    uint32_t ns_wdg = 0;
    uint64_t timer_expire_ticks = 1, timeout;
    uint64_t wd_ctrl_base;
    int32_t err;
    uint64_t result;
    uint64_t int_id = 0;

    g_handler_flag = 1;
    g_wd_num = val_wd_get_info(0, WD_INFO_COUNT);
    g_event.is_bound_irq = TRUE;

    do {
        /*Array index starts from 0, so subtract 1 from count*/
        g_wd_num--;

        /* Skip secure Watchdog */
        if (val_wd_get_info(g_wd_num, WD_INFO_ISSECURE))
            continue;

        ns_wdg++;
        timeout = WD_TIME_OUT;

        /* Read Watchdog interrupt from Watchdog info table */
        int_id = val_wd_get_info(g_wd_num, WD_INFO_GSIV);
        val_print(ACS_LOG_DEBUG, "\n        WS0 interrupt id: %lld", int_id);
        /* Read Watchdog base address from Watchdog info table */
        wd_ctrl_base = val_wd_get_info(g_wd_num, WD_INFO_CTRL_BASE);
        g_wd_addr = val_pa_to_va(wd_ctrl_base);

        err = val_gic_disable_interrupt(int_id);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        Interrupt %lld disable failed", int_id);
            g_test_status = SDEI_TEST_FAIL;
            goto unmap_va;
        }
        /* Bind Watchdog interrupt to event */
        err = val_sdei_interrupt_bind(int_id, &g_event.event_num);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        SPI intr number %lld bind failed with err %d",
                                                                                    int_id, err);
            g_test_status = SDEI_TEST_FAIL;
            goto unmap_va;
        }
        val_pe_data_cache_clean_invalidate((uint64_t)&g_event.event_num);

        /* Check 1 - Handler-Unregistered State */
        err = val_sdei_event_status(g_event.event_num, &result);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        Handler-Unregistered state check failed with err %d",
                                                                                              err);
            g_test_status = SDEI_TEST_FAIL;
            goto unmap_va;
        } else {
            if (result & EVENT_STATUS_REGISTER_BIT) {
                val_print(ACS_LOG_ERR, "\n        SDEI_EVENT_STATUS register bit mismatch,"
                                       "found = %llx", (result & EVENT_STATUS_REGISTER_BIT));
                g_test_status = SDEI_TEST_FAIL;
                goto unmap_va;
            }
        }

        err = val_sdei_event_register(g_event.event_num, (uint64_t)asm_event_handler,
                                      (void *)event_handler, 0, 0);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        SDEI evt %d register fail with err %x",
                                    g_event.event_num, err);
            g_test_status = SDEI_TEST_FAIL;
            goto interrupt_release;
        }

        /* Check 2 - Handler-Registered State */
        err = val_sdei_event_status(g_event.event_num, &result);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        Handler-Registered state check failed with err %d",
                                                                                            err);
            g_test_status = SDEI_TEST_FAIL;
            goto event_unregister;
        } else {
            if (!(result & EVENT_STATUS_REGISTER_BIT)) {
                val_print(ACS_LOG_ERR, "\n        SDEI_EVENT_STATUS register bit mismatch,"
                                       "found = %llx", (result & EVENT_STATUS_REGISTER_BIT));
                g_test_status = SDEI_TEST_FAIL;
                goto event_unregister;
            }
        }

        err = val_sdei_event_enable(g_event.event_num);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        SDEI event enable failed with err %d", err);
            g_test_status = SDEI_TEST_FAIL;
            goto event_unregister;
        }
        /* Check 3 - Handler-Enabled State */
        err = val_sdei_event_status(g_event.event_num, &result);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        Handler-Enabled state check failed with err %d", err);
            g_test_status = SDEI_TEST_FAIL;
            goto event_unregister;
        } else {
            if (!(result & EVENT_STATUS_ENABLE_BIT)) {
                val_print(ACS_LOG_ERR, "\n        SDEI_EVENT_STATUS enable bit mismatch,"
                                       "found value = %llx\n", (result & EVENT_STATUS_ENABLE_BIT));
                g_test_status = SDEI_TEST_FAIL;
                goto event_unregister;
            }
        }

        /* Generate Watchdog interrupt */
        val_wd_set_ws0(g_wd_addr, g_wd_num, timer_expire_ticks);

        while (timeout--) {
            val_pe_data_cache_invalidate((uint64_t)&g_handler_flag);
            if (g_handler_flag == 0)
                break;
        }
        if (g_handler_flag) {
            val_print(ACS_LOG_ERR, "\n        Watchdog interrupt trigger failed");
            val_wd_set_ws0(g_wd_addr, g_wd_num, 0);
            g_test_status = SDEI_TEST_FAIL;
            goto event_unregister;
        }
    } while (0);

    if (!ns_wdg) {
        g_test_status = SDEI_TEST_FAIL;
        val_print(ACS_LOG_ERR, "\n        No non-secure Watchdogs reported");
        return;
    }

    timeout = TIMEOUT_MEDIUM;
    do {
        err = val_sdei_event_status(g_event.event_num, &result);
        if (err)
            val_print(ACS_LOG_ERR, "\n        SDEI event %d status failed err %x",
                                                                     g_event.event_num, err);
        if (!(result & EVENT_STATUS_RUNNING_BIT))
            break;
    } while (timeout--);

    if ((result & EVENT_STATUS_REGISTER_BIT)) {
        g_test_status = SDEI_TEST_ERROR;
        goto event_unregister;
    }
    else {
        err = val_sdei_interrupt_release(g_event.event_num);
        if (err) {
            val_print(ACS_LOG_ERR, "\n        Event num %d release failed :err %d",
                                                                    g_event.event_num, err);
            g_test_status = SDEI_TEST_ERROR;
        }
        goto unmap_va;
    }

event_unregister:
    err = val_sdei_event_unregister(g_event.event_num);
    if (err) {
        val_print(ACS_LOG_ERR, "\n        SDEI event %d unregister failed :err %x",
                                                                        g_event.event_num, err);
    }
interrupt_release:
    err = val_sdei_interrupt_release(g_event.event_num);
    if (err) {
        val_print(ACS_LOG_ERR, "\n        Event number %d release failed with err %x",
                                                                        g_event.event_num, err);
    }
unmap_va:
    val_va_free(g_wd_addr);
    val_test_pe_set_status(val_pe_get_index(),
                           ((g_test_status == SDEI_TEST_PASS) ? SDEI_TEST_PASS : SDEI_TEST_FAIL)
                           );
}

SDEI_SET_TEST_DEPS(test_032_deps, TEST_001_ID, TEST_002_ID);
SDEI_PUBLISH_TEST(test_032, TEST_032_ID, TEST_DESC, test_032_deps, test_entry, FALSE);
