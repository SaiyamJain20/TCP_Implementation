# TCP-like Protocol Implementation

This project implements a reliable transport protocol over UDP that simulates key TCP features such as packet chunking, acknowledgments, and retransmission.

## Overview

The implementation consists of two main components:
- `client.c`: Client application that connects to a server
- `server.c`: Server application that listens for client connections

Both components use a custom packet structure and reliability mechanism to ensure data delivery even over an unreliable transport layer (UDP).

## Features

- Packet chunking: Splits messages into fixed-size chunks (8 bytes per chunk)
- Acknowledgment system: Receivers confirm receipt of each chunk
- Retransmission: Automatically retransmits unacknowledged packets after timeout
- Sequence numbering: Ensures correct packet ordering
- Simulated packet loss: Can deliberately drop packets to test reliability

## Protocol Details

### Chunk Structure

```
struct Chunk
{
    int sequence_number;    // Unique identifier for each chunk
    char data[CHUNK_SIZE + 1]; // Data payload (8 bytes + null terminator)
    int total_chunks;       // Total number of chunks in the message
    struct timeval sent_time; // Timestamp for retransmission calculation
};
```

### Reliability Mechanism

- Selective repeat ARQ for efficient retransmission
- Timeout-based retransmission (100,000 nanoseconds)
- Simulated packet loss through DONT_SEND_ACK constant

## Usage

### Server
```
./server <port>
```

### Client
```
./client <hostname> <port>
```

### Compiling
Compile both applications using gcc:

```
gcc -o server server.c
gcc -o client client.c
```

## Communication Flow
1. Client sends initial connection request
2. Users can type messages on either side
3. Messages are broken into chunks and sent
4. Receiver acknowledges each chunk
5. Unacknowledged chunks are retransmitted
6. Complete message is reassembled at receiving end
7. Either side can end the session by typing "exit"

## Implementation Notes
The protocol uses a queue-based system for tracking unacknowledged packets and a circular buffer implementation to efficiently manage retransmissions.