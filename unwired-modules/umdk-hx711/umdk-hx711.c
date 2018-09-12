/*
 * Copyright (C) 2018 Unwired Devices [info@unwds.com]
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file		umdk-hx711.c
 * @brief       umdk-hx711 module implementation
 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_HX711_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "hx711"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "umdk-hx711.h"
#include "periph/gpio.h"
#include "board.h"
#include "unwds-common.h"
#include "thread.h"
#include "rtctimers-millis.h"
#include "xtimer.h"

static uwnds_cb_t *callback;

static kernel_pid_t timer_pid;
static msg_t timer_msg = {};
static rtctimers_millis_t timer;

static bool is_polled = false;


static struct {
	uint8_t publish_period_min;
    uint32_t hx711_cal;
    uint32_t zero;
} hx711_config;

static void reset_config(void) {
	hx711_config.publish_period_min = UMDK_HX711_PUBLISH_PERIOD_MIN;
    hx711_config.hx711_cal = 0;
    hx711_config.zero = 0;
}

static void init_config(void) {
	reset_config();

	if (!unwds_read_nvram_config(_UMDK_MID_, (uint8_t *) &hx711_config, sizeof(hx711_config))) {
		reset_config();
    }
}

static inline void save_config(void) {
	unwds_write_nvram_config(_UMDK_MID_, (uint8_t *) &hx711_config, sizeof(hx711_config));
}

static uint32_t hx711_get_data(void) {
    uint32_t data = 0;
    
    /* wake up */
    gpio_clear(HX711_SCK_PIN);
    
    xtimer_spin(xtimer_ticks_from_usec(1000));
    
    /* wait for sensor to be ready */
    while(gpio_read(HX711_DATA_PIN) != 0) {};
    
    /* wait a bit */
    xtimer_spin(xtimer_ticks_from_usec(1000));
    
    for (int i = 0; i < 24; i++) {
        gpio_set(HX711_SCK_PIN);
        xtimer_spin(xtimer_ticks_from_usec(2));
        gpio_clear(HX711_SCK_PIN);
        data <<= 1;
        if (gpio_read(HX711_DATA_PIN) > 0) {
            data++;
        }
        xtimer_spin(xtimer_ticks_from_usec(2));
    }
    data ^= 0x800000;
    
    /* 25th pulse */
    gpio_set(HX711_SCK_PIN);
    xtimer_spin(xtimer_ticks_from_usec(2));
    gpio_clear(HX711_SCK_PIN);
    
    /* wait a bit */
    xtimer_spin(xtimer_ticks_from_usec(1000));
    
    /* put to sleep */
    gpio_set(HX711_SCK_PIN);
    
    return data;
}

static void prepare_result(module_data_t *data) {
    uint32_t raw = hx711_get_data();
    
    uint32_t weight = raw;
        
    if (weight > hx711_config.zero) {
        weight -= hx711_config.zero;
    } else {
        weight = 0;
    }
    
    if (hx711_config.hx711_cal != 0) {
        weight *= 10;
        weight /= hx711_config.hx711_cal;
    }
    
    printf("[umdk-" _UMDK_NAME_ "] Weight: %lu g\n", weight);

    if (data) {
        data->length = 2 + sizeof(weight) + sizeof(raw);
        data->data[0] = _UMDK_MID_;
        data->data[1] = UMDK_HX711_DATA_DATA;
        memcpy(data->data + 2, &weight, sizeof(weight));
        memcpy(data->data + 2 + sizeof(weight), &raw, sizeof(raw));
    }
}

static volatile uint32_t btn_last_press = 0;

static void btn_connect(void* arg) {
    (void) arg;
    if (rtctimers_millis_now() > btn_last_press + 500) {
        is_polled = false;
        msg_send(&timer_msg, timer_pid);
        
        btn_last_press = rtctimers_millis_now();
    }
}

void set_period(int period) {
    hx711_config.publish_period_min = period;
        
    printf("[umdk-" _UMDK_NAME_ "] Period set to %d minutes\n", hx711_config.publish_period_min);
    if (hx711_config.publish_period_min != 0) {
        rtctimers_millis_set_msg(&timer, 60000 * hx711_config.publish_period_min, &timer_msg, timer_pid);
    } else {
        rtctimers_millis_remove(&timer);
    }
    
    save_config();
}

