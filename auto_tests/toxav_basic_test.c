#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "check_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <vpx/vpx_image.h>

#include "../toxav/toxav.h"
#include "../toxcore/crypto_core.h"
#include "../toxcore/logger.h"
#include "../toxcore/tox.h"
#include "../toxcore/util.h"

#include "helpers.h"

#define TEST_REGULAR_AV 1
#define TEST_REGULAR_A 1
#define TEST_REGULAR_V 1
#define TEST_REJECT 1
#define TEST_CANCEL 1
#define TEST_MUTE_UNMUTE 1
#define TEST_STOP_RESUME_PAYLOAD 1
#define TEST_PAUSE_RESUME_SEND 1


#define ck_assert_call_control(a, b, c) do { \
    TOXAV_ERR_CALL_CONTROL cc_err; \
    bool ok = toxav_call_control(a, b, c, &cc_err); \
    if (!ok) { \
        printf("toxav_call_control returned error %d\n", cc_err); \
    } \
    ck_assert(ok); \
    ck_assert(cc_err == TOXAV_ERR_CALL_CONTROL_OK); \
} while (0)


typedef struct {
    bool incoming;
    uint32_t state;
} CallControl;


/**
 * Callbacks
 */
static void t_toxav_call_cb(ToxAV *av, uint32_t friend_number, bool audio_enabled, bool video_enabled, void *user_data)
{
    printf("Handling CALL callback\n");
    ((CallControl *)user_data)->incoming = true;
}

static void t_toxav_call_state_cb(ToxAV *av, uint32_t friend_number, uint32_t state, void *user_data)
{
    printf("Handling CALL STATE callback: %d\n", state);
    ((CallControl *)user_data)->state = state;
}

static void t_toxav_receive_video_frame_cb(ToxAV *av, uint32_t friend_number,
        uint16_t width, uint16_t height,
        uint8_t const *y, uint8_t const *u, uint8_t const *v,
        int32_t ystride, int32_t ustride, int32_t vstride,
        void *user_data)
{
    printf("Received video payload\n");
}

static void t_toxav_receive_audio_frame_cb(ToxAV *av, uint32_t friend_number,
        int16_t const *pcm,
        size_t sample_count,
        uint8_t channels,
        uint32_t sampling_rate,
        void *user_data)
{
    printf("Received audio payload\n");
}

static void t_accept_friend_request_cb(Tox *m, const uint8_t *public_key, const uint8_t *data, size_t length,
                                       void *userdata)
{
    if (length == 7 && memcmp("gentoo", data, 7) == 0) {
        ck_assert(tox_friend_add_norequest(m, public_key, nullptr) != (uint32_t) ~0);
    }
}


/**
 * Iterate helper
 */
static void iterate_tox(Tox *bootstrap, Tox *Alice, Tox *Bob)
{
    c_sleep(100);
    tox_iterate(bootstrap, nullptr);
    tox_iterate(Alice, nullptr);
    tox_iterate(Bob, nullptr);
}

