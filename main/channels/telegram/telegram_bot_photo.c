esp_err_t telegram_send_photo(const char *chat_id, const uint8_t *jpeg_data, size_t jpeg_len, const char *caption)
{
    if (!chat_id || chat_id[0] == '\0' || !jpeg_data || jpeg_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bot_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send photo: no bot token");
        return ESP_ERR_INVALID_STATE;
    }

    // Currently we skip proxy for photo upload for simplicity, just direct HTTPS
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendPhoto", s_bot_token);

    http_resp_t resp = {
        .buf = calloc(1, 4096),
        .len = 0,
        .cap = 4096,
    };
    if (!resp.buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp.buf);
        return ESP_FAIL;
    }

    const char *boundary = "----WebKitFormBoundaryesp32s3mimi";
    char header_content_type[128];
    snprintf(header_content_type, sizeof(header_content_type), "multipart/form-data; boundary=%s", boundary);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", header_content_type);

    // Build the multipart payload parts
    char body_start[512];
    int start_len = snprintf(body_start, sizeof(body_start),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        boundary, chat_id, boundary);

    char body_end[256];
    int end_len = 0;
    if (caption && caption[0] != '\0') {
        end_len = snprintf(body_end, sizeof(body_end),
            "\r\n--%s\r\n"
            "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
            "%s\r\n"
            "--%s--\r\n",
            boundary, caption, boundary);
    } else {
        end_len = snprintf(body_end, sizeof(body_end),
            "\r\n--%s--\r\n",
            boundary);
    }

    // Set total content length
    int total_len = start_len + jpeg_len + end_len;
    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%d", total_len);
    esp_http_client_set_header(client, "Content-Length", len_str);

    // Open connection
    esp_err_t err = esp_http_client_open(client, total_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Uploading photo to Telegram (%d bytes total)...", total_len);
        esp_http_client_write(client, body_start, start_len);
        
        // Write JPEG data in chunks if needed, but esp_http_client_write handles it
        esp_http_client_write(client, (const char *)jpeg_data, jpeg_len);
        
        esp_http_client_write(client, body_end, end_len);
        
        // Read response
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length >= 0) {
            esp_http_client_read_response(client, resp.buf, resp.cap - 1);
            ESP_LOGI(TAG, "Telegram photo send response: %.300s", resp.buf);
        } else {
            ESP_LOGE(TAG, "Failed to read response headers");
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(resp.buf);

    return err;
}
