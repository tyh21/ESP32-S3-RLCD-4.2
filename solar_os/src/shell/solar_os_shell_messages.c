#include "solar_os_shell_commands.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "solar_os_memory.h"
#include "solar_os_messaging.h"
#include "solar_os_shell_common.h"
#include "solar_os_shell_io.h"

static const char * const messages_commands[] = {
    "status", "conversations", "list", "send", "read", "delete",
    "clear", "outbox", "cancel",
};

static void messages_usage(solar_os_shell_io_t *io)
{
    solar_os_shell_io_writeln(io, "usage:");
    solar_os_shell_io_writeln(io, "  messages status");
    solar_os_shell_io_writeln(io, "  messages conversations");
    solar_os_shell_io_writeln(io, "  messages list <conversation-id>");
    solar_os_shell_io_writeln(
        io,
        "  messages send <conversation-id> <text> [--allow-untrusted]");
    solar_os_shell_io_writeln(io, "  messages read <conversation-id>");
    solar_os_shell_io_writeln(io, "  messages delete <message-id>");
    solar_os_shell_io_writeln(
        io,
        "  messages clear <gateway|meshcore|link|all>");
    solar_os_shell_io_writeln(io, "  messages outbox");
    solar_os_shell_io_writeln(io, "  messages cancel <message-id>");
}

static bool messages_parse_provider(
    const char *text,
    solar_os_messaging_provider_id_t *provider)
{
    if (text == NULL || provider == NULL) {
        return false;
    }
    if (strcmp(text, "all") == 0) {
        *provider = 0;
        return true;
    }
    if (strcmp(text, "gateway") == 0) {
        *provider = SOLAR_OS_MESSAGING_PROVIDER_GATEWAY;
        return true;
    }
    if (strcmp(text, "meshcore") == 0) {
        *provider = SOLAR_OS_MESSAGING_PROVIDER_MESHCORE;
        return true;
    }
    if (strcmp(text, "link") == 0) {
        *provider = SOLAR_OS_MESSAGING_PROVIDER_LINK;
        return true;
    }
    return false;
}

