idf_component_register(
    SRCS
        "mimi.c"
        "bus/message_bus.c"
        "wifi/wifi_manager.c"
        "channels/telegram/telegram_bot.c"
        "channels/feishu/feishu_bot.c"
        "llm/llm_proxy.c"
        "agent/agent_loop.c"
        "agent/context_builder.c"
        "memory/memory_store.c"
        "memory/session_mgr.c"
        "gateway/ws_server.c"
        "cli/serial_cli.c"
        "proxy/http_proxy.c"
        "cron/cron_service.c"
        "heartbeat/heartbeat.c"
        "tools/tool_registry.c"
        "tools/tool_cron.c"
        "tools/tool_web_search.c"
        "tools/tool_get_time.c"
        "tools/tool_files.c"
        "tools/tool_gpio.c"
        "tools/gpio_policy.c"
        "tools/tool_oled.c"
        "tools/tool_audio.c"
        "tools/button_handler.c"
        "skills/skill_loader.c"
        "onboard/wifi_onboard.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_http_client esp_http_server
        esp_https_ota esp_event json spiffs console vfs app_update esp-tls
        esp_timer esp_websocket_client esp_driver_gpio esp_driver_i2s driver
)

# Dynamically get the host laptop's IP address
execute_process(
    COMMAND python -c "import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(('8.8.8.8', 80)); print(s.getsockname()[0]); s.close()"
    OUTPUT_VARIABLE HOST_IP
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Pass it as a macro to the C code
target_compile_definitions(${COMPONENT_LIB} PUBLIC "HOST_IP=\"${HOST_IP}\"")
