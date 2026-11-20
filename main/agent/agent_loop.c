#include "agent_loop.h"
#include "agent/context_builder.h"
#include "bus/message_bus.h"
#include "display/display_status.h"
#include "llm/llm_proxy.h"
#include "memory/session_mgr.h"
#include "mimi_config.h"
#include "tools/tool_registry.h"


#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>


static const char *TAG = "agent";

#define TOOL_OUTPUT_SIZE (8 * 1024)

/* Build the assistant content array from llm_response_t for the messages
 * history. Returns a cJSON array with text and tool_use blocks. */
static cJSON *build_assistant_content(const llm_response_t *resp) {
  cJSON *content = cJSON_CreateArray();

  /* Text block */
  if (resp->text && resp->text_len > 0) {
    cJSON *text_block = cJSON_CreateObject();
    cJSON_AddStringToObject(text_block, "type", "text");
    cJSON_AddStringToObject(text_block, "text", resp->text);
    cJSON_AddItemToArray(content, text_block);
  }

  /* Tool use blocks */
  for (int i = 0; i < resp->call_count; i++) {
    const llm_tool_call_t *call = &resp->calls[i];
    cJSON *tool_block = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_block, "type", "tool_use");
    cJSON_AddStringToObject(tool_block, "id", call->id);
    cJSON_AddStringToObject(tool_block, "name", call->name);

    cJSON *input = cJSON_Parse(call->input);
    if (input) {
      cJSON_AddItemToObject(tool_block, "input", input);
    } else {
      cJSON_AddItemToObject(tool_block, "input", cJSON_CreateObject());
    }

    cJSON_AddItemToArray(content, tool_block);
  }

  return content;
}

static void json_set_string(cJSON *obj, const char *key, const char *value) {
  if (!obj || !key || !value) {
    return;
  }
  cJSON_DeleteItemFromObject(obj, key);
  cJSON_AddStringToObject(obj, key, value);
}

static void append_turn_context_prompt(char *prompt, size_t size,
                                       const mimi_msg_t *msg) {
  if (!prompt || size == 0 || !msg) {
    return;
  }

  size_t off = strnlen(prompt, size - 1);
  if (off >= size - 1) {
    return;
  }

  int n = snprintf(prompt + off, size - off,
                   "\n## Current Turn Context\n"
                   "- source_channel: %s\n"
                   "- source_chat_id: %s\n"
                   "- If using cron_add for Telegram in this turn, set "
                   "channel='telegram' and chat_id to source_chat_id.\n"
                   "- Never use chat_id 'cron' for Telegram messages.\n",
                   msg->channel[0] ? msg->channel : "(unknown)",
                   msg->chat_id[0] ? msg->chat_id : "(empty)");

  if (n < 0 || (size_t)n >= (size - off)) {
    prompt[size - 1] = '\0';
  }
}

static char *patch_tool_input_with_context(const llm_tool_call_t *call,
                                           const mimi_msg_t *msg) {
  if (!call || !msg || strcmp(call->name, "cron_add") != 0) {
    return NULL;
  }

  cJSON *root = cJSON_Parse(call->input ? call->input : "{}");
  if (!root || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    root = cJSON_CreateObject();
  }
  if (!root) {
    return NULL;
  }

  bool changed = false;

  cJSON *channel_item = cJSON_GetObjectItem(root, "channel");
  const char *channel =
      cJSON_IsString(channel_item) ? channel_item->valuestring : NULL;

  if ((!channel || channel[0] == '\0') && msg->channel[0] != '\0') {
    json_set_string(root, "channel", msg->channel);
    channel = msg->channel;
    changed = true;
  }

  if (channel && strcmp(channel, MIMI_CHAN_TELEGRAM) == 0 &&
      strcmp(msg->channel, MIMI_CHAN_TELEGRAM) == 0 &&
      msg->chat_id[0] != '\0') {
    cJSON *chat_item = cJSON_GetObjectItem(root, "chat_id");
    const char *chat_id =
        cJSON_IsString(chat_item) ? chat_item->valuestring : NULL;
    if (!chat_id || chat_id[0] == '\0' || strcmp(chat_id, "cron") == 0) {
      json_set_string(root, "chat_id", msg->chat_id);
      changed = true;
    }
  }

  char *patched = NULL;
  if (changed) {
    patched = cJSON_PrintUnformatted(root);
    if (patched) {
      ESP_LOGI(TAG, "Patched cron_add target to %s:%s", msg->channel,
               msg->chat_id);
    }
  }

  cJSON_Delete(root);
  return patched;
}

