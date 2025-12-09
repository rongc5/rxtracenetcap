#include "rxlockfreequeue.h"
#include <stdio.h>

/* Atomic operations using GCC builtins */
#define atomic_load_relaxed(ptr) __atomic_load_n(ptr, __ATOMIC_RELAXED)
#define atomic_store_relaxed(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELAXED)
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)

/* Round up to next power of 2 */
static uint32_t next_power_of_2(uint32_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

LockFreeQueue* lfq_create(uint32_t capacity) {
    if (capacity == 0) {
        return NULL;
    }

    LockFreeQueue* queue = (LockFreeQueue*)malloc(sizeof(LockFreeQueue));
    if (!queue) {
        return NULL;
    }

    /* Round up to power of 2 */
    uint32_t real_capacity = next_power_of_2(capacity);

    /* Allocate buffer */
    queue->buffer = (PacketNode*)malloc(sizeof(PacketNode) * real_capacity);
    if (!queue->buffer) {
        free(queue);
        return NULL;
    }

    /* Initialize */
    queue->capacity = real_capacity;
    queue->mask = real_capacity - 1;
    queue->write_pos = 0;
    queue->read_pos = 0;

    return queue;
}

void lfq_destroy(LockFreeQueue* queue) {
    if (queue) {
        if (queue->buffer) {
            free(queue->buffer);
        }
        free(queue);
    }
}

bool lfq_push(LockFreeQueue* queue, const PacketNode* item) {
    if (!queue || !item) {
        return false;
    }

    uint64_t write_pos = atomic_load_relaxed(&queue->write_pos);
    uint64_t read_pos = atomic_load_acquire(&queue->read_pos);

    /* Check if queue is full */
    if (write_pos - read_pos >= queue->capacity) {
        return false;
    }

    /* Copy item to buffer */
    uint32_t index = (uint32_t)(write_pos & queue->mask);
    PacketNode* slot = &queue->buffer[index];

    /* Copy packet data */
    memcpy(&slot->header, &item->header, sizeof(struct pcap_pkthdr));
    memcpy(slot->data, item->data, item->header.caplen);
    slot->src_port = item->src_port;
    slot->dst_port = item->dst_port;
    slot->app_len = item->app_len;
    slot->valid = item->valid;

    /* Adjust app_data pointer to point within the copied buffer */
    if (item->app_data && item->valid) {
        ptrdiff_t offset = item->app_data - item->data;
        slot->app_data = slot->data + offset;
    } else {
        slot->app_data = NULL;
    }

    /* Advance write position with release semantics */
    atomic_store_release(&queue->write_pos, write_pos + 1);

    return true;
}

bool lfq_pop(LockFreeQueue* queue, PacketNode* item) {
    if (!queue || !item) {
        return false;
    }

    uint64_t read_pos = atomic_load_relaxed(&queue->read_pos);
    uint64_t write_pos = atomic_load_acquire(&queue->write_pos);

    /* Check if queue is empty */
    if (read_pos >= write_pos) {
        return false;
    }

    /* Copy item from buffer */
    uint32_t index = (uint32_t)(read_pos & queue->mask);
    PacketNode* slot = &queue->buffer[index];

    /* Copy packet data */
    memcpy(&item->header, &slot->header, sizeof(struct pcap_pkthdr));
    memcpy(item->data, slot->data, slot->header.caplen);
    item->src_port = slot->src_port;
    item->dst_port = slot->dst_port;
    item->app_len = slot->app_len;
    item->valid = slot->valid;

    /* Adjust app_data pointer */
    if (slot->app_data && slot->valid) {
        ptrdiff_t offset = slot->app_data - slot->data;
        item->app_data = item->data + offset;
    } else {
        item->app_data = NULL;
    }

    /* Advance read position with release semantics */
    atomic_store_release(&queue->read_pos, read_pos + 1);

    return true;
}

uint32_t lfq_size(const LockFreeQueue* queue) {
    if (!queue) {
        return 0;
    }

    uint64_t write_pos = atomic_load_relaxed(&queue->write_pos);
    uint64_t read_pos = atomic_load_relaxed(&queue->read_pos);

    return (uint32_t)(write_pos - read_pos);
}

bool lfq_is_empty(const LockFreeQueue* queue) {
    return lfq_size(queue) == 0;
}

bool lfq_is_full(const LockFreeQueue* queue) {
    if (!queue) {
        return true;
    }

    uint64_t write_pos = atomic_load_relaxed(&queue->write_pos);
    uint64_t read_pos = atomic_load_relaxed(&queue->read_pos);

    return (write_pos - read_pos) >= queue->capacity;
}
