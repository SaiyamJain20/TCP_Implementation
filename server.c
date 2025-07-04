#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h>

#define BUFFER_SIZE 1024
#define CHUNK_SIZE 8
#define MAX_CHUNKS BUFFER_SIZE / CHUNK_SIZE + 1
#define TIMEOUT_SEC 0
#define TIMEOUT_NSEC 100000
#define DONT_SEND_ACK 2

struct Chunk
{
    int sequence_number;
    char data[CHUNK_SIZE + 1];
    int total_chunks;
    struct timeval sent_time;
};

void make_non_blocking(int sockfd)
{
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int portNo = atoi(argv[1]);

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Error opening socket");
        exit(1);
    }

    make_non_blocking(sock);

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(portNo);
    servaddr.sin_addr.s_addr = INADDR_ANY;
    char buffer[BUFFER_SIZE];
    struct timeval now;
    struct sockaddr_in clientaddr;
    socklen_t clientAddrlen = sizeof(clientaddr);

    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind failed");
        exit(1);
    }

    while (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&clientaddr, &clientAddrlen) < 0)
        ;

    while (1)
    {
        printf("Waiting...\n");
        fflush(stdout);
        long long int j = 0;
        struct Chunk recieved_chunk[MAX_CHUNKS];
        memset(recieved_chunk, 0, sizeof(recieved_chunk));
        int received_flags[MAX_CHUNKS];
        memset(received_flags, 0, sizeof(received_flags));

        char req[BUFFER_SIZE];
        while (recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&clientaddr, &clientAddrlen) < 0)
            ;
        if (strncmp(req, "exit", 4) == 0)
        {
            printf("Quitting\n");
            fflush(stdout);
            break;
        }
        while (1)
        {
            struct Chunk chunk;
            while (recvfrom(sock, &chunk, sizeof(struct Chunk), 0, (struct sockaddr *)&clientaddr, &clientAddrlen) < 0)
                ;
            j++;
            if(j % DONT_SEND_ACK == 0) continue;

            recieved_chunk[chunk.sequence_number] = chunk;
            received_flags[chunk.sequence_number] = 1;

            int ack_num = chunk.sequence_number;
            sendto(sock, &ack_num, sizeof(int), 0, (struct sockaddr *)&clientaddr, clientAddrlen);

            int left = chunk.total_chunks;
            for (int i = 0; i < chunk.total_chunks; i++)
            {
                if (received_flags[i])
                {
                    left--;
                }
            }

            if (left <= 0)
            {
                char full_message[BUFFER_SIZE];
                memset(full_message, 0, sizeof(full_message));
                for (int i = 0; i < chunk.total_chunks; i++)
                {
                    strcat(full_message, recieved_chunk[i].data);
                }
                printf("Received response: %s\n", full_message);
                fflush(stdout);
                break;
            }

            usleep(1000);
        }

        printf("Enter message (or 'exit' to exit): ");
        fflush(stdout);
        char in[BUFFER_SIZE];
        fgets(in, BUFFER_SIZE, stdin);
        fflush(stdin);
        int sequence_number = 0;
        int ack_number;

        if (strncmp(in, "exit", 4) == 0 && strlen(in) == 5)
        {
            sendto(sock, "exit", strlen("exit"), 0, (struct sockaddr *)&clientaddr, clientAddrlen);
            printf("Quitting\n");
            fflush(stdout);
            break;
        }
        else
        {
            sendto(sock, "continue", strlen("continue"), 0, (struct sockaddr *)&clientaddr, clientAddrlen);
        }
        int total_chunks = (strlen(in) + CHUNK_SIZE - 1) / CHUNK_SIZE;
        int sent_chunks = 0, cnt = 0;

        struct Chunk chunks[MAX_CHUNKS];
        for (int i = 0; i < total_chunks; i++)
        {
            chunks[i].sequence_number = i;
            strncpy(chunks[i].data, in + i * CHUNK_SIZE, CHUNK_SIZE);
            chunks[i].data[CHUNK_SIZE] = '\0';
            chunks[i].sent_time = (struct timeval){0, 0};
            chunks[i].total_chunks = total_chunks;
        }

        int acknowledged_chunks[MAX_CHUNKS];
        memset(acknowledged_chunks, 0, sizeof(acknowledged_chunks));
        int queue[MAX_CHUNKS + 10];
        int start = 0, end = -1;
        int sent = 0;

        for (int i = 0; i < total_chunks; i++)
        {
            gettimeofday(&now, NULL);
            sendto(sock, &chunks[i], sizeof(struct Chunk), 0, (struct sockaddr *)&clientaddr, clientAddrlen);
            printf("Sent chunk %d, data: %s\n", chunks[i].sequence_number, chunks[i].data);
            fflush(stdout);
            chunks[i].sent_time = now;
            end = (end + 1) % (MAX_CHUNKS + 10);
            queue[end] = i;
            sent++;
            cnt++;

            int ack_num;
            int n = recvfrom(sock, &ack_num, sizeof(int), MSG_DONTWAIT, (struct sockaddr *)&clientaddr, &clientAddrlen);
            if (n > 0)
            {
                printf("Received ACK for chunk %d\n", ack_num);
                fflush(stdout);
                acknowledged_chunks[ack_num] = 1;
                sent_chunks++;
            }

            while (cnt != 0 && acknowledged_chunks[queue[start]])
            {
                start = (start + 1) % (MAX_CHUNKS + 10);
                cnt--;
            }

            while (cnt != 0 && !acknowledged_chunks[queue[start]] && chunks[queue[start]].sent_time.tv_sec != 0 && (now.tv_sec - chunks[queue[start]].sent_time.tv_sec) * 1000000 + (now.tv_usec - chunks[queue[start]].sent_time.tv_usec) > TIMEOUT_NSEC)
            {
                gettimeofday(&now, NULL);
                sendto(sock, &chunks[queue[start]], sizeof(struct Chunk), 0, (struct sockaddr *)&clientaddr, clientAddrlen);
                printf("Resent chunk %d, data: %s\n", chunks[queue[start]].sequence_number, chunks[queue[start]].data);
                fflush(stdout);
                chunks[queue[start]].sent_time = now;
                end = (end + 1) % (MAX_CHUNKS + 10);
                queue[end] = queue[start];
                start = (start + 1) % (MAX_CHUNKS + 10);
                sent++;
            }
        }

        while (sent_chunks < total_chunks)
        {
            int ack_num;
            int n = recvfrom(sock, &ack_num, sizeof(int), MSG_DONTWAIT, (struct sockaddr *)&clientaddr, &clientAddrlen);
            if (n > 0)
            {
                printf("Received ACK for chunk %d\n", ack_num);
                fflush(stdout);
                acknowledged_chunks[ack_num] = 1;
                sent_chunks++;
            }

            while (cnt != 0 && acknowledged_chunks[queue[start]])
            {
                start = (start + 1) % (MAX_CHUNKS + 10);
                cnt--;
            }

            gettimeofday(&now, NULL);
            while (cnt != 0  && !acknowledged_chunks[queue[start]] && (now.tv_sec - chunks[queue[start]].sent_time.tv_sec) * 1000000 + (now.tv_usec - chunks[queue[start]].sent_time.tv_usec) > TIMEOUT_NSEC)
            {
                sendto(sock, &chunks[queue[start]], sizeof(struct Chunk), 0, (struct sockaddr *)&clientaddr, clientAddrlen);
                printf("Resent chunk %d, data: %s\n", chunks[queue[start]].sequence_number, chunks[queue[start]].data);
                fflush(stdout);
                chunks[queue[start]].sent_time = now;
                end = (end + 1) % (MAX_CHUNKS + 10);
                queue[end] = queue[start];
                start = (start + 1) % (MAX_CHUNKS + 10);
                sent++;
            }

            usleep(1000);
        }
    }

    close(sock);
    return 0;
}
