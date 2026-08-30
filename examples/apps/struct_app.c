#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

struct Packet {
    uint32_t magic;
    uint32_t packet_id;
    uint64_t size_bytes;
    const char *source_name;
};

__attribute__((noinline))
int process_packet(struct Packet *pkt)
{
    printf("[Packet Server] Processing packet #%u (%lu bytes) from %s\n",
           pkt->packet_id, pkt->size_bytes, pkt->source_name);
    return (int)(pkt->size_bytes * 2);
}

int main(void)
{
    printf("[Packet Server] Starting server loop...\n");
    for (uint32_t i = 1; i <= 4; i++) {
        struct Packet p = {
            .magic = 0xdeadbeef,
            .packet_id = i * 100,
            .size_bytes = i * 256,
            .source_name = "ClientNodeA"
        };
        int status = process_packet(&p);
        printf("[Packet Server] Packet #%u handled with status %d\n", p.packet_id, status);
        usleep(30000);
    }
    printf("[Packet Server] Done!\n");
    return 0;
}
