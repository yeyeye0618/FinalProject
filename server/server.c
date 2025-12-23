// server/server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h> // For potential future threading

#include "common.h"

#define PORT 8080
#define MAX_PENDING_CONNECTIONS 5

// Global state for simplicity
int total_tickets = 100;
pthread_mutex_t ticket_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_delay = 0; // Simulation delay in seconds

// Session Management
#define MAX_SESSIONS 100
uint32_t active_sessions[MAX_SESSIONS] = {0};
pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_session(uint32_t session_id) {
    pthread_mutex_lock(&session_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (active_sessions[i] == 0) {
            active_sessions[i] = session_id;
            break;
        }
    }
    pthread_mutex_unlock(&session_mutex);
}

int is_valid_session(uint32_t session_id) {
    if (session_id == 0) return 0;
    int found = 0;
    pthread_mutex_lock(&session_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (active_sessions[i] == session_id) {
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&session_mutex);
    return found;
}

void handle_connection(int client_socket);

int main() {
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // 1. Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Optional: Allow reusing address/port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // 2. Bind socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 3. Listen for connections
    if (listen(server_fd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // Check for Simulation Delay
    char *delay_env = getenv("SERVER_DELAY");
    if (delay_env) {
        server_delay = atoi(delay_env);
        printf("[TEST MODE] Server Response Delay set to %d seconds.\n", server_delay);
    }

    printf("Server listening on port %d\n", PORT);
    printf("Initial tickets: %d\n", total_tickets);

    // 4. Accept connections in a loop
    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) < 0) {
            perror("accept failed");
            continue; // Continue to next iteration
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Connection accepted from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // Set Timeout (10 seconds)
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
            perror("setsockopt failed (RCVTIMEO)");
        }

        // Handle the connection (for this simple server, we handle it sequentially)
        handle_connection(client_socket);
    }

    close(server_fd);
    return 0;
}

void handle_connection(int client_socket) {
    ProtocolHeader header;
    int read_ret;

    // Loop to handle multiple requests from the same client
    while ((read_ret = read_n_bytes(client_socket, &header, sizeof(ProtocolHeader))) > 0) {
        
        // 1. Decrypt Header
        xor_cipher(&header, sizeof(ProtocolHeader));

        // 2. Verify Checksum (Header part)
        // Note: For simplicity, we assume checksum was calculated on cleartext. 
        // We need to re-calculate it. 
        // But since we need the body to fully verify, we'll verify after reading body if needed.
        // Or we can assume Checksum covers Header+Body.
        
        // Let's read the body first if there is one
        void *body_buffer = NULL;
        int body_len = header.packet_len - sizeof(ProtocolHeader);
        if (body_len > 0) {
            body_buffer = malloc(body_len);
            if (read_n_bytes(client_socket, body_buffer, body_len) <= 0) {
                perror("Failed to read body");
                free(body_buffer);
                return;
            }
            // Decrypt Body
            xor_cipher(body_buffer, body_len);
        }

        // 3. Verify Checksum (Full Packet)
        uint32_t received_checksum = header.checksum;
        header.checksum = 0; // Zero out to calculate
        uint32_t calc_sum = calculate_checksum(&header, sizeof(ProtocolHeader));
        if (body_buffer) {
            calc_sum += calculate_checksum(body_buffer, body_len);
        }
        
        if (calc_sum != received_checksum) {
            printf("Checksum mismatch! Expected %u, got %u\n", received_checksum, calc_sum);
            if (body_buffer) free(body_buffer);
            close(client_socket);
            return;
        }
        // Restore checksum (optional, but good for debugging if we print it)
        header.checksum = received_checksum; 

        printf("Received request: packet_len=%u, opcode=0x%X, req_id=%u, session_id=%u\n",
               header.packet_len, header.opcode, header.req_id, header.session_id);

        ServerResponse response;
        memset(&response, 0, sizeof(ServerResponse)); // Clear response buffer
        int send_body = 1;

        // 4. Validate Session (unless Login)
        if (header.opcode != OP_LOGIN && !is_valid_session(header.session_id)) {
            printf("Invalid Session ID: %u\n", header.session_id);
            header.opcode = OP_RESPONSE_FAIL;
            strcpy(response.message, "Invalid Session ID. Please Login.");
            // Proceed to send response
        } else {
            switch (header.opcode) {
                case OP_LOGIN: {
                    // Generate new Session ID
                    uint32_t new_session_id = (rand() % 900000) + 100000;
                    add_session(new_session_id);
                    
                    // We reuse ServerResponse to send back session_id in remaining_tickets field? 
                    // Or define a new LoginResponse? 
                    // For simplicity, let's use the 'message' or 'remaining_tickets' field hacks, 
                    // or just assume the client looks at the `session_id` field in the RESPONSE HEADER?
                    // YES! The response header should carry the session_id back.
                    
                    header.session_id = new_session_id; // Set for response
                    header.opcode = OP_RESPONSE_SUCCESS;
                    strcpy(response.message, "Login Successful");
                    response.remaining_tickets = 0; 
                    break;
                }

                case OP_QUERY_AVAILABILITY: {
                    pthread_mutex_lock(&ticket_mutex);
                    response.remaining_tickets = total_tickets;
                    pthread_mutex_unlock(&ticket_mutex);

                    strcpy(response.message, "Query successful.");
                    header.opcode = OP_RESPONSE_SUCCESS;
                    break;
                }

                case OP_BOOK_TICKET: {
                    if (!body_buffer) {
                        header.opcode = OP_RESPONSE_FAIL;
                        strcpy(response.message, "Missing body.");
                        break;
                    }
                    BookRequest *req_body = (BookRequest *)body_buffer;
                    
                    pthread_mutex_lock(&ticket_mutex);
                    if (total_tickets >= req_body->num_tickets) {
                        total_tickets -= req_body->num_tickets;
                        response.remaining_tickets = total_tickets;
                        sprintf(response.message, "Booking successful for user %u.", req_body->user_id);
                        header.opcode = OP_RESPONSE_SUCCESS;
                    } else {
                        response.remaining_tickets = total_tickets;
                        sprintf(response.message, "Booking failed: not enough tickets.");
                        header.opcode = OP_RESPONSE_FAIL;
                    }
                    pthread_mutex_unlock(&ticket_mutex);
                    break;
                }

                default: {
                    printf("Unknown opcode: 0x%X\n", header.opcode);
                    header.opcode = OP_RESPONSE_FAIL;
                    strcpy(response.message, "Unknown operation.");
                    break;
                }
            }
        }

        if (body_buffer) free(body_buffer);

        // 5. Send Response
        header.packet_len = sizeof(ProtocolHeader) + sizeof(ServerResponse);
        header.checksum = 0;
        
        // Calculate Checksum for Response
        uint32_t res_sum = calculate_checksum(&header, sizeof(ProtocolHeader));
        res_sum += calculate_checksum(&response, sizeof(ServerResponse));
        header.checksum = res_sum;

        // Encrypt Response
        xor_cipher(&header, sizeof(ProtocolHeader));
        xor_cipher(&response, sizeof(ServerResponse));

        // Simulate Delay if configured
        if (server_delay > 0) {
            printf("[TEST] Sleeping for %d seconds before response...\n", server_delay);
            sleep(server_delay);
        }

        write_n_bytes(client_socket, &header, sizeof(ProtocolHeader));
        write_n_bytes(client_socket, &response, sizeof(ServerResponse));
    }

    if (read_ret == 0) {
        printf("Client disconnected.\n");
    } else {
        perror("read_n_bytes failed");
    }

    close(client_socket);
}