int umdk_hx711_shell_cmd(int argc, char **argv) {
    if (argc == 1) {
        puts ("[umdk-" _UMDK_NAME_ "] get - get results now");
        puts ("[umdk-" _UMDK_NAME_ "] send - send results now");
        puts ("[umdk-" _UMDK_NAME_ "] zero - set zero");
        puts ("[umdk-" _UMDK_NAME_ "] cal <grams> - calibrate with known weight");
        return 0;
    }
    
    char *cmd = argv[1];
    
    if (strcmp(cmd, "period") == 0) {
        set_period(atoi(argv[2]));
    }
	
    if (strcmp(cmd, "get") == 0) {
        prepare_result(NULL);
    }
    
    if (strcmp(cmd, "send") == 0) {
        is_polled = false;
        /* Send signal to publisher thread */
		msg_send(&timer_msg, timer_pid);
    }
    
    if (strcmp(cmd, "zero") == 0) {
        hx711_config.zero = 0;
        for (int i = 0; i < 10; i++) {
            hx711_config.zero += hx711_get_data();
            rtctimers_millis_sleep(100);
        }
        
        hx711_config.zero /= 10;
        
        if (hx711_config.hx711_cal != 0) {
            uint32_t zero_grams = hx711_config.zero * 10;
            zero_grams /= hx711_config.hx711_cal;
            printf("[umdk-" _UMDK_NAME_ "] Zero set to %lu g\n", zero_grams);
        } else {
            printf("[umdk-" _UMDK_NAME_ "] Zero set to %lu units\n", hx711_config.zero);
        }
        
        save_config();
    }
    
    if (strcmp(cmd, "cal") == 0) {
        uint32_t cal_weight = atoi(argv[2]);
        
        uint32_t weight = 0;
        for (int i = 0; i < 10; i++) {
            weight += (hx711_get_data() - hx711_config.zero);
            rtctimers_millis_sleep(100);
        }
        
        hx711_config.hx711_cal = weight/cal_weight;
        
        char buf[10];
        int_to_float_str(buf, hx711_config.hx711_cal, 1);
        printf("[umdk-" _UMDK_NAME_ "] Calibration done, %s units/g\n", buf);
        
        save_config();
    }
    
    return 1;
}

static void *timer_thread(void *arg) {
    (void) arg;
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);

    puts("[umdk-" _UMDK_NAME_ "] Periodic publisher thread started");

    while (1) {
        msg_receive(&msg);

        module_data_t data = {};
        data.as_ack = is_polled;
        is_polled = false;

        prepare_result(&data);

        /* Notify the application */
        callback(&data);

        /* Restart after delay */
        rtctimers_millis_set_msg(&timer, 60000 * hx711_config.publish_period_min, &timer_msg, timer_pid);
    }
    
    return NULL;
}

void umdk_hx711_init(uint32_t *non_gpio_pin_map, uwnds_cb_t *event_callback)
{
    (void) non_gpio_pin_map;

    callback = event_callback;
    
    init_config();

    gpio_init(HX711_DATA_PIN, GPIO_IN);
    gpio_init(HX711_SCK_PIN, GPIO_OUT);
    
    /* put HX711 to sleep */
    gpio_set(HX711_SCK_PIN);
    
    /* Create handler thread */
	char *stack = (char *) allocate_stack(UMDK_HX711_STACK_SIZE);
	if (!stack) {
		return;
	}
    
	timer_pid = thread_create(stack, UMDK_HX711_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, timer_thread, NULL, "hx711 thread");
    

    /* Start publishing timer */
	rtctimers_millis_set_msg(&timer, 60000 * hx711_config.publish_period_min, &timer_msg, timer_pid);
    
#ifdef UNWD_CONNECT_BTN
    if (UNWD_USE_CONNECT_BTN) {
        gpio_init_int(UNWD_CONNECT_BTN, GPIO_IN_PU, GPIO_FALLING, btn_connect, NULL);
    }
#endif

    puts("[umdk-" _UMDK_NAME_ "] HX711 ADC ready");
    
    unwds_add_shell_command(_UMDK_NAME_, "type '" _UMDK_NAME_ "' for commands list", umdk_hx711_shell_cmd);
}

static void reply_fail(module_data_t *reply) {
	reply->length = 2;
	reply->data[0] = _UMDK_MID_;
	reply->data[1] = UMDK_HX711_DATA_ERROR;
}

static void reply_ok(module_data_t *reply) {
	reply->length = 2;
	reply->data[0] = _UMDK_MID_;
	reply->data[1] = UMDK_HX711_DATA_OK;
}

bool umdk_hx711_cmd(module_data_t *data, module_data_t *reply)
{
	if (data->length < 1)
		return false;

	umdk_hx711_cmd_t c = data->data[0];

	switch (c) {
	case UMDK_HX711_CMD_POLL:
        is_polled = true;
        /* Send signal to publisher thread */
		msg_send(&timer_msg, timer_pid);
		return false; /* Don't reply now */
        
    case UMDK_HX711_CMD_PERIOD: {
		if (data->length != 2) {
			reply_fail(reply);
			break;
		}

		uint8_t period = data->data[1];
		set_period(period);

		reply_ok(reply);
		break;
	}

	default:
        reply_fail(reply);
		break;
	}

    return true;
}

#ifdef __cplusplus
}
#endif