/* Build the user message with tool_result blocks */
static cJSON *build_tool_results(const llm_response_t *resp,
                                 const mimi_msg_t *msg, char *tool_output,
                                 size_t tool_output_size) {
  cJSON *content = cJSON_CreateArray();

  for (int i = 0; i < resp->call_count; i++) {
    const llm_tool_call_t *call = &resp->calls[i];
    const char *tool_input = call->input ? call->input : "{}";
    char *patched_input = patch_tool_input_with_context(call, msg);
    if (patched_input) {
      tool_input = patched_input;
    }

    /* Execute tool */
    tool_output[0] = '\0';
    tool_registry_execute(call->name, tool_input, tool_output,
                          tool_output_size);
    free(patched_input);

    ESP_LOGI(TAG, "Tool %s result: %d bytes", call->name,
             (int)strlen(tool_output));

    /* Build tool_result block */
    cJSON *result_block = cJSON_CreateObject();
    cJSON_AddStringToObject(result_block, "type", "tool_result");
    cJSON_AddStringToObject(result_block, "tool_use_id", call->id);
    cJSON_AddStringToObject(result_block, "content", tool_output);
    cJSON_AddItemToArray(content, result_block);
  }

  return content;
}

static void word_wrap_oled_local(const char *text, char *l1, size_t l1_sz, char *l2, size_t l2_sz) {
  if (l1 && l1_sz > 0) l1[0] = '\0';
  if (l2 && l2_sz > 0) l2[0] = '\0';
  if (!text || !*text || !l1 || !l2) return;

  while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') text++;

  size_t tlen = strlen(text);
  if (tlen < l1_sz) {
    strncpy(l1, text, l1_sz - 1);
    l1[l1_sz - 1] = '\0';
    return;
  }

  int split1 = 16;
  while (split1 > 0 && text[split1] != ' ' && text[split1] != '\0') {
    split1--;
  }
  if (split1 == 0) split1 = 16;

  int copy1 = split1 < (int)l1_sz ? split1 : (int)l1_sz - 1;
  strncpy(l1, text, copy1);
  l1[copy1] = '\0';

  const char *p2 = text + split1;
  while (*p2 == ' ' || *p2 == '\t' || *p2 == '\n' || *p2 == '\r') p2++;

  if (*p2) {
    int split2 = 16;
    size_t rem = strlen(p2);
    if (rem < (size_t)split2) {
      split2 = (int)rem;
    } else {
      while (split2 > 0 && p2[split2] != ' ' && p2[split2] != '\0') {
        split2--;
      }
      if (split2 == 0) split2 = 16;
    }
    int copy2 = split2 < (int)l2_sz ? split2 : (int)l2_sz - 1;
    strncpy(l2, p2, copy2);
    l2[copy2] = '\0';
  }
}

static bool extract_and_strip_oled_tag(char *text, char *out_l1, size_t l1_sz, char *out_l2, size_t l2_sz) {
  if (out_l1 && l1_sz > 0) out_l1[0] = '\0';
  if (out_l2 && l2_sz > 0) out_l2[0] = '\0';
  if (!text || !*text) return false;

  char *tag_start = strcasestr(text, "[OLED:");
  bool colon_format = true;
  if (!tag_start) {
    tag_start = strcasestr(text, "[OLED]");
    colon_format = false;
  }
  if (!tag_start) return false;

  char *tag_end = strchr(tag_start, ']');
  if (!tag_end) return false;

  char raw_tag[128] = {0};
  size_t tag_len = (size_t)(tag_end - tag_start + 1);
  if (tag_len >= sizeof(raw_tag)) tag_len = sizeof(raw_tag) - 1;
  strncpy(raw_tag, tag_start, tag_len);
  raw_tag[tag_len] = '\0';

  char *content = colon_format ? (raw_tag + 6) : (raw_tag + 6);
  while (*content == ' ' || *content == '\t') content++;

  char *delim = strchr(content, '|');
  if (!delim) delim = strchr(content, '\n');
  if (!delim) delim = strchr(content, ';');

  if (delim) {
    *delim = '\0';
    char *p1 = content;
    char *p2 = delim + 1;

    char *close_bracket = strchr(p2, ']');
    if (close_bracket) *close_bracket = '\0';

    while (*p1 == ' ' || *p1 == '\t') p1++;
    char *end1 = p1 + strlen(p1) - 1;
    while (end1 > p1 && (*end1 == ' ' || *end1 == '\t' || *end1 == '\r' || *end1 == '\n')) {
      *end1 = '\0';
      end1--;
    }

    while (*p2 == ' ' || *p2 == '\t' || *p2 == '\r' || *p2 == '\n') p2++;
    char *end2 = p2 + strlen(p2) - 1;
    while (end2 > p2 && (*end2 == ' ' || *end2 == '\t' || *end2 == '\r' || *end2 == '\n')) {
      *end2 = '\0';
      end2--;
    }

    if (out_l1 && l1_sz > 0) {
      strncpy(out_l1, p1, l1_sz - 1);
      out_l1[l1_sz - 1] = '\0';
    }
    if (out_l2 && l2_sz > 0) {
      strncpy(out_l2, p2, l2_sz - 1);
      out_l2[l2_sz - 1] = '\0';
    }
  } else {
    char *close_bracket = strchr(content, ']');
    if (close_bracket) *close_bracket = '\0';
    while (*content == ' ' || *content == '\t') content++;
    word_wrap_oled_local(content, out_l1, l1_sz, out_l2, l2_sz);
  }

  char *after = tag_end + 1;
  while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
  memmove(tag_start, after, strlen(after) + 1);

  return true;
}

