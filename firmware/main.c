#include <atmel_start.h>
#include "atmel_start_pins.h"
#include "hal_delay.h"

#include "version.h"
#include "dcf.h"
#include "millis.h"
#include "log.h"
#include "ldr.h"
#include "peripherals.h"

// max time of a sync interval (5 minutes plus 10 seconds margin for early start)
#define MAX_SYNC_MILLIS (5 * 60 * 1000 + 10000)

// milliseconds we can go until the RTC is considered stale and due for sync (12 hours)
#define STALE_TIMEOUT_MS (12 * 3600 * 1000)

volatile uint64_t last_dcf_sync = 0;
volatile bool do_sync = false;

bool time_is_stale() {
    // stale unless we managed to sync successfully in the past 12 hours
    return !last_dcf_sync || millis() - last_dcf_sync > STALE_TIMEOUT_MS;
}

static void init_sync(struct calendar_descriptor *const descr) {
    if (time_is_stale()) {
        do_sync = true;
    }
}

void button(void) {
    bool val = gpio_get_pin_level(SWITCH);
    ulog(INFO, "Switch %s", val ? "released" : "pressed")
    if (val) {
        last_dcf_sync = 0;
        do_sync = true;
    }
}

int main(void) {
    struct io_descriptor *uart_io;
    static struct calendar_alarm alarm_2am = {
            .cal_alarm.mode = REPEAT,
            .cal_alarm.option = CALENDAR_ALARM_MATCH_HOUR,
            // start a few seconds early, so we don't have to wait for the start of the minute
            .cal_alarm.datetime.time.hour = 1,
            .cal_alarm.datetime.time.min = 59,
            .cal_alarm.datetime.time.sec = 55,
    };
    static struct calendar_alarm alarm_3am = {
            .cal_alarm.mode = REPEAT,
            .cal_alarm.option = CALENDAR_ALARM_MATCH_HOUR,
            .cal_alarm.datetime.time.hour = 2,
            .cal_alarm.datetime.time.min = 59,
            .cal_alarm.datetime.time.sec = 55,
    };
    static struct calendar_alarm alarm_4am = {
            .cal_alarm.mode = REPEAT,
            .cal_alarm.option = CALENDAR_ALARM_MATCH_HOUR,
            .cal_alarm.datetime.time.hour = 3,
            .cal_alarm.datetime.time.min = 59,
            .cal_alarm.datetime.time.sec = 55,
    };

    atmel_start_init();
    gpio_set_pin_level(LED, false);
    gpio_set_pin_level(PERIPHERAL_CTL, true);   // turn off power to displays and DHT20 module
    gpio_set_pin_level(DCF_CTL, true);          // turn off power to DCF77 module
    gpio_set_pin_level(LDR_SINK, false);        // enable LDR

    if (timer_start(&TIMER_0)) ulog(ERROR, "Failed to start TIMER_0!")
    if (timer_start(&TIMER_1)) ulog(ERROR, "Failed to start TIMER_1!")

    usart_sync_get_io_descriptor(&USART_0, &uart_io);
    usart_sync_enable(&USART_0);

    millis_init();
    calendar_enable(&CALENDAR_0);
    ldr_init();
    ext_irq_register(PIN_PA15, button);

    printf("\r\n\r\n");
    ulog(INFO, "Radio clock firmware build: %s", VERSION_STR)
    ulog(INFO, "https://github.com/erikvanzijst/radioclock")
    ulog(INFO, "Erik van Zijst <erik.van.zijst@gmail.com>\r\n")

    ulog(INFO, "Time not set; starting sync...")

    dcf_sync(0x7FFFFFFF);

    power_up_peripherals();

    calendar_set_alarm(&CALENDAR_0, &alarm_2am, init_sync);
    calendar_set_alarm(&CALENDAR_0, &alarm_3am, init_sync);
    calendar_set_alarm(&CALENDAR_0, &alarm_4am, init_sync);

    for (;;) {
        if (do_sync) {
            ulog(INFO, "Starting time sync...")
            power_down_peripherals();

            struct calendar_date_time dt;
            switch (dcf_sync(MAX_SYNC_MILLIS)) {
                case SUCCESSFUL:
                    last_dcf_sync = millis();
                    calendar_get_date_time(&CALENDAR_0, &dt);
                    ulog(INFO, "Time sync: %04d-%02d-%02d %02d:%02d:00", dt.date.year, dt.date.month, dt.date.day, dt.time.hour, dt.time.min)
                    break;
                case TIMEOUT:
                    ulog(WARN, "Time sync failed (tried for %u sec)", MAX_SYNC_MILLIS / 1000)
                    break;
            }

            power_up_peripherals();
            do_sync = false;
        }
    }
}
