#include <iostream>
#include <csignal>
#include <chrono>

#include <stdio.h>
#include <time.h>
#include <math.h>

#include <curl/curl.h>

#include <vrt/vrt_string.h>
#include <vrt/vrt_types.h>
#include <vrt/vrt_write.h>

#include <zmq.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

static bool stop_signal_called = false;

void sig_int_handler(int)
{
    stop_signal_called = true;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string curlDownload(CURL* curl){

    CURLcode res;
    std::string readBuffer;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
    }
    return readBuffer;
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

    CURL* curl;
    curl = curl_easy_init();

    std::string readBuffer;
    std::string myLink = "https://api.astropeiler.de/25m";

    curl_easy_setopt(curl, CURLOPT_URL, myLink.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop." << std::endl;

    // Create ptree root
    pt::ptree root;

    auto start_time = std::chrono::steady_clock::now();

    // Track time between updates
    auto last_update = start_time;

    while (not stop_signal_called) {

        const auto now = std::chrono::steady_clock::now();

        const auto time_since_last_context = now - last_update;
        if (time_since_last_context > std::chrono::milliseconds(250)) {

            last_update = now;

            memset(data_buffer, 0, sizeof(data_buffer));

            pc.fields.integer_seconds_timestamp = (int)time(NULL); // use current time
            pc.fields.fractional_seconds_timestamp = 0; // tbd

            pc.body = data_buffer;
            pc.header.packet_count = packet_count;
            packet_count = (packet_count + 1) % 16;

            readBuffer = curlDownload(curl);
            // std::cout << readBuffer << std::endl;

            // Load the json string in this ptree
            std::istringstream is(readBuffer);
            pt::read_json(is, root);

            float azimuth = root.get<float>("AZ_ACT", 0);
            float elevation = root.get<float>("EL_ACT", 0);

            float azimuth_offset = root.get<float>("AZ_OFF", 0);
            float elevation_offset = root.get<float>("EL_OFF", 0);

            float ra = root.get<float>("RA_ACT", 0);
            float dec = root.get<float>("DEC_ACT", 0);

            float ra_tar = root.get<float>("RA_TAR", 0);
            float dec_tar = root.get<float>("DEC_TAR", 0);

            float jd = root.get<float>("JD", 0);
            float mjd = root.get<float>("MJD", 0);

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

            // Debug
            // printf("Unix: %d, ",(int)time(NULL));
            // printf("RA: %f, ", ra);
            // printf("JD: %.4f, ", (jd - 2440587.5)*86400.0 );            
            // printf("MJD: %f\n", (mjd - 2440587.5 + 2400000.5)*86400.0 );          
        }

    }

    curl_easy_cleanup(curl);
    std::cout << "Done." << std::endl;
    return 0;
}