static void agent_loop_task(void *arg) {
  ESP_LOGI(TAG, "Agent loop started on core %d", xPortGetCoreID());

  /* Allocate large buffers from PSRAM */
  char *system_prompt =
      heap_caps_calloc(1, MIMI_CONTEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
  char *history_json =
      heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
  char *tool_output = heap_caps_calloc(1, TOOL_OUTPUT_SIZE, MALLOC_CAP_SPIRAM);

  if (!system_prompt || !history_json || !tool_output) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM buffers");
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    mimi_msg_t msg;
    esp_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
    if (err != ESP_OK)
      continue;

    ESP_LOGI(TAG, "Processing message from %s:%s", msg.channel, msg.chat_id);
    display_status_set("Msg received", msg.channel);

    /* Check for /clear or /reset command */
    if (msg.content && (strcmp(msg.content, "/clear") == 0 || strcmp(msg.content, "/reset") == 0)) {
      session_clear(msg.chat_id);
      mimi_msg_t out = {0};
      strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
      strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
      out.content = strdup("Chat memory cleared! I'm ready for a fresh start.");
      if (out.content) {
        message_bus_push_outbound(&out);
      }
      free(msg.content);
      continue;
    }

    /* 1. Build system prompt */
    /* Refresh tools_json every turn so remote tools fetched after boot are included */
    const char *tools_json = tool_registry_get_tools_json();
    context_build_system_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE);
    append_turn_context_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE, &msg);
    ESP_LOGI(TAG, "LLM turn context: channel=%s chat_id=%s, tools=%d bytes",
             msg.channel, msg.chat_id,
             tools_json ? (int)strlen(tools_json) : 0);

    /* 2. Load session history into cJSON array */
    session_get_history_json(msg.chat_id, history_json,
                             MIMI_LLM_STREAM_BUF_SIZE, MIMI_AGENT_MAX_HISTORY);

    cJSON *messages = cJSON_Parse(history_json);
    if (!messages)
      messages = cJSON_CreateArray();

    /* 3. Append current user message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", msg.content);
    cJSON_AddItemToArray(messages, user_msg);

    /* 4. ReAct loop */
    char *final_text = NULL;
    int iteration = 0;
    bool sent_working_status = false;

    while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
      /* Send "working" indicator before each API call */
#if MIMI_AGENT_SEND_WORKING_STATUS
      if (!sent_working_status && msg.chat_id[0] != '\0' && strcmp(msg.channel, MIMI_CHAN_SYSTEM) != 0) {
        mimi_msg_t status = {0};
        strncpy(status.channel, msg.channel, sizeof(status.channel) - 1);
        strncpy(status.chat_id, msg.chat_id, sizeof(status.chat_id) - 1);
        status.content = strdup("ESPeting...");
        if (status.content) {
          if (message_bus_push_outbound(&status) != ESP_OK) {
            ESP_LOGW(TAG, "Outbound queue full, drop working status");
            free(status.content);
          } else {
            sent_working_status = true;
          }
        }
      }
