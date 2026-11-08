idf_component_register(
    SRCS
        "mimi.c"
        "bus/message_bus.c"
        "display/display_status.c"
        "wifi/wifi_manager.c"
        "channels/telegram/telegram_bot.c"
        "channels/cloud_link/cloud_link.c"
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
        "sync/config_sync.c"
        "sync/backend_discovery.c"
        "tools/tool_registry.c"
        "tools/tool_cron.c"
        "tools/tool_web_search.c"
        "tools/tool_get_time.c"
        "tools/tool_files.c"
        "tools/tool_gpio.c"
        "tools/gpio_policy.c"
        "tools/tool_audio.c"
        "tools/tool_camera.c"
        "tools/button_handler.c"
        "voice/voice_client.c"
        "skills/skill_loader.c"
        "onboard/wifi_onboard.c"
        "onboard/wifi_onboard_ble.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_http_client esp_http_server
        esp_https_ota esp_event json spiffs console vfs app_update esp-tls
        esp_timer esp_websocket_client esp_driver_gpio esp_driver_i2s driver esp32-camera
        mdns ssd1306 wifi_provisioning bt protocomm
)

# Dynamically get the host laptop's IP address
execute_process(
    COMMAND python -c "import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(('8.8.8.8', 80)); print(s.getsockname()[0]); s.close()"
    OUTPUT_VARIABLE HOST_IP
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Pass it as a macro to the C code
target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    "HOST_IP=\"${HOST_IP}\""
    "BACKEND_IP=\"${HOST_IP}\""
)
