#include <stdint.h>
#include <string.h>

#include "pcfg_crc32.h"
#include "pcfg_media_internal.h"
#include "test_harness.h"

static const uint8_t expected_header[PCFG_MEDIA_HEADER_SIZE] = {
    0x31, 0x46, 0x43, 0x50, 0x01, 0x00, 0x20, 0x00,
    0x04, 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x26, 0x39, 0xf4, 0xcb, 0x00, 0x00, 0x00, 0x00,
    0xcb, 0x0c, 0xcd, 0xaf, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t expected_commit[PCFG_MEDIA_COMMIT_SIZE] = {
    0x31, 0x54, 0x4d, 0x43, 0x04, 0x03, 0x02, 0x01,
    0x26, 0x39, 0xf4, 0xcb, 0xd0, 0x2c, 0xea, 0xa7
};

static pcfg_media_header_t sample_header(void)
{
    pcfg_media_header_t header;
    header.magic = PCFG_MEDIA_DEFAULT_MAGIC;
    header.sequence = UINT32_C(0x01020304);
    header.payload_size = UINT32_C(3);
    header.payload_crc32 = UINT32_C(0xcbf43926);
    return header;
}

static pcfg_media_commit_t sample_commit(void)
{
    pcfg_media_commit_t commit;
    commit.sequence = UINT32_C(0x01020304);
    commit.payload_crc32 = UINT32_C(0xcbf43926);
    return commit;
}

static void test_crc32_iso_hdlc_golden_vector(void)
{
    static const char vector[] = "123456789";
    TEST_ASSERT_EQ_U32(UINT32_C(0xcbf43926),
                       pcfg_crc32(vector, sizeof(vector) - 1u));
}

static void test_crc32_streaming_matches_one_shot(void)
{
    static const char vector[] = "123456789";
    uint32_t state = pcfg_crc32_init();
    state = pcfg_crc32_update(state, vector, 3u);
    state = pcfg_crc32_update(state, vector + 3, 2u);
    state = pcfg_crc32_update(state, vector + 5, 4u);
    TEST_ASSERT_EQ_U32(pcfg_crc32(vector, sizeof(vector) - 1u),
                       pcfg_crc32_finish(state));
}

static void test_header_encoding_is_exact_little_endian(void)
{
    uint8_t bytes[PCFG_MEDIA_HEADER_SIZE];
    pcfg_media_header_t decoded;
    pcfg_media_header_t header = sample_header();

    pcfg_media_encode_header(bytes, &header);
    TEST_ASSERT_MEM_EQ(expected_header, bytes, sizeof(bytes));
    TEST_ASSERT(pcfg_media_decode_header(bytes, PCFG_MEDIA_DEFAULT_MAGIC,
                                         &decoded) == PCFG_MEDIA_VALID);
    TEST_ASSERT_EQ_U32(header.sequence, decoded.sequence);
    TEST_ASSERT_EQ_U32(header.payload_size, decoded.payload_size);
    TEST_ASSERT_EQ_U32(header.payload_crc32, decoded.payload_crc32);
}

static void test_header_crc_excludes_its_own_field(void)
{
    uint8_t bytes[PCFG_MEDIA_HEADER_SIZE];
    pcfg_media_header_t decoded;
    pcfg_media_header_t header = sample_header();

    pcfg_media_encode_header(bytes, &header);
    bytes[8] ^= UINT8_C(0x01);
    TEST_ASSERT(pcfg_media_decode_header(bytes, PCFG_MEDIA_DEFAULT_MAGIC,
                                         &decoded) == PCFG_MEDIA_CORRUPT);

    pcfg_media_encode_header(bytes, &header);
    bytes[24] ^= UINT8_C(0x01);
    TEST_ASSERT(pcfg_media_decode_header(bytes, PCFG_MEDIA_DEFAULT_MAGIC,
                                         &decoded) == PCFG_MEDIA_CORRUPT);
}

static void test_commit_encoding_is_exact_little_endian(void)
{
    uint8_t bytes[PCFG_MEDIA_COMMIT_SIZE];
    pcfg_media_commit_t decoded;
    pcfg_media_commit_t commit = sample_commit();

    pcfg_media_encode_commit(bytes, &commit);
    TEST_ASSERT_MEM_EQ(expected_commit, bytes, sizeof(bytes));
    TEST_ASSERT(pcfg_media_decode_commit(bytes, &decoded) == PCFG_MEDIA_VALID);
    TEST_ASSERT_EQ_U32(commit.sequence, decoded.sequence);
    TEST_ASSERT_EQ_U32(commit.payload_crc32, decoded.payload_crc32);
}

static void test_decode_rejects_nonzero_v1_flags_and_reserved(void)
{
    uint8_t bytes[PCFG_MEDIA_HEADER_SIZE];
    pcfg_media_header_t decoded;
    pcfg_media_header_t header = sample_header();

    pcfg_media_encode_header(bytes, &header);
    bytes[20] = 1u;
    TEST_ASSERT(pcfg_media_decode_header(bytes, PCFG_MEDIA_DEFAULT_MAGIC,
                                         &decoded) == PCFG_MEDIA_UNSUPPORTED);

    pcfg_media_encode_header(bytes, &header);
    bytes[28] = 1u;
    TEST_ASSERT(pcfg_media_decode_header(bytes, PCFG_MEDIA_DEFAULT_MAGIC,
                                         &decoded) == PCFG_MEDIA_UNSUPPORTED);
}

int main(void)
{
    RUN_TEST(test_crc32_iso_hdlc_golden_vector);
    RUN_TEST(test_crc32_streaming_matches_one_shot);
    RUN_TEST(test_header_encoding_is_exact_little_endian);
    RUN_TEST(test_header_crc_excludes_its_own_field);
    RUN_TEST(test_commit_encoding_is_exact_little_endian);
    RUN_TEST(test_decode_rejects_nonzero_v1_flags_and_reserved);
    puts("test_crc_media: PASS");
    return 0;
}
