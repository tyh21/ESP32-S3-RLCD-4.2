#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "solar_os_agent.h"
#include "solar_os_agent_provider.h"

typedef struct {
    solar_os_agent_history_message_t messages[SOLAR_OS_AGENT_HISTORY_MESSAGE_MAX];
    size_t message_count;
    char *storage;
    size_t storage_size;
    char provider_response_id[96];
} solar_os_agent_history_t;

esp_err_t solar_os_agent_conversation_commit(
    const char *id,
    const char *provider,
    const char *model,
    const char *user_text,
    const char *assistant_text,
    const char *tool_summary,
    const char *provider_response_id,
    char *committed_id,
    size_t committed_id_len);

esp_err_t solar_os_agent_conversation_load_history(
    const char *id,
    bool include_messages,
    solar_os_agent_history_t *history);

void solar_os_agent_conversation_free_history(
    solar_os_agent_history_t *history);