static bool messages_parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed == 0 ||
        parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool messages_parse_u64(const char *text, uint64_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long long parsed = strtoull(text, &end, 16);
    if (end == text || *end != '\0' || parsed == 0) {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static void messages_status(solar_os_shell_io_t *io)
{
    solar_os_messaging_status_t status;
    const esp_err_t error = solar_os_messaging_get_status(&status);
    if (error != ESP_OK) {
        solar_os_shell_io_printf(io,
                                 "messages: unavailable: %s\n",
                                 solar_os_shell_error_text(error));
        return;
    }
    solar_os_shell_io_printf(
        io,
        "Conversations: %u/%u\n"
        "Messages: %u/%u (%u unread)\n"
        "Pending outbox: %u/%u (volatile)\n"
        "History: %s, capacity %u, limit %u bytes\n"
        "Dropped: messages %" PRIu32 ", outbox %" PRIu32 "\n",
        (unsigned)status.conversations,
        (unsigned)SOLAR_OS_MESSAGING_CONVERSATION_CAPACITY,
        (unsigned)status.messages,
        (unsigned)SOLAR_OS_MESSAGING_MESSAGE_CAPACITY,
        (unsigned)status.unread,
        (unsigned)status.queued_outbox,
        (unsigned)SOLAR_OS_MESSAGING_OUTBOX_CAPACITY,
        status.persistent ?
            (status.inbox_backed ? "compact internal" : "full") :
            "volatile",
        (unsigned)status.persistent_capacity,
        (unsigned)status.persistent_limit_bytes,
        status.dropped_messages,
        status.dropped_outbox);
    if (status.storage_error != ESP_OK) {
        solar_os_shell_io_printf(io,
                                 "Storage error: %s\n",
                                 solar_os_shell_error_text(status.storage_error));
    }
    for (solar_os_messaging_provider_id_t provider =
             SOLAR_OS_MESSAGING_PROVIDER_GATEWAY;
         provider <= SOLAR_OS_MESSAGING_PROVIDER_LINK;
         provider++) {
        solar_os_messaging_provider_status_t provider_status;
        if (solar_os_messaging_provider_get_status(provider,
                                                   &provider_status) == ESP_OK) {
            solar_os_shell_io_printf(
                io,
                "Provider %-8s: %s%s%s%s\n",
                provider_status.name,
                provider_status.running ? "running" : "stopped",
                provider_status.connected ? ", connected" : "",
                provider_status.detail[0] != '\0' ? ", " : "",
                provider_status.detail[0] != '\0' ?
                    provider_status.detail : "");
        }
    }
}

static void messages_outbox(solar_os_shell_io_t *io)
{
    solar_os_messaging_outbound_t *requests = solar_os_memory_calloc(
        SOLAR_OS_MESSAGING_OUTBOX_CAPACITY,
        sizeof(*requests),
        SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
        "messages.shell.outbox");
    if (requests == NULL) {
        solar_os_shell_io_writeln(io, "outbox: no PSRAM for snapshot");
        return;
    }
    const size_t count = solar_os_messaging_outbox_snapshot(
        requests,
        SOLAR_OS_MESSAGING_OUTBOX_CAPACITY);
    for (size_t i = 0; i < count; i++) {
        const solar_os_messaging_outbound_t *request = &requests[i];
        solar_os_shell_io_printf(
            io,
            "%016" PRIx64 "  %-8s conversation=%" PRIu32
            " attempts=%u  %s\n",
            request->message_key,
            solar_os_messaging_provider_name(request->provider),
            request->conversation_id,
            (unsigned)request->attempts,
            request->body);
    }
    if (count == 0U) {
        solar_os_shell_io_writeln(io, "Outbox is empty");
    }
    solar_os_memory_free(requests);
}

static void messages_conversations(solar_os_shell_io_t *io)
{
    solar_os_messaging_conversation_t *conversations =
        solar_os_memory_calloc(SOLAR_OS_MESSAGING_CONVERSATION_CAPACITY,
                               sizeof(*conversations),
                               SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
                               "messages.shell.conversations");
    if (conversations == NULL) {
        solar_os_shell_io_writeln(io, "messages: no PSRAM for snapshot");
        return;
    }
    const size_t count = solar_os_messaging_conversation_snapshot(
        conversations,
        SOLAR_OS_MESSAGING_CONVERSATION_CAPACITY);
    for (size_t i = 0; i < count; i++) {
        const solar_os_messaging_conversation_t *conversation =
            &conversations[i];
        solar_os_shell_io_printf(
            io,
            "%" PRIu32 "  %-8s %-9s unread=%" PRIu32
            " security=0x%02" PRIx32 "  %s\n",
            conversation->id,
            solar_os_messaging_provider_name(conversation->provider),
            solar_os_conversation_kind_name(conversation->kind),
            conversation->unread_count,
            conversation->security_flags,
            conversation->title);
    }
    if (count == 0) {
        solar_os_shell_io_writeln(io, "No conversations");
    }
    solar_os_memory_free(conversations);
}

typedef struct {
    solar_os_shell_io_t *io;
    size_t count;
} messages_list_context_t;

static bool messages_list_visit(
    const solar_os_messaging_message_t *message,
    void *user)
{
    messages_list_context_t *context = user;
    solar_os_shell_io_printf(
        context->io,
        "%016" PRIx64 "  %-3s %-9s sec=0x%02" PRIx32 "  %s%s%s\n",
        message->key,
        message->direction == SOLAR_OS_MESSAGE_INBOUND ? "in" : "out",
        solar_os_delivery_state_name(message->delivery),
        message->security_flags,
        message->sender[0] != '\0' ? message->sender : "",
        message->sender[0] != '\0' ? ": " : "",
        message->body);
    context->count++;
    return true;
}

static void messages_list(solar_os_shell_io_t *io,
                          solar_os_conversation_id_t conversation_id)
{
    messages_list_context_t context = {
        .io = io,
    };
    (void)solar_os_messaging_message_visit(conversation_id,
                                           0,
                                           messages_list_visit,
                                           &context,
                                           NULL);
    if (context.count == 0) {
        solar_os_shell_io_writeln(io, "No messages");
    }
}

static void messages_send(solar_os_shell_io_t *io,
                          int argc,
                          char **argv,
                          solar_os_conversation_id_t conversation_id)
{
    bool allow_untrusted = false;
    char *body = solar_os_memory_calloc(1,
                                        SOLAR_OS_MESSAGING_BODY_MAX,
                                        SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
                                        "messages.shell.body");
    if (body == NULL) {
        solar_os_shell_io_writeln(io, "messages: no PSRAM for message");
        return;
    }
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--allow-untrusted") == 0) {
            allow_untrusted = true;
            continue;
        }
        if (body[0] != '\0' &&
            strlcat(body, " ", SOLAR_OS_MESSAGING_BODY_MAX) >=
                SOLAR_OS_MESSAGING_BODY_MAX) {
            solar_os_memory_free(body);
            solar_os_shell_io_writeln(io, "messages: body too long");
            return;
        }
        if (strlcat(body, argv[i], SOLAR_OS_MESSAGING_BODY_MAX) >=
            SOLAR_OS_MESSAGING_BODY_MAX) {
            solar_os_memory_free(body);
            solar_os_shell_io_writeln(io, "messages: body too long");
            return;
        }
    }
    if (body[0] == '\0') {
        solar_os_memory_free(body);
        messages_usage(io);
        return;
    }
    solar_os_message_key_t message_key = 0;
    const esp_err_t error =
        solar_os_messaging_send(conversation_id,
                                body,
                                allow_untrusted,
                                &message_key);
    solar_os_memory_free(body);
    if (error == ESP_OK) {
        solar_os_shell_io_printf(io,
                                 "messages: queued %016" PRIx64 "\n",
                                 message_key);
    } else {
        solar_os_shell_io_printf(io,
                                 "messages: send failed: %s\n",
                                 solar_os_shell_error_text(error));
    }
}

