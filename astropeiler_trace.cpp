#include <iostream>
#include <csignal>
#include <chrono>

#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include <vrt/vrt_string.h>
#include <vrt/vrt_types.h>
#include <vrt/vrt_write.h>

#include <assert.h>
#include <zmq.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define VERBOSE 0

// VRT update time in ms
#define UPDATE_TIME 100

#define HOST "192.168.200.81"
#define PORTNUM 4000

#define BUFFER_SIZE 4096
#define LINE_BUFFER_SIZE 4096

int in_dict = 0;
bool end_of_dict = false;
char key[64];
double value;

float azimuth;
float elevation;
float azimuth_offset;
float elevation_offset;
float azimuth_error;
float elevation_error;
float azimuth_tar;
float elevation_tar;
float ra;
float dec;
float ra_tar;
float dec_tar;
double jd;

static bool stop_signal_called = false;

void sig_int_handler(int)
{
    stop_signal_called = true;
}

void set_variable(char* key, double value) {
    if (strcmp(key, "AZ_ACT")==0) {
        azimuth = value;
        if (VERBOSE)
            printf("Azimuth current: %lf\n", azimuth);
    } else if (strcmp(key, "EL_ACT")==0) {
        elevation = value;
        if (VERBOSE)
            printf("Elevation current: %lf\n", elevation);
    } else if (strcmp(key, "RA_ACT")==0) {
        ra = value;
        if (VERBOSE)
            printf("RA current: %lf\n", ra);
    } else if (strcmp(key, "DEC_ACT")==0) {
        dec = value;
        if (VERBOSE)
            printf("DEC current: %lf\n", dec);
    } else if (strcmp(key, "RA_TAR")==0) {
        ra_tar = value;
        if (VERBOSE)
            printf("RA target: %lf\n", ra_tar);
    } else if (strcmp(key, "DEC_TAR")==0) {
        dec_tar = value;
        if (VERBOSE)
            printf("DEC target: %lf\n", dec_tar);
    } if (strcmp(key, "AZ_OFFSET")==0) {
        azimuth_offset = value;
        if (VERBOSE)
            printf("Azimuth offset: %lf\n", azimuth_offset);
    } else if (strcmp(key, "EL_OFFSET")==0) {
        elevation_offset = value;
        if (VERBOSE)
            printf("Elevation offset: %lf\n", elevation_offset);
    } if (strcmp(key, "AZ_TAR")==0) {
        azimuth_tar = value;
        if (VERBOSE)
            printf("Azimuth target: %lf\n", azimuth_tar);
    } else if (strcmp(key, "EL_TAR")==0) {
        elevation_tar = value;
        if (VERBOSE)
            printf("Elevation target: %lf\n", elevation_tar);
    } else if (strcmp(key, "JD")==0) {
        jd = value;
        if (VERBOSE)
            printf("JD: %lf\n", jd);
    }
}

void parse_pickle(char* line) {

    if (strncmp(line, "(dp", 3) == 0) {
        in_dict = 1;
        return;
    }
    if (!in_dict) return;

    if (line[0] == 'S' || line[1] == 'S') {
        // String key: S'key'
        char* start = strchr(line, '\'') + 1;
        char* end = strchr(start, '\'');
        size_t len = end - start;
        if (len > 0 && len < LINE_BUFFER_SIZE) {
            strncpy(key, start, len);
            key[len] = '\0';
        }
    } else if (line[0] == 'F') {
        // Float value: F88.0
        sscanf(line + 1, "%lf", &value);
        // printf("Parsed key-value: %s = %lf\n", key, value);
        set_variable(key, value);
    } else if (line[0] == '.' || line[1] == '.') {
        // End of pickle
        // printf("end of pickle\n");
        in_dict = 0;
        end_of_dict = true;
        return;
    }   
}

