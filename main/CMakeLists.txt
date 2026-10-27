idf_component_register(
    SRCS "main.c" "event_script.c" "event_auto.c" "monitor.c" "monitor_protocol.c" "time_task.c" "experiment.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_touch_sens esp_driver_gptimer esp_timer
                  esp_wifi esp_event esp_netif nvs_flash lwip
)
