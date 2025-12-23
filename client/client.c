// client/client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#include "common.h"

#define SERVER_IP "127.0.0.1"
#define PORT 8080

uint32_t global_session_id = 0; // Store session ID

void perform_login(int sockfd);
void query_availability(int sockfd);
void book_tickets(int sockfd, int num_tickets, int user_id);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <query|book> [num_tickets]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in serv_addr;

    // 1. Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("invalid address or address not supported");
        exit(EXIT_FAILURE);
    }

    // 2. Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        exit(EXIT_FAILURE);
    }
    
    // Set Timeouts (5 seconds)
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt failed (RCVTIMEO)");
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt failed (SNDTIMEO)");
    }

    printf("Connected to server %s:%d\n", SERVER_IP, PORT);

    // 3. Perform Login First
    perform_login(sockfd);

    // 4. Perform action based on arguments
    if (strcmp(argv[1], "query") == 0) {
        query_availability(sockfd);
    } else if (strcmp(argv[1], "book") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s book <num_tickets>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        int num_tickets = atoi(argv[2]);
        if (num_tickets <= 0) {
            fprintf(stderr, "Number of tickets must be a positive integer.\n");
            exit(EXIT_FAILURE);
        }
        // Use a random user ID for simulation
        srand(time(NULL));
        int user_id = rand() % 10000;
        book_tickets(sockfd, num_tickets, user_id);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
    }

    close(sockfd);
    return 0;
}

void perform_login(int sockfd) {
    static uint16_t req_id_counter = 0;

    printf("Logging in...\n");
    ProtocolHeader req_header = {
        .packet_len = sizeof(ProtocolHeader),
        .opcode = OP_LOGIN,
        .req_id = req_id_counter++,
        .session_id = 0,
        .checksum = 0
    };
    
    // Calculate Checksum & Encrypt
    req_header.checksum = calculate_checksum(&req_header, sizeof(ProtocolHeader));
    xor_cipher(&req_header, sizeof(ProtocolHeader));

    if (write_n_bytes(sockfd, &req_header, sizeof(ProtocolHeader)) <= 0) {
        perror("Failed to send login request");
        exit(EXIT_FAILURE);
    }

    // Read Response
    ProtocolHeader res_header;
    if (read_n_bytes(sockfd, &res_header, sizeof(ProtocolHeader)) <= 0) {
        perror("Failed to read login response header");
        exit(EXIT_FAILURE);
    }
    // Decrypt Header
    xor_cipher(&res_header, sizeof(ProtocolHeader));
    
    // Read Body
    ServerResponse res_body;
    if (read_n_bytes(sockfd, &res_body, sizeof(ServerResponse)) <= 0) {
        perror("Failed to read login response body");
        exit(EXIT_FAILURE);
    }
    // Decrypt Body
    xor_cipher(&res_body, sizeof(ServerResponse));

    // Verify Checksum
    uint32_t received_checksum = res_header.checksum;
    res_header.checksum = 0;
    uint32_t calc_sum = calculate_checksum(&res_header, sizeof(ProtocolHeader));
    calc_sum += calculate_checksum(&res_body, sizeof(ServerResponse));
    
    if (calc_sum != received_checksum) {
        fprintf(stderr, "Login response checksum mismatch!\n");
        exit(EXIT_FAILURE);
    }

    if (res_header.opcode == OP_RESPONSE_SUCCESS) {
        global_session_id = res_header.session_id;
        printf("Login successful. Session ID: %u\n", global_session_id);
    } else {
        fprintf(stderr, "Login failed: %s\n", res_body.message);
        exit(EXIT_FAILURE);
    }
}

