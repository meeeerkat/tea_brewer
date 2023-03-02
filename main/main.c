#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "shift_register.h"
#include "shift_stepper_motor_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/gpio.h"


#define SER_GPIO 16
#define SCLK_GPIO 17
#define SRCLK_GPIO 18
#define COFFEE_MAKER_RELAY_GPIO 23
#define START_BUTTON_GPIO 25
#define RESET_BUTTON_GPIO 26


#define STRAINER_MOTOR_STEPS 800



// Coffee maker & cup parameters
const uint64_t WARMUP_DURATION = 40 * 1000;                         // 40 s
const uint64_t POURING_DURATION_PER_ML = (6 * 60 * 1000) / 400;     // 6 min for 400ml
const uint8_t BREWING_SINCE_PERCENT = 75;

const uint64_t VOLUME = 400;                // ml
const uint64_t BREW_DURATION = 5*60*1000;   // ms






enum Event {StartEvent, ResetEvent, PouredEvent, InfusedEvent, ResetedEvent};
enum State {Waiting, Pouring, Infusing, Reseting};




static QueueHandle_t events_queue = NULL;

static void IRAM_ATTR event_isr_handler(void* arg) {
  enum Event event = (uint32_t) arg;
  xQueueSendFromISR(events_queue, &event, NULL);
}


void update_state(const enum Event event);

// State functions
shift_register_t sr;
shift_stepper_motor_controller_t ssmc;
TimerHandle_t pouring_timer, infusing_timer;

void pour() {
  // Powering on coffee machine
  gpio_set_level(COFFEE_MAKER_RELAY_GPIO, 1);

  // Moving the tea strainer over the cup
  shift_stepper_motor_controller__moveto(&ssmc, 0, 0);

  xTimerStart(pouring_timer, portMAX_DELAY);
}

void infuse() {
  // Powering down coffee machine
  gpio_set_level(COFFEE_MAKER_RELAY_GPIO, 0);
  
  xTimerStart(infusing_timer, portMAX_DELAY);
}


void reset() {
  // Powering down coffee machine
  gpio_set_level(COFFEE_MAKER_RELAY_GPIO, 0);

  // Reseting strainer position
  QueueHandle_t* queue = shift_stepper_motor_controller_finished_movement_queue(&ssmc);
  uint8_t id;
  while(xQueueReceive(*queue, &id, 0) == pdTRUE);
  shift_stepper_motor_controller__moveto(&ssmc, 0, -STRAINER_MOTOR_STEPS);

  update_state(ResetedEvent);
}



// State machine
enum State state = Waiting;

void update_state(const enum Event event) {

  if (event == ResetedEvent) {
    state = Waiting;
    return;
  }

  // For all states we can start a reset (even waiting)
  if (event == ResetEvent) {
    state = Reseting;
    reset();
    return;
  }

  if (state == Waiting && event == StartEvent) {
    state = Pouring;
    pour();
    return;
  }

  if (state == Pouring && event == PouredEvent) {
    state = Infusing;
    infuse();
    return;
  }

  // Basically a reset...
  if (state == Infusing && event == InfusedEvent) {
    state = Reseting;
    reset();
    return;
  }
}

void update_state_timer_callback(TimerHandle_t timer) {
  const enum Event event = (const enum Event) pvTimerGetTimerID(timer);
  update_state(event);
}







void app_main(void) {
  shift_register__init(&sr, SER_GPIO, SCLK_GPIO, SRCLK_GPIO, 8);
  shift_stepper_motor_controller__init(&ssmc, 1, &sr, 10, 5);

  gpio_set_direction(COFFEE_MAKER_RELAY_GPIO, GPIO_MODE_OUTPUT);

  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_POSEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  //bit mask of the pins that you want to set,e.g.GPIO18/19
  io_conf.pin_bit_mask = (1ULL << START_BUTTON_GPIO) | (1ULL << RESET_BUTTON_GPIO);
  //io_conf.pull_down_en = 0;
  //io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  //install gpio isr service
  gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
  //hook isr handler for specific gpio pin
  gpio_isr_handler_add(START_BUTTON_GPIO, event_isr_handler, (void*) StartEvent);
  //hook isr handler for specific gpio pin
  gpio_isr_handler_add(RESET_BUTTON_GPIO, event_isr_handler, (void*) ResetEvent);

  //create a queue to handle gpio event from isr
  events_queue = xQueueCreate(10, sizeof(uint8_t));


  // Create timers
  const uint64_t pouring_duration = POURING_DURATION_PER_ML * VOLUME;
  
  pouring_timer = xTimerCreate(
      "pouring_timer",
      pdMS_TO_TICKS(pouring_duration + WARMUP_DURATION),
      pdFALSE,
      (void*) PouredEvent,
      update_state_timer_callback);

  int64_t actual_brewing_wait_duration = BREW_DURATION - (pouring_duration*(100-BREWING_SINCE_PERCENT))/100;
  if (actual_brewing_wait_duration < 0)
    actual_brewing_wait_duration = 0;

  infusing_timer = xTimerCreate(
      "infusing_timer",
      pdMS_TO_TICKS(actual_brewing_wait_duration),
      pdFALSE,
      (void*) InfusedEvent,
      update_state_timer_callback);



  enum Event event;
  for(;;)
    if(xQueueReceive(events_queue, &event, portMAX_DELAY))
      update_state(event);



  // NEEDED TO KEEP sr & ssmc variables alive
  // (can always declare them globally instead)
  //while(1) vTaskDelay(pdMS_TO_TICKS(100000));
}