#endif

      display_status_set("Thinking...", "");
      llm_response_t resp;
      err = llm_chat_tools(system_prompt, messages, tools_json, &resp);

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "LLM call failed: %s", esp_err_to_name(err));
        display_status_set("LLM error", "");
        break;
      }

      if (!resp.tool_use) {
        /* Normal completion — save final text and break */
        if (resp.text && resp.text_len > 0) {
          final_text = strdup(resp.text);
        }
        llm_response_free(&resp);
        break;
      }

      ESP_LOGI(TAG, "Tool use iteration %d: %d calls", iteration + 1,
               resp.call_count);

      /* Append assistant message with content array */
      cJSON *asst_msg = cJSON_CreateObject();
      cJSON_AddStringToObject(asst_msg, "role", "assistant");
      cJSON_AddItemToObject(asst_msg, "content",
                            build_assistant_content(&resp));
      cJSON_AddItemToArray(messages, asst_msg);

      /* Execute tools and append results */
      cJSON *tool_results =
          build_tool_results(&resp, &msg, tool_output, TOOL_OUTPUT_SIZE);
      cJSON *result_msg = cJSON_CreateObject();
      cJSON_AddStringToObject(result_msg, "role", "user");
      cJSON_AddItemToObject(result_msg, "content", tool_results);
      cJSON_AddItemToArray(messages, result_msg);

      llm_response_free(&resp);
      iteration++;
    }

    cJSON_Delete(messages);

    /* 5. Send response */
    if (final_text && final_text[0]) {
      /* Extract OLED summary tag and update screen immediately with timed hold */
      char oled_l1[17] = {0};
      char oled_l2[17] = {0};
      if (extract_and_strip_oled_tag(final_text, oled_l1, sizeof(oled_l1), oled_l2, sizeof(oled_l2))) {
        ESP_LOGI(TAG, "OLED summary tag parsed: '%s' / '%s'", oled_l1, oled_l2);
        display_status_set_text_timed(oled_l1, oled_l2, 8000);
      } else {
        word_wrap_oled_local(final_text, oled_l1, sizeof(oled_l1), oled_l2, sizeof(oled_l2));
        ESP_LOGI(TAG, "OLED fallback wrap: '%s' / '%s'", oled_l1, oled_l2);
        display_status_set_text_timed(oled_l1, oled_l2, 8000);
      }

      /* Save to session (only user text + clean assistant text) */
      esp_err_t save_user = session_append(msg.chat_id, "user", msg.content);
      esp_err_t save_asst =
          session_append(msg.chat_id, "assistant", final_text);
      if (save_user != ESP_OK || save_asst != ESP_OK) {
        ESP_LOGW(TAG, "Session save failed for chat %s (user=%s, assistant=%s)",
                 msg.chat_id, esp_err_to_name(save_user),
                 esp_err_to_name(save_asst));
      } else {
        ESP_LOGI(TAG, "Session saved for chat %s", msg.chat_id);
      }

      /* Push response to outbound */
      mimi_msg_t out = {0};
      strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
      strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
      out.content = final_text; /* transfer ownership */
      ESP_LOGI(TAG, "Queue final response to %s:%s (%d bytes)", out.channel,
               out.chat_id, (int)strlen(final_text));
      if (message_bus_push_outbound(&out) != ESP_OK) {
        ESP_LOGW(TAG, "Outbound queue full, drop final response");
        free(final_text);
      } else {
        final_text = NULL;
      }
    } else {
      /* Error or empty response fallback */
      free(final_text);
      mimi_msg_t out = {0};
      strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
      strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
      if (iteration > 0 && tool_output && (strstr(tool_output, "success") || strstr(tool_output, "displayed") || strstr(tool_output, "HIGH") || strstr(tool_output, "LOW"))) {
        out.content = strdup("Executed tool successfully.");
        display_status_set_text_timed("Action Executed", "Success", 8000);
      } else {
        out.content = strdup("Sorry, I encountered an error.");
        display_status_set_text_timed("Agent Error", "Please try again", 8000);
      }
      if (out.content) {
        if (message_bus_push_outbound(&out) != ESP_OK) {
          ESP_LOGW(TAG, "Outbound queue full, drop response");
          free(out.content);
        }
      }
    }


    /* Free inbound message content */
    free(msg.content);
    display_status_idle();

    /* Log memory status */
    ESP_LOGI(TAG, "Free PSRAM: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }
}

esp_err_t agent_loop_init(void) {
  ESP_LOGI(TAG, "Agent loop initialized");
  return ESP_OK;
}

esp_err_t agent_loop_start(void) {
  const uint32_t stack_candidates[] = {
      MIMI_AGENT_STACK, 20 * 1024, 16 * 1024, 14 * 1024, 12 * 1024,
  };

  for (size_t i = 0;
       i < (sizeof(stack_candidates) / sizeof(stack_candidates[0])); i++) {
    uint32_t stack_size = stack_candidates[i];
    BaseType_t ret =
        xTaskCreatePinnedToCore(agent_loop_task, "agent_loop", stack_size, NULL,
                                MIMI_AGENT_PRIO, NULL, MIMI_AGENT_CORE);

    if (ret == pdPASS) {
      ESP_LOGI(TAG, "agent_loop task created with stack=%u bytes",
               (unsigned)stack_size);
      return ESP_OK;
    }

    ESP_LOGW(TAG,
             "agent_loop create failed (stack=%u, free_internal=%u, "
             "largest_internal=%u), retrying...",
             (unsigned)stack_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  }

  return ESP_FAIL;
}
