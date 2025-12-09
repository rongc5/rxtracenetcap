#ifndef RX_LOCKFREE_QUEUE_H
#define RX_LOCKFREE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pcap/pcap.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lock-free single-producer single-consumer (SPSC) ring buffer queue
 *
 * Features:
 * - Wait-free for both producer and consumer
 * - Cache-line aligned to avoid false sharing
 * - Power-of-2 size for fast modulo operations
 */

#define CACHE_LINE_SIZE 64

/* Packet data node */
typedef struct {
    struct pcap_pkthdr header;      /* pcap packet header */
    uint8_t data[65536];            /* packet data (max jumbo frame) */
    uint16_t src_port;              /* pre-parsed source port */
    uint16_t dst_port;              /* pre-parsed destination port */
    const uint8_t* app_data;        /* pointer to application layer data within 'data' */
    uint32_t app_len;               /* application layer data length */
    bool valid;                     /* whether packet is valid */
} PacketNode;

/* Lock-free queue structure */
typedef struct {
    /* Producer-only fields (cache line 0) */
    uint64_t write_pos __attribute__((aligned(CACHE_LINE_SIZE)));
    char pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];

    /* Consumer-only fields (cache line 1) */
    uint64_t read_pos __attribute__((aligned(CACHE_LINE_SIZE)));
    char pad2[CACHE_LINE_SIZE - sizeof(uint64_t)];

    /* Shared read-only fields (cache line 2) */
    uint32_t capacity;              /* must be power of 2 */
    uint32_t mask;                  /* capacity - 1, for fast modulo */
    PacketNode* buffer;             /* ring buffer */
} LockFreeQueue;

/**
 * Create a lock-free queue
 * @param capacity Queue capacity (will be rounded up to power of 2)
 * @return Queue pointer, or NULL on failure
 */
LockFreeQueue* lfq_create(uint32_t capacity);

/**
 * Destroy a lock-free queue
 */
void lfq_destroy(LockFreeQueue* queue);

/**
 * Push an item to the queue (producer side)
 * @return true if successful, false if queue is full
 */
bool lfq_push(LockFreeQueue* queue, const PacketNode* item);

/**
 * Pop an item from the queue (consumer side)
 * @return true if successful, false if queue is empty
 */
bool lfq_pop(LockFreeQueue* queue, PacketNode* item);

/**
 * Get current queue size (approximate, for monitoring only)
 */
uint32_t lfq_size(const LockFreeQueue* queue);

/**
 * Check if queue is empty
 */
bool lfq_is_empty(const LockFreeQueue* queue);

/**
 * Check if queue is full
 */
bool lfq_is_full(const LockFreeQueue* queue);

#ifdef __cplusplus
}
#endif

#endif /* RX_LOCKFREE_QUEUE_H */
