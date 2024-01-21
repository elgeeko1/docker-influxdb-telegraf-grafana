#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>

#define SERVER_PORT 8094
#define SERVER_IP "192.168.64.74"
#define BUFFER_SIZE 256 * 1024 * 1024 // 256MB

// Function to set a socket to non-blocking mode
//
// @param [in] sockfd The socket file descriptor
// @return success (0) or errno
int set_non_blocking(const int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("Error getting socket flags");
        return errno;
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Error setting socket to non-blocking");
        return errno;
    }
    return 0;
}

// Function to initialize a socket and connect to the server
//
// @return The socket file descriptor. -1 if error (check errno)
int init_socket(void) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create socket (SOCK_DGRAM for UDP)
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        return -1;
    }

    set_non_blocking(sockfd);

    return sockfd;
}

// Function to send data using non-blocking sockets
//
// This function sends data over a socket using non-blocking mode and monitors
// the socket's readiness for writing using `select()`.
//
// @param [in] sockfd The socket file descriptor
// @param [in] server_addr The server address
// @param [in] data Pointer to the data to be sent
// @param [in] data_size Size of the data to be sent in bytes
// @return success (0) or errno on failure
// @note Exits the program if an error occurs during data transmission.
int send_data(const int sockfd,
              const struct sockaddr_in * const server_addr,
              const char * const data,
              const size_t data_size)
{
    ssize_t sent;
    size_t total_sent = 0;
    fd_set write_fds;
    struct timeval tv;

    while (total_sent < data_size) {
        FD_ZERO(&write_fds);
        FD_SET(sockfd, &write_fds);
        tv.tv_sec = 5; // Timeout after 5 seconds
        tv.tv_usec = 0;

        // Wait until the socket is ready to send data
        if (select(sockfd + 1, NULL, &write_fds, NULL, &tv) > 0) {
            if (FD_ISSET(sockfd, &write_fds)) {
                sent = sendto(sockfd,
                              data + total_sent,
                              data_size - total_sent,
                              0,
                              (const struct sockaddr * const)server_addr,
                              sizeof(*server_addr) );

                if (sent < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // The socket is not ready for sending data, try again
                        // Sleep for 10 milliseconds
                        usleep(10000); // 10 milliseconds = 10000 microseconds
                        continue;
                    }
                    perror("Error sending data");
                    return -1;
                }

                total_sent += sent;
            }
        } else {
            // select() failed or timeout occurred
            perror("Error or timeout on select()");
            return -1;
        }
    }
    return 0;
}

// Formats a string following the InfluxDB line protocol.
//  @param [in] measurementName The name of the measurement.
//  @param [in] tagSet Comma-separated tag sets (e.g., "tagKey1=tagValue1,tagKey2=tagValue2").
//  @param [in] fieldSet Comma-separated field sets (e.g., "fieldKey1=fieldValue1,fieldKey2=fieldValue2").
//  @param [in] timestamp_ns Timestamp in nanoseconds.
//  @param [out] outStr Pointer to the output string buffer.
//  @param [in] maxLen The maximum length of the string buffer.
//  @return The length of the formatted string. -1 if the buffer is unsufficient for the formatted
//
// Example InfluxDB line: weather,location=us-midwest temperature=82 1465839830100400200
size_t formatInfluxDBLine(const char * const measurementName,
                       const char * const tagSet,
                       const char * const fieldSet,
                       const long long timestamp_ns,
                       char * const outStr,
                       const size_t maxLen) {
    size_t len = snprintf(outStr, maxLen, "%s,%s %s %lld", measurementName, tagSet, fieldSet, timestamp_ns);
    return len < maxLen ? len : -1; // Return -1 if the buffer is not large enough
}

// Retrieves the hostname of the system and prints it to a string.
// 
// @param [out] outStr Pointer to the output string buffer.
// @param [iin] maxLen The maximum length of the string buffer.
// @return success (0) or -1 on failure.
#define MAX_HOSTNAME_LEN 256
int getSystemHostname(char * const outStr, const size_t maxLen) {
    if (gethostname(outStr, maxLen) != 0) {
        perror("gethostname failed");
        return -1;
    }
    return 0;
}

// Handler function for SIGINT
// @param [in] sig The signal captured.
volatile int sigint_received = 0;
void sigint_handler(int sig) {
    sigint_received = 1;
}

#define TAGSET_MAX_LENGTH 512
#define INFLUXDB_MAX_LINE_LENGTH 2048
int main() {
    // socket
    int sockfd = -1;
    struct sockaddr_in server_addr;

    // influx line
    const char measurement[] = "series";
    char tagSet[TAGSET_MAX_LENGTH] = {"\0"};
    char fieldSet[TAGSET_MAX_LENGTH] = {"\0"};
    char hostname[MAX_HOSTNAME_LEN] = {"\0"};
    char influxdb_line[INFLUXDB_MAX_LINE_LENGTH] = {"\0"};

    // measurement to produce
    float value = 0;
    int value_count = 0;

    // timestamp
    struct timeval timestamp;
    long long timestamp_ns = 0;

    // capture SIGINT and exit politely.
    struct sigaction act;
    act.sa_handler = sigint_handler;  // Specify the handler function
    sigemptyset(&act.sa_mask);        // Initialize the signal set to empty
    act.sa_flags = 0;                 // No flags
    if (sigaction(SIGINT, &act, NULL) < 0)
        perror("sigaction");

    // construct tag set
    strncat(tagSet, "language=c", TAGSET_MAX_LENGTH-1);
    if(getSystemHostname(hostname, MAX_HOSTNAME_LEN) == 0){
        strncat(tagSet, ",hostname=", TAGSET_MAX_LENGTH-1);
        strncat(tagSet, hostname, TAGSET_MAX_LENGTH-1);
    }

    // Initialize socket and connect to server
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    sockfd = init_socket();
    if(sockfd == -1)
        return -1;

    // produce periodic values until interruped
    while(!sigint_received){
        // generate data
        value = fmod(value + 0.2, 10);
        value_count++;
        gettimeofday(&timestamp, NULL);
        // convert timeval into time in nanoseconds
        timestamp_ns = (long long)timestamp.tv_sec * 1000000000LL + (long long)timestamp.tv_usec * 1000LL;
        
        // construct field set
        snprintf(fieldSet, TAGSET_MAX_LENGTH, "value=%f", value);

        // construct InfluxDB line
        snprintf(influxdb_line,
                 INFLUXDB_MAX_LINE_LENGTH,
                 "%s,%s %s %lld",
                 measurement,
                 tagSet,
                 fieldSet,
                 timestamp_ns);

        send_data(sockfd, &server_addr, influxdb_line, strlen(influxdb_line));

        // Sleep for 100 milliseconds
        usleep(100000); // 100 milliseconds = 100000 microseconds
        // Print a debug message every 5 seconds
        // values are produced every 100 ms = 10Hz, so 50 values every 5 seconds
        if(value_count % 50 == 0){
            printf("C value publisher has published %d measurements.\n", value_count);
            fflush(stdout);
        }
    }

    // Close the socket
    close(sockfd);

    return 0;
}