void get_update(int sockfd) {

    const char *command = "???";

    // Send command
    if (send(sockfd, command, strlen(command), 0) < 0) {
        perror("Error sending");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    char line_buf[LINE_BUFFER_SIZE];

    int line_len = 0;
    in_dict = 0;
    end_of_dict = false;

    ssize_t bytes_received;
    while (!end_of_dict && (bytes_received = recv(sockfd, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[bytes_received] = '\0';  // Null-terminate
        // printf("%s", buffer);

        for (ssize_t i = 0; i < bytes_received; i++) {
            char c = buffer[i];
            if (line_len < LINE_BUFFER_SIZE - 1) {
                line_buf[line_len++] = c;
            }

            if (c == '\n') {
                line_buf[line_len] = '\0';  // Null-terminate the line
                // printf("Received line: %s", line_buf);
                line_len = 0;  // Reset line buffer
                parse_pickle(line_buf);
            }
        }

        if (line_len > 0) {
            // Handle trailing line with no newline
            line_buf[line_len] = '\0';
            // printf("Final partial line: %s\n", line_buf);
            parse_pickle(line_buf);
        }
    }
}


int main() {

    int rc;

    const int num_data_words = 32;  // 16 32-bit words for traces, 16 words for status;

    // One 32-bit word for header, one word for stream id, two words for class
    // id, three words for time
    const int num_header_words = 7;

    struct vrt_class_identifier vc;
    vc.oui = 0xFF0042;

    struct vrt_packet pc;
    pc.header.packet_type = VRT_PT_EXT_CONTEXT;
    pc.header.packet_size = num_data_words + num_header_words;
    pc.header.tsi = VRT_TSI_OTHER;  // unix time
    pc.header.tsf = VRT_TSF_REAL_TIME;
    pc.header.tsm = VRT_TSM_FINE;
    pc.fields.stream_id = 0;
    pc.header.has.trailer = false;
    pc.header.has.class_id = true;
    pc.fields.class_id = vc;
    pc.words_body = num_data_words;

    float data_buffer[num_data_words];

    // Setup up ZMQ
    int hwm = 10000;
    void *context = zmq_ctx_new();
    void *zmq_server = zmq_socket(context, ZMQ_PUB);
    rc = zmq_setsockopt(zmq_server, ZMQ_SNDHWM, &hwm, sizeof hwm);
    assert(rc == 0);
    rc = zmq_bind(zmq_server, "tcp://*:50011");
    assert(rc == 0);

    uint8_t packet_count = 0;

    const char *hostname = HOST;
    int port = PORTNUM;

    // Resolve hostname
    struct hostent *server = gethostbyname(hostname);
    if (!server) {
        fprintf(stderr, "Error: No such host %s\n", hostname);
        exit(EXIT_FAILURE);
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

     // Set up server address struct
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting");
        close(sockfd);
        exit(EXIT_FAILURE);
    } else {
        printf("Connected\n");
    }

    // Set timeout to 100ms
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 100000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop." << std::endl;

    auto start_time = std::chrono::steady_clock::now();

    // Track time between updates
    auto last_update = start_time;

    while (not stop_signal_called) {

        const auto now = std::chrono::steady_clock::now();

        const auto time_since_last_context = now - last_update;
        if (time_since_last_context > std::chrono::milliseconds(100)) {

            last_update = now;

            memset(data_buffer, 0, sizeof(data_buffer));

            get_update(sockfd);

            int unix_time_from_jd = round((jd - 2440587.5)*86400.0);

            // Debug time
            // printf("Unix: %d, ",(int)time(NULL));
            // printf("JD: %lf, ", (jd - 2440587.5)*86400.0 );            
            // printf("JD: %d\n", unix_time_from_jd );            

            if (VERBOSE)
                printf("Update time (local/api): %d/%d\n", (int)time(NULL), unix_time_from_jd);

            pc.fields.integer_seconds_timestamp = unix_time_from_jd;
            pc.fields.fractional_seconds_timestamp = 0; // tbd

            pc.body = data_buffer;
            pc.header.packet_count = packet_count;
            packet_count = (packet_count + 1) % 16;
           
            data_buffer[0] = M_PI*azimuth/180.0;
            data_buffer[1] = M_PI*elevation/180.0;

            data_buffer[8] = M_PI*azimuth_offset/180.0;
            data_buffer[9] = M_PI*elevation_offset/180.0;

            data_buffer[10] = M_PI*ra_tar/12.0;
            data_buffer[11] = M_PI*dec_tar/180.0;

            data_buffer[12] = M_PI*ra/12.0;
            data_buffer[13] = M_PI*dec/180.0;

            zmq_msg_t msg;
            // Header, coarse time, fine time, N traces
            rc = zmq_msg_init_size(&msg, num_data_words * sizeof(uint32_t) +
                                   num_header_words * sizeof(uint32_t));
            assert(rc == 0);
            int32_t rv = vrt_write_packet(&pc, zmq_msg_data(&msg),
                                num_data_words + num_header_words, true);
            if (rv < 0) {
                fprintf(stderr, "Failed to write packet: %s\n", vrt_string_error(rv));
                return EXIT_FAILURE;
            }
            zmq_msg_send(&msg, zmq_server, 0);
            zmq_msg_close(&msg);

        }

        usleep(1000);

    }

    close(sockfd);
    std::cout << "Done." << std::endl;
    return 0;
}
