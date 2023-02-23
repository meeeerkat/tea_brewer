#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "shift_register.h"
#include "shift_stepper_motor_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const uint64_t WARMUP_DURATION = 40 * 1000;                         // 40 s
const uint64_t POURING_DURATION_PER_ML = (6 * 60 * 1000) / 400;     // 6 min for 400ml
const uint8_t BREWING_SINCE_PERCENT = 75;



void brew(shift_stepper_motor_controller_t* ssmc, uint64_t volume /* ml */, uint64_t brew_duration /* ms */) {
  

  // Powering on coffee machine
  // TODO

  // Moving the tea strainer over the cup
  shift_stepper_motor_controller__move(ssmc, 0, 750);

  const uint64_t pouring_duration = POURING_DURATION_PER_ML * volume;
  vTaskDelay(pdMS_TO_TICKS((pouring_duration + WARMUP_DURATION)/30));

  // When pouring is really slow (due to the coffee maker), the tea already has some time to brew (brewing since the cup is filled at 75%)
  uint64_t actual_brewing_wait_duration = brew_duration - (pouring_duration*(100-BREWING_SINCE_PERCENT))/100;
  if (actual_brewing_wait_duration > 0) // if we jump this brewing time, the coffee maker is way too slow
    vTaskDelay(pdMS_TO_TICKS(actual_brewing_wait_duration/30));

  // Moving the tea strainer out of the cup and over the bin
  shift_stepper_motor_controller__move(ssmc, 0, -750);

  // NEED TO EMPTY THE finished_movement_queue ???
  QueueHandle_t* queue = shift_stepper_motor_controller_finished_movement_queue(ssmc);
  uint8_t id;
  while(xQueueReceive(*queue, &id, 0) == pdTRUE);
}



void app_main(void) {
  shift_register_t sr;
  shift_stepper_motor_controller_t ssmc;

  shift_register__init(&sr, 16, 17, 18, 8);
  shift_stepper_motor_controller__init(&ssmc, 1, &sr, 10, 5);


  // TODO: launch this at the press of a button
  brew(&ssmc, 400, 5*60*1000);


  // NEEDED TO KEEP sr & ssmc variables alive
  // (can always declare them globally instead)
  while(1) vTaskDelay(pdMS_TO_TICKS(100000));
}