static void test_av_flows(void)
{
    Tox *Alice, *Bob, *bootstrap;
    ToxAV *AliceAV, *BobAV;
    uint32_t index[] = { 1, 2, 3 };

    CallControl AliceCC, BobCC;

    {
        TOX_ERR_NEW error;

        bootstrap = tox_new_log(nullptr, &error, &index[0]);
        ck_assert(error == TOX_ERR_NEW_OK);

        Alice = tox_new_log(nullptr, &error, &index[1]);
        ck_assert(error == TOX_ERR_NEW_OK);

        Bob = tox_new_log(nullptr, &error, &index[2]);
        ck_assert(error == TOX_ERR_NEW_OK);
    }

    printf("Created 3 instances of Tox\n");
    printf("Preparing network...\n");
    long long unsigned int cur_time = time(nullptr);

    uint8_t address[TOX_ADDRESS_SIZE];

    tox_callback_friend_request(Alice, t_accept_friend_request_cb);
    tox_self_get_address(Alice, address);

    printf("bootstrapping Alice and Bob off a third bootstrap node\n");
    uint8_t dht_key[TOX_PUBLIC_KEY_SIZE];
    tox_self_get_dht_id(bootstrap, dht_key);
    const uint16_t dht_port = tox_self_get_udp_port(bootstrap, nullptr);

    tox_bootstrap(Alice, "localhost", dht_port, dht_key, nullptr);
    tox_bootstrap(Bob, "localhost", dht_port, dht_key, nullptr);

    ck_assert(tox_friend_add(Bob, address, (const uint8_t *)"gentoo", 7, nullptr) != (uint32_t) ~0);

    uint8_t off = 1;

    while (1) {
        iterate_tox(bootstrap, Alice, Bob);

        if (tox_self_get_connection_status(bootstrap) &&
                tox_self_get_connection_status(Alice) &&
                tox_self_get_connection_status(Bob) && off) {
            printf("Toxes are online, took %llu seconds\n", time(nullptr) - cur_time);
            off = 0;
        }

        if (tox_friend_get_connection_status(Alice, 0, nullptr) == TOX_CONNECTION_UDP &&
                tox_friend_get_connection_status(Bob, 0, nullptr) == TOX_CONNECTION_UDP) {
            break;
        }

        c_sleep(20);
    }


    {
        TOXAV_ERR_NEW error;
        AliceAV = toxav_new(Alice, &error);
        ck_assert(error == TOXAV_ERR_NEW_OK);

        BobAV = toxav_new(Bob, &error);
        ck_assert(error == TOXAV_ERR_NEW_OK);
    }

    toxav_callback_call(AliceAV, t_toxav_call_cb, &AliceCC);
    toxav_callback_call_state(AliceAV, t_toxav_call_state_cb, &AliceCC);
    toxav_callback_video_receive_frame(AliceAV, t_toxav_receive_video_frame_cb, &AliceCC);
    toxav_callback_audio_receive_frame(AliceAV, t_toxav_receive_audio_frame_cb, &AliceCC);

    toxav_callback_call(BobAV, t_toxav_call_cb, &BobCC);
    toxav_callback_call_state(BobAV, t_toxav_call_state_cb, &BobCC);
    toxav_callback_video_receive_frame(BobAV, t_toxav_receive_video_frame_cb, &BobCC);
    toxav_callback_audio_receive_frame(BobAV, t_toxav_receive_audio_frame_cb, &BobCC);

    printf("Created 2 instances of ToxAV\n");
    printf("All set after %llu seconds!\n", time(nullptr) - cur_time);


#define REGULAR_CALL_FLOW(A_BR, V_BR) \
    do { \
        memset(&AliceCC, 0, sizeof(CallControl)); \
        memset(&BobCC, 0, sizeof(CallControl)); \
        \
        TOXAV_ERR_CALL call_err; \
        toxav_call(AliceAV, 0, A_BR, V_BR, &call_err); \
        \
        if (call_err != TOXAV_ERR_CALL_OK) { \
            printf("toxav_call failed: %d\n", call_err); \
            ck_assert(0); \
        } \
        \
        \
        long long unsigned int start_time = time(nullptr); \
        \
        \
        while (BobCC.state != TOXAV_FRIEND_CALL_STATE_FINISHED) { \
            \
            if (BobCC.incoming) { \
                TOXAV_ERR_ANSWER answer_err; \
                toxav_answer(BobAV, 0, A_BR, V_BR, &answer_err); \
                \
                if (answer_err != TOXAV_ERR_ANSWER_OK) { \
                    printf("toxav_answer failed: %d\n", answer_err); \
                    ck_assert(0); \
                } \
                BobCC.incoming = false; \
            } else { \
                /* TODO(mannol): rtp */ \
                \
                if (time(nullptr) - start_time >= 1) { \
                    \
                    TOXAV_ERR_CALL_CONTROL cc_err; \
                    toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_CANCEL, &cc_err); \
                    \
                    if (cc_err != TOXAV_ERR_CALL_CONTROL_OK) { \
                        printf("toxav_call_control failed: %d\n", cc_err); \
                        ck_assert(0); \
                    } \
                } \
            } \
             \
            iterate_tox(bootstrap, Alice, Bob); \
        } \
        printf("Success!\n");\
    } while(0)

    if (TEST_REGULAR_AV) {
        printf("\nTrying regular call (Audio and Video)...\n");
        REGULAR_CALL_FLOW(48, 4000);
    }

    if (TEST_REGULAR_A) {
        printf("\nTrying regular call (Audio only)...\n");
        REGULAR_CALL_FLOW(48, 0);
    }

    if (TEST_REGULAR_V) {
        printf("\nTrying regular call (Video only)...\n");
        REGULAR_CALL_FLOW(0, 4000);
    }