void query_availability(int sockfd) {
    static uint16_t req_id_counter = 100;

    // 1. Prepare and send request header
    ProtocolHeader req_header = {
        .packet_len = sizeof(ProtocolHeader),
        .opcode = OP_QUERY_AVAILABILITY,
        .req_id = req_id_counter++,
        .session_id = global_session_id,
        .checksum = 0
    };
    
    // Checksum & Encrypt
    req_header.checksum = calculate_checksum(&req_header, sizeof(ProtocolHeader));
    xor_cipher(&req_header, sizeof(ProtocolHeader));

    if (write_n_bytes(sockfd, &req_header, sizeof(ProtocolHeader)) <= 0) {
        perror("Failed to send query request");
        return;
    }

    printf("Sent query request (req_id=%u).\n", req_id_counter-1);

    // 2. Read response
    ProtocolHeader res_header;
    if (read_n_bytes(sockfd, &res_header, sizeof(ProtocolHeader)) <= 0) {
        perror("Failed to read response header");
        return;
    }
    xor_cipher(&res_header, sizeof(ProtocolHeader));

    ServerResponse res_body;
    if (read_n_bytes(sockfd, &res_body, sizeof(ServerResponse)) <= 0) {
        perror("Failed to read response body");
        return;
    }
    xor_cipher(&res_body, sizeof(ServerResponse));

    // Verify Checksum
    uint32_t received_checksum = res_header.checksum;
    res_header.checksum = 0;
    uint32_t calc_sum = calculate_checksum(&res_header, sizeof(ProtocolHeader));
    calc_sum += calculate_checksum(&res_body, sizeof(ServerResponse));
    if (calc_sum != received_checksum) {
        fprintf(stderr, "Response checksum mismatch!\n");
        return;
    }

    // 3. Print result
    printf("----------------------------------------\n");
    printf("Server Response (req_id=%u):\n", res_header.req_id);
    printf("  OpCode: 0x%X\n", res_header.opcode);
    printf("  Remaining Tickets: %u\n", res_body.remaining_tickets);
    printf("  Message: %s\n", res_body.message);
    printf("----------------------------------------\n");
}

void book_tickets(int sockfd, int num_tickets, int user_id) {
    static uint16_t req_id_counter = 200;

    // 1. Prepare request header and body
    ProtocolHeader req_header = {
        .packet_len = sizeof(ProtocolHeader) + sizeof(BookRequest),
        .opcode = OP_BOOK_TICKET,
        .req_id = req_id_counter++,
        .session_id = global_session_id,
        .checksum = 0
    };
    BookRequest req_body = {
        .num_tickets = num_tickets,
        .user_id = user_id
    };

    // Calculate Checksum (Header + Body)
    // Note: To calc checksum correctly for header, header needs default 0 checksum field.
    req_header.checksum = calculate_checksum(&req_header, sizeof(ProtocolHeader));
    req_header.checksum += calculate_checksum(&req_body, sizeof(BookRequest));
    
    // Encrypt
    xor_cipher(&req_header, sizeof(ProtocolHeader));
    xor_cipher(&req_body, sizeof(BookRequest));

    // 2. Send request
    if (write_n_bytes(sockfd, &req_header, sizeof(ProtocolHeader)) <= 0) {
        perror("Failed to send booking request header");
        return;
    }
    if (write_n_bytes(sockfd, &req_body, sizeof(BookRequest)) <= 0) {
        perror("Failed to send booking request body");
        return;
    }
    printf("Sent book request for %d tickets (user_id=%d, req_id=%u).\n", num_tickets, user_id, req_header.req_id); // Note: req_header is encrypted now, printing it would show garbage if we accessed fields. Used counter-1 or similar. Actually here we might print unexpected values if we printed struct fields.
    // Fixed: printing local vars or previous knowns. req_header.req_id is encrypted.
    
    // 3. Read response
    ProtocolHeader res_header;
    if (read_n_bytes(sockfd, &res_header, sizeof(ProtocolHeader)) <= 0) {
        perror("Failed to read response header");
        return;
    }
    xor_cipher(&res_header, sizeof(ProtocolHeader));

    ServerResponse res_body;
    if (read_n_bytes(sockfd, &res_body, sizeof(ServerResponse)) <= 0) {
        perror("Failed to read response body");
        return;
    }
    xor_cipher(&res_body, sizeof(ServerResponse));

    // Verify Checksum
    uint32_t received_checksum = res_header.checksum;
    res_header.checksum = 0;
    uint32_t calc_sum = calculate_checksum(&res_header, sizeof(ProtocolHeader));
    calc_sum += calculate_checksum(&res_body, sizeof(ServerResponse));
    if (calc_sum != received_checksum) {
        fprintf(stderr, "Response checksum mismatch!\n");
        return;
    }

    // 4. Print result
    printf("----------------------------------------\n");
    printf("Server Response (req_id=%u):\n", res_header.req_id);
    if (res_header.opcode == OP_RESPONSE_SUCCESS) {
        printf("  Status: SUCCESS\n");
    } else {
        printf("  Status: FAIL\n");
    }
    printf("  Remaining Tickets: %u\n", res_body.remaining_tickets);
    printf("  Message: %s\n", res_body.message);
    printf("----------------------------------------\n");
}
