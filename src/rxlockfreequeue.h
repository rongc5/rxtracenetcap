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










#define CACHE_LINE_SIZE 64


typedef struct {
    struct pcap_pkthdr header;
    uint8_t data[65536];
    uint16_t src_port;
    uint16_t dst_port;
    const uint8_t* app_data;
    uint32_t app_len;
    bool valid;
} PacketNode;


typedef struct {

    uint64_t write_pos __attribute__((aligned(CACHE_LINE_SIZE)));
    char pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];


    uint64_t read_pos __attribute__((aligned(CACHE_LINE_SIZE)));
    char pad2[CACHE_LINE_SIZE - sizeof(uint64_t)];


    uint32_t capacity;
    uint32_t mask;
    PacketNode* buffer;
} LockFreeQueue;






LockFreeQueue* lfq_create(uint32_t capacity);




void lfq_destroy(LockFreeQueue* queue);





bool lfq_push(LockFreeQueue* queue, const PacketNode* item);





bool lfq_pop(LockFreeQueue* queue, PacketNode* item);




uint32_t lfq_size(const LockFreeQueue* queue);




bool lfq_is_empty(const LockFreeQueue* queue);




bool lfq_is_full(const LockFreeQueue* queue);

#ifdef __cplusplus
}
#endif

#endif