#undef REGULAR_CALL_FLOW

    if (TEST_REJECT) { /* Alice calls; Bob rejects */
        printf("\nTrying reject flow...\n");

        memset(&AliceCC, 0, sizeof(CallControl));
        memset(&BobCC, 0, sizeof(CallControl));

        {
            TOXAV_ERR_CALL rc;
            toxav_call(AliceAV, 0, 48, 0, &rc);

            if (rc != TOXAV_ERR_CALL_OK) {
                printf("toxav_call failed: %d\n", rc);
                ck_assert(0);
            }
        }

        while (!BobCC.incoming) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        /* Reject */
        {
            TOXAV_ERR_CALL_CONTROL rc;
            toxav_call_control(BobAV, 0, TOXAV_CALL_CONTROL_CANCEL, &rc);

            if (rc != TOXAV_ERR_CALL_CONTROL_OK) {
                printf("toxav_call_control failed: %d\n", rc);
                ck_assert(0);
            }
        }

        while (AliceCC.state != TOXAV_FRIEND_CALL_STATE_FINISHED) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        printf("Success!\n");
    }

    if (TEST_CANCEL) { /* Alice calls; Alice cancels while ringing */
        printf("\nTrying cancel (while ringing) flow...\n");

        memset(&AliceCC, 0, sizeof(CallControl));
        memset(&BobCC, 0, sizeof(CallControl));

        {
            TOXAV_ERR_CALL rc;
            toxav_call(AliceAV, 0, 48, 0, &rc);

            if (rc != TOXAV_ERR_CALL_OK) {
                printf("toxav_call failed: %d\n", rc);
                ck_assert(0);
            }
        }

        while (!BobCC.incoming) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        /* Cancel */
        {
            TOXAV_ERR_CALL_CONTROL rc;
            toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_CANCEL, &rc);

            if (rc != TOXAV_ERR_CALL_CONTROL_OK) {
                printf("toxav_call_control failed: %d\n", rc);
                ck_assert(0);
            }
        }

        /* Alice will not receive end state */
        while (BobCC.state != TOXAV_FRIEND_CALL_STATE_FINISHED) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        printf("Success!\n");
    }

    if (TEST_MUTE_UNMUTE) { /* Check Mute-Unmute etc */
        printf("\nTrying mute functionality...\n");

        memset(&AliceCC, 0, sizeof(CallControl));
        memset(&BobCC, 0, sizeof(CallControl));

        /* Assume sending audio and video */
        {
            TOXAV_ERR_CALL rc;
            toxav_call(AliceAV, 0, 48, 1000, &rc);

            if (rc != TOXAV_ERR_CALL_OK) {
                printf("toxav_call failed: %d\n", rc);
                ck_assert(0);
            }
        }

        while (!BobCC.incoming) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        /* At first try all stuff while in invalid state */
        ck_assert(!toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_PAUSE, nullptr));
        ck_assert(!toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_RESUME, nullptr));
        ck_assert(!toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_MUTE_AUDIO, nullptr));
        ck_assert(!toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_UNMUTE_AUDIO, nullptr));
        ck_assert(!toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_HIDE_VIDEO, nullptr));
        ck_assert(!toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_SHOW_VIDEO, nullptr));

        {
            TOXAV_ERR_ANSWER rc;
            toxav_answer(BobAV, 0, 48, 4000, &rc);

            if (rc != TOXAV_ERR_ANSWER_OK) {
                printf("toxav_answer failed: %d\n", rc);
                ck_assert(0);
            }
        }

        iterate_tox(bootstrap, Alice, Bob);

        /* Pause and Resume */
        printf("Pause and Resume\n");
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_PAUSE);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state == 0);
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_RESUME);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state & (TOXAV_FRIEND_CALL_STATE_SENDING_A | TOXAV_FRIEND_CALL_STATE_SENDING_V));

        /* Mute/Unmute single */
        printf("Mute/Unmute single\n");
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_MUTE_AUDIO);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state ^ TOXAV_FRIEND_CALL_STATE_ACCEPTING_A);
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_UNMUTE_AUDIO);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A);

        /* Mute/Unmute both */
        printf("Mute/Unmute both\n");
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_MUTE_AUDIO);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state ^ TOXAV_FRIEND_CALL_STATE_ACCEPTING_A);
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_HIDE_VIDEO);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state ^ TOXAV_FRIEND_CALL_STATE_ACCEPTING_V);
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_UNMUTE_AUDIO);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A);
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_SHOW_VIDEO);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V);

        {
            TOXAV_ERR_CALL_CONTROL rc;
            toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_CANCEL, &rc);

            if (rc != TOXAV_ERR_CALL_CONTROL_OK) {
                printf("toxav_call_control failed: %d\n", rc);
                ck_assert(0);
            }
        }

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state == TOXAV_FRIEND_CALL_STATE_FINISHED);

        printf("Success!\n");
    }

    if (TEST_STOP_RESUME_PAYLOAD) { /* Stop and resume audio/video payload */
        printf("\nTrying stop/resume functionality...\n");

        memset(&AliceCC, 0, sizeof(CallControl));
        memset(&BobCC, 0, sizeof(CallControl));

        /* Assume sending audio and video */
        {
            TOXAV_ERR_CALL rc;
            toxav_call(AliceAV, 0, 48, 0, &rc);

            if (rc != TOXAV_ERR_CALL_OK) {
                printf("toxav_call failed: %d\n", rc);
                ck_assert(0);
            }
        }

        while (!BobCC.incoming) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        {
            TOXAV_ERR_ANSWER rc;
            toxav_answer(BobAV, 0, 48, 0, &rc);

            if (rc != TOXAV_ERR_ANSWER_OK) {
                printf("toxav_answer failed: %d\n", rc);
                ck_assert(0);
            }
        }

        iterate_tox(bootstrap, Alice, Bob);

        printf("Call started as audio only\n");
        printf("Turning on video for Alice...\n");
        ck_assert(toxav_video_set_bit_rate(AliceAV, 0, 1000, nullptr));

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state & TOXAV_FRIEND_CALL_STATE_SENDING_V);

        printf("Turning off video for Alice...\n");
        ck_assert(toxav_video_set_bit_rate(AliceAV, 0, 0, nullptr));

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(!(BobCC.state & TOXAV_FRIEND_CALL_STATE_SENDING_V));

        printf("Turning off audio for Alice...\n");
        ck_assert(toxav_audio_set_bit_rate(AliceAV, 0, 0, nullptr));

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(!(BobCC.state & TOXAV_FRIEND_CALL_STATE_SENDING_A));

        {
            TOXAV_ERR_CALL_CONTROL rc;
            toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_CANCEL, &rc);

            if (rc != TOXAV_ERR_CALL_CONTROL_OK) {
                printf("toxav_call_control failed: %d\n", rc);
                ck_assert(0);
            }
        }

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state == TOXAV_FRIEND_CALL_STATE_FINISHED);

        printf("Success!\n");
    }

    if (TEST_PAUSE_RESUME_SEND) { /* Stop and resume audio/video payload and test send options */
        printf("\nTrying stop/resume functionality...\n");

        memset(&AliceCC, 0, sizeof(CallControl));
        memset(&BobCC, 0, sizeof(CallControl));

        /* Assume sending audio and video */
        {
            TOXAV_ERR_CALL rc;
            toxav_call(AliceAV, 0, 48, 0, &rc);

            if (rc != TOXAV_ERR_CALL_OK) {
                printf("toxav_call failed: %d\n", rc);
                ck_assert(0);
            }
        }

        while (!BobCC.incoming) {
            iterate_tox(bootstrap, Alice, Bob);
        }

        {
            TOXAV_ERR_ANSWER rc;
            toxav_answer(BobAV, 0, 48, 0, &rc);

            if (rc != TOXAV_ERR_ANSWER_OK) {
                printf("toxav_answer failed: %d\n", rc);
                ck_assert(0);
            }
        }

        int16_t PCM[5670];

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_PAUSE);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(!toxav_audio_send_frame(AliceAV, 0, PCM, 960, 1, 48000, nullptr));
        ck_assert(!toxav_audio_send_frame(BobAV, 0, PCM, 960, 1, 48000, nullptr));
        ck_assert_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_RESUME);
        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(toxav_audio_send_frame(AliceAV, 0, PCM, 960, 1, 48000, nullptr));
        ck_assert(toxav_audio_send_frame(BobAV, 0, PCM, 960, 1, 48000, nullptr));
        iterate_tox(bootstrap, Alice, Bob);

        {
            TOXAV_ERR_CALL_CONTROL rc;
            toxav_call_control(AliceAV, 0, TOXAV_CALL_CONTROL_CANCEL, &rc);

            if (rc != TOXAV_ERR_CALL_CONTROL_OK) {
                printf("toxav_call_control failed: %d\n", rc);
                ck_assert(0);
            }
        }

        iterate_tox(bootstrap, Alice, Bob);
        ck_assert(BobCC.state == TOXAV_FRIEND_CALL_STATE_FINISHED);

        printf("Success!\n");
    }

    toxav_kill(BobAV);
    toxav_kill(AliceAV);
    tox_kill(Bob);
    tox_kill(Alice);
    tox_kill(bootstrap);

    printf("\nTest successful!\n");
}

int main(void)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    test_av_flows();
    return 0;
}
