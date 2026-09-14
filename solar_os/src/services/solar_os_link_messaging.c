#include "solar_os_link_messaging.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_random.h"
#include "solar_os_config.h"

#if SOLAR_OS_PACKAGE_SERVICE_MESSAGING

#include "solar_os_contacts.h"
#include "solar_os_log.h"
#include "solar_os_messaging.h"

#define LINK_MESSAGING_ACK_TIMEOUT_MS 10000U

typedef struct {
    bool active;
    bool inflight;
    bool transmitted;
    char link[SOLAR_OS_LINK_NAME_MAX];
    size_t payload_max;
    uint32_t request_id;
    uint32_t destination;
    uint16_t sequence;
    uint32_t session_epoch;
    uint32_t ack_deadline_ms;
} link_messaging_state_t;

static link_messaging_state_t link_messaging;
static EXT_RAM_BSS_ATTR solar_os_messaging_outbound_t link_messaging_outbound;
static const char *TAG = "link_messaging";

static uint64_t link_messaging_provider_key(uint32_t source,
                                            uint16_t sequence)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    const uint8_t identity[] = {
        (uint8_t)(link_messaging.session_epoch >> 24),
        (uint8_t)(link_messaging.session_epoch >> 16),
        (uint8_t)(link_messaging.session_epoch >> 8),
        (uint8_t)link_messaging.session_epoch,
        (uint8_t)(source >> 24),
        (uint8_t)(source >> 16),
        (uint8_t)(source >> 8),
        (uint8_t)source,
        (uint8_t)(sequence >> 8),
        (uint8_t)sequence,
    };
    for (size_t i = 0; i < sizeof(identity); i++) {
        hash ^= identity[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash != 0U ? hash : UINT64_MAX;
}

static void link_messaging_address_encode(uint32_t device_id, uint8_t address[4])
{
    address[0] = (uint8_t)(device_id >> 24);
    address[1] = (uint8_t)(device_id >> 16);
    address[2] = (uint8_t)(device_id >> 8);
    address[3] = (uint8_t)device_id;
}

static bool link_messaging_address_decode(const solar_os_messaging_address_t *address,
                                          uint32_t *device_id)
{
    if (address == NULL || device_id == NULL || address->length != 4U) {
        return false;
    }
    *device_id = ((uint32_t)address->bytes[0] << 24) | ((uint32_t)address->bytes[1] << 16) |
                 ((uint32_t)address->bytes[2] << 8) | (uint32_t)address->bytes[3];
    return *device_id != 0U && *device_id != SOLAR_OS_LINK_BROADCAST;
}

static void link_messaging_clear_inflight(void)
{
    link_messaging.inflight = false;
    link_messaging.transmitted = false;
    link_messaging.request_id = 0U;
    link_messaging.destination = 0U;
    link_messaging.sequence = 0U;
    link_messaging.ack_deadline_ms = 0U;
}

static void link_messaging_update_inflight(solar_os_delivery_state_t state, const char *error)
{
    if (!link_messaging.inflight) {
        return;
    }
    (void)solar_os_messaging_outbox_update(link_messaging.request_id, state, error);
    if (state == SOLAR_OS_DELIVERY_SENT || state == SOLAR_OS_DELIVERY_DELIVERED ||
        state == SOLAR_OS_DELIVERY_FAILED) {
        link_messaging_clear_inflight();
    }
}

static esp_err_t link_messaging_fail_outbound(uint32_t request_id, const char *error)
{
    return solar_os_messaging_outbox_update(request_id, SOLAR_OS_DELIVERY_FAILED, error);
}

bool solar_os_link_messaging_available(void) { return true; }

esp_err_t solar_os_link_messaging_start(const char *link)
{
    if (link == NULL || link[0] == '\0' ||
        strnlen(link, SOLAR_OS_LINK_NAME_MAX) >= SOLAR_OS_LINK_NAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_messaging.active) {
        return ESP_ERR_INVALID_STATE;
    }

    solar_os_link_status_t status;
    esp_err_t error = solar_os_link_get_status(link, &status);
    if (error != ESP_OK) {
        return error;
    }
    error = solar_os_messaging_provider_register(SOLAR_OS_MESSAGING_PROVIDER_LINK, "link");
    if (error != ESP_OK) {
        return error;
    }

    char provider_key[SOLAR_OS_MESSAGING_PROVIDER_KEY_MAX];
    char title[SOLAR_OS_MESSAGING_TITLE_MAX];
    snprintf(provider_key, sizeof(provider_key), "b:%s", link);
    snprintf(title, sizeof(title), "%s broadcast", link);
    const solar_os_messaging_conversation_upsert_t conversation = {
        .provider = SOLAR_OS_MESSAGING_PROVIDER_LINK,
        .provider_key = provider_key,
        .kind = SOLAR_OS_CONVERSATION_BROADCAST,
        .title = title,
        .group_ref = SOLAR_OS_LINK_BROADCAST,
    };
    error = solar_os_messaging_conversation_upsert(&conversation, NULL);
    if (error != ESP_OK) {
        (void)solar_os_messaging_provider_set_status(SOLAR_OS_MESSAGING_PROVIDER_LINK, false, false,
                                                     error, "broadcast conversation unavailable");
        return error;
    }

    memset(&link_messaging, 0, sizeof(link_messaging));
    do {
        link_messaging.session_epoch = esp_random();
    } while (link_messaging.session_epoch == 0U);
    link_messaging.active = true;
    link_messaging.payload_max =
        status.frame_mtu - SOLAR_OS_LINK_HEADER_SIZE - SOLAR_OS_LINK_CRC_SIZE;
    strlcpy(link_messaging.link, link, sizeof(link_messaging.link));

    char detail[SOLAR_OS_MESSAGING_ERROR_MAX];
    snprintf(detail, sizeof(detail), "%s local=%08" PRIx32 " payload=%u", link, status.local_id,
             (unsigned)link_messaging.payload_max);
    error = solar_os_messaging_provider_set_status(SOLAR_OS_MESSAGING_PROVIDER_LINK, true, true,
                                                   ESP_OK, detail);
    if (error != ESP_OK) {
        memset(&link_messaging, 0, sizeof(link_messaging));
    }
    return error;
}

void solar_os_link_messaging_stop(void)
{
    if (!link_messaging.active) {
        return;
    }
    if (link_messaging.inflight) {
        (void)solar_os_messaging_outbox_update(link_messaging.request_id, SOLAR_OS_DELIVERY_QUEUED,
                                               "Link stopped before delivery");
    }
    (void)solar_os_messaging_provider_set_status(SOLAR_OS_MESSAGING_PROVIDER_LINK, false, false,
                                                 ESP_OK, NULL);
    memset(&link_messaging, 0, sizeof(link_messaging));
}

void solar_os_link_messaging_process(uint32_t now_ms)
{
    if (!link_messaging.active) {
        return;
    }
    if (link_messaging.inflight) {
        if (link_messaging.transmitted && link_messaging.destination != SOLAR_OS_LINK_BROADCAST &&
            (int32_t)(now_ms - link_messaging.ack_deadline_ms) >= 0) {
            link_messaging_update_inflight(SOLAR_OS_DELIVERY_FAILED,
                                           "Link acknowledgement timed out");
        }
        return;
    }

    solar_os_messaging_outbound_t *outbound = &link_messaging_outbound;
    if (solar_os_messaging_outbox_peek(SOLAR_OS_MESSAGING_PROVIDER_LINK, outbound) != ESP_OK) {
        return;
    }

    solar_os_messaging_conversation_t conversation;
    if (solar_os_messaging_conversation_get(outbound->conversation_id, &conversation) != ESP_OK) {
        (void)link_messaging_fail_outbound(outbound->id, "Link conversation unavailable");
        return;
    }

    uint32_t destination = 0U;
    if (conversation.kind == SOLAR_OS_CONVERSATION_BROADCAST) {
        destination = SOLAR_OS_LINK_BROADCAST;
    } else if (conversation.kind == SOLAR_OS_CONVERSATION_DIRECT &&
               conversation.endpoint_id != SOLAR_OS_ENDPOINT_ID_NONE) {
        solar_os_endpoint_t endpoint;
        if (solar_os_contacts_get_endpoint(conversation.endpoint_id, &endpoint) != ESP_OK ||
            endpoint.provider != SOLAR_OS_MESSAGING_PROVIDER_LINK ||
            endpoint.trust == SOLAR_OS_CONTACT_TRUST_BLOCKED ||
            !link_messaging_address_decode(&endpoint.address, &destination)) {
            (void)link_messaging_fail_outbound(outbound->id, "Link contact unavailable or blocked");
            return;
        }
    } else {
        (void)link_messaging_fail_outbound(outbound->id,
                                           "Link supports direct and broadcast conversations");
        return;
    }

    const size_t body_len = strnlen(outbound->body, sizeof(outbound->body));
    if (body_len > link_messaging.payload_max) {
        char error[SOLAR_OS_MESSAGING_ERROR_MAX];
        snprintf(error, sizeof(error), "Link text exceeds %u-byte payload",
                 (unsigned)link_messaging.payload_max);
        (void)link_messaging_fail_outbound(outbound->id, error);
        return;
    }

    uint16_t sequence = 0U;
    const esp_err_t error = solar_os_link_send(link_messaging.link, SOLAR_OS_LINK_MESSAGE_TEXT,
                                               destination, outbound->body, body_len, &sequence);
    if (error == ESP_ERR_NO_MEM) {
        return;
    }
    if (error != ESP_OK) {
        (void)link_messaging_fail_outbound(outbound->id, esp_err_to_name(error));
        return;
    }

    link_messaging.inflight = true;
    link_messaging.request_id = outbound->id;
    link_messaging.destination = destination;
    link_messaging.sequence = sequence;
    if (solar_os_messaging_outbox_update(outbound->id, SOLAR_OS_DELIVERY_SENDING, NULL) != ESP_OK) {
        SOLAR_OS_LOGW(TAG, "outbox request %" PRIu32 " disappeared after queueing", outbound->id);
        link_messaging_clear_inflight();
    }
}

void solar_os_link_messaging_note_transmit(const solar_os_link_frame_t *frame, esp_err_t result,
                                           uint32_t now_ms)
{
    if (!link_messaging.active || !link_messaging.inflight || frame == NULL) {
        return;
    }
    solar_os_link_message_t message;
    if (solar_os_link_decode(frame->data, frame->len, &message) != ESP_OK ||
        message.type != SOLAR_OS_LINK_MESSAGE_TEXT || message.sequence != link_messaging.sequence ||
        message.destination != link_messaging.destination) {
        return;
    }
    if (result != ESP_OK) {
        link_messaging_update_inflight(SOLAR_OS_DELIVERY_FAILED, esp_err_to_name(result));
        return;
    }
    if (message.destination == SOLAR_OS_LINK_BROADCAST) {
        link_messaging_update_inflight(SOLAR_OS_DELIVERY_SENT, NULL);
        return;
    }
    link_messaging.transmitted = true;
    link_messaging.ack_deadline_ms = now_ms + LINK_MESSAGING_ACK_TIMEOUT_MS;
}

esp_err_t solar_os_link_messaging_note_ingest(const solar_os_link_ingest_result_t *result,
                                              uint32_t now_ms)
{
    if (!link_messaging.active || result == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const solar_os_link_message_t *message = &result->message;
    if (result->acknowledgement) {
        if (link_messaging.inflight && link_messaging.destination == message->source &&
            link_messaging.sequence == message->sequence) {
            link_messaging_update_inflight(SOLAR_OS_DELIVERY_DELIVERED, NULL);
        }
        return ESP_OK;
    }
    if (!result->accepted || message->type != SOLAR_OS_LINK_MESSAGE_TEXT) {
        return ESP_OK;
    }
    if (message->payload_len > SOLAR_OS_LINK_PAYLOAD_MAX ||
        memchr(message->payload, '\0', message->payload_len) != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char body[SOLAR_OS_LINK_PAYLOAD_MAX + 1U];
    memcpy(body, message->payload, message->payload_len);
    body[message->payload_len] = '\0';

    uint8_t address[4];
    link_messaging_address_encode(message->source, address);
    char default_name[SOLAR_OS_CONTACT_NAME_MAX + 1U];
    snprintf(default_name, sizeof(default_name), "Link %08" PRIx32, message->source);
    solar_os_contact_id_t contact_id = SOLAR_OS_CONTACT_ID_NONE;
    solar_os_endpoint_id_t endpoint_id = SOLAR_OS_ENDPOINT_ID_NONE;
    esp_err_t error = solar_os_contacts_upsert_discovered(
        SOLAR_OS_MESSAGING_PROVIDER_LINK, address, sizeof(address), default_name,
        SOLAR_OS_ENDPOINT_CAP_DIRECT | SOLAR_OS_ENDPOINT_CAP_BROADCAST | SOLAR_OS_ENDPOINT_CAP_ACK,
        now_ms, link_messaging.link, strlen(link_messaging.link), &contact_id, &endpoint_id);
    if (error != ESP_OK) {
        return error;
    }

    solar_os_contact_t contact;
    solar_os_endpoint_t endpoint;
    if (solar_os_contacts_get(contact_id, &contact) != ESP_OK ||
        solar_os_contacts_get_endpoint(endpoint_id, &endpoint) != ESP_OK ||
        endpoint.trust == SOLAR_OS_CONTACT_TRUST_BLOCKED) {
        return ESP_ERR_INVALID_STATE;
    }

    char provider_key[SOLAR_OS_MESSAGING_PROVIDER_KEY_MAX];
    char title[SOLAR_OS_MESSAGING_TITLE_MAX];
    solar_os_conversation_kind_t kind;
    if (message->destination == SOLAR_OS_LINK_BROADCAST) {
        snprintf(provider_key, sizeof(provider_key), "b:%s", link_messaging.link);
        snprintf(title, sizeof(title), "%s broadcast", link_messaging.link);
        kind = SOLAR_OS_CONVERSATION_BROADCAST;
    } else {
        snprintf(provider_key, sizeof(provider_key), "direct:%" PRIu32, endpoint_id);
        strlcpy(title, contact.display_name, sizeof(title));
        kind = SOLAR_OS_CONVERSATION_DIRECT;
    }
    uint32_t security_flags = 0U;
    if (endpoint.trust == SOLAR_OS_CONTACT_TRUST_TRUSTED) {
        security_flags |= SOLAR_OS_SECURITY_PEER_TRUSTED;
    }
    const solar_os_messaging_inbound_t inbound = {
        .provider = SOLAR_OS_MESSAGING_PROVIDER_LINK,
        .conversation_key = provider_key,
        .conversation_kind = kind,
        .conversation_title = title,
        .contact_id = contact_id,
        .endpoint_id = endpoint_id,
        .group_ref = kind == SOLAR_OS_CONVERSATION_BROADCAST ? SOLAR_OS_LINK_BROADCAST : 0U,
        .provider_message_key =
            link_messaging_provider_key(message->source, message->sequence),
        .timestamp_ms = now_ms,
        .security_flags = security_flags,
        .sender = contact.display_name,
        .body = body,
    };
    return solar_os_messaging_publish_inbound(&inbound, NULL, NULL);
}

#else

bool solar_os_link_messaging_available(void) { return false; }

esp_err_t solar_os_link_messaging_start(const char *link)
{
    (void)link;
    return ESP_ERR_NOT_SUPPORTED;
}

void solar_os_link_messaging_stop(void) {}

void solar_os_link_messaging_process(uint32_t now_ms) { (void)now_ms; }

void solar_os_link_messaging_note_transmit(const solar_os_link_frame_t *frame, esp_err_t result,
                                           uint32_t now_ms)
{
    (void)frame;
    (void)result;
    (void)now_ms;
}

esp_err_t solar_os_link_messaging_note_ingest(const solar_os_link_ingest_result_t *result,
                                              uint32_t now_ms)
{
    (void)result;
    (void)now_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