void solar_os_shell_cmd_messages(solar_os_context_t *ctx,
                                 int argc,
                                 char **argv)
{
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    if (io == NULL) {
        return;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        messages_status(io);
        return;
    }
    if (argc == 2 && strcmp(argv[1], "conversations") == 0) {
        messages_conversations(io);
        return;
    }
    if (argc == 2 && strcmp(argv[1], "outbox") == 0) {
        messages_outbox(io);
        return;
    }
    uint32_t conversation_id = 0;
    if (argc == 3 && strcmp(argv[1], "list") == 0 &&
        messages_parse_u32(argv[2], &conversation_id)) {
        messages_list(io, conversation_id);
        return;
    }
    if (argc >= 4 && strcmp(argv[1], "send") == 0 &&
        messages_parse_u32(argv[2], &conversation_id)) {
        messages_send(io, argc, argv, conversation_id);
        return;
    }
    if (argc == 3 && strcmp(argv[1], "read") == 0 &&
        messages_parse_u32(argv[2], &conversation_id)) {
        const esp_err_t error =
            solar_os_messaging_mark_read(conversation_id);
        solar_os_shell_io_printf(io,
                                 "messages: %s\n",
                                 error == ESP_OK ? "read" :
                                     solar_os_shell_error_text(error));
        return;
    }
    uint64_t message_id = 0;
    if (argc == 3 && strcmp(argv[1], "delete") == 0 &&
        messages_parse_u64(argv[2], &message_id)) {
        const esp_err_t error =
            solar_os_messaging_message_delete(message_id);
        solar_os_shell_io_printf(io,
                                 "messages: %s\n",
                                 error == ESP_OK ? "deleted" :
                                     solar_os_shell_error_text(error));
        return;
    }
    solar_os_messaging_provider_id_t provider = 0;
    if (argc == 3 && strcmp(argv[1], "clear") == 0 &&
        messages_parse_provider(argv[2], &provider)) {
        size_t removed = 0;
        const esp_err_t error = solar_os_messaging_clear(provider, &removed);
        if (error == ESP_OK) {
            solar_os_shell_io_printf(io,
                                     "messages: cleared %u %s messages\n",
                                     (unsigned)removed,
                                     argv[2]);
        } else {
            solar_os_shell_io_printf(io,
                                     "messages: clear failed: %s\n",
                                     solar_os_shell_error_text(error));
        }
        return;
    }
    if (argc == 3 && strcmp(argv[1], "cancel") == 0 &&
        messages_parse_u64(argv[2], &message_id)) {
        const esp_err_t error = solar_os_messaging_cancel(message_id);
        solar_os_shell_io_printf(io,
                                 "messages: %s\n",
                                 error == ESP_OK ? "cancelled" :
                                     solar_os_shell_error_text(error));
        return;
    }
    solar_os_shell_diag_subcommand(io,
                                   "messages",
                                   argc,
                                   argv,
                                   "messages status|conversations|list|send|read|delete|clear|outbox|cancel",
                                   messages_commands,
                                   sizeof(messages_commands) / sizeof(messages_commands[0]));
}

void solar_os_shell_cmd_outbox(solar_os_context_t *ctx,
                               int argc,
                               char **argv)
{
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    if (io == NULL) {
        return;
    }
    if (argc == 1 ||
        (argc == 2 && strcmp(argv[1], "list") == 0)) {
        messages_outbox(io);
        return;
    }
    uint64_t message_id = 0;
    if (argc == 3 && strcmp(argv[1], "cancel") == 0 &&
        messages_parse_u64(argv[2], &message_id)) {
        const esp_err_t error = solar_os_messaging_cancel(message_id);
        solar_os_shell_io_printf(io,
                                 "outbox: %s\n",
                                 error == ESP_OK ? "cancelled" :
                                     solar_os_shell_error_text(error));
        return;
    }
    solar_os_shell_io_writeln(io, "usage: outbox [list]");
    solar_os_shell_io_writeln(io, "       outbox cancel <message-id>");
}
