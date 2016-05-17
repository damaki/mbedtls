/*
 * aead_chacha20_poly1305.c
 *
 *  Created on: 17 May 2016
 *      Author: dan
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_AEAD_CHACHA20_POLY1305_C)

#include "mbedtls/aead_chacha20_poly1305.h"
#include <string.h>

#if defined(MBEDTLS_SELF_TEST)
#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdio.h>
#define mbedtls_printf printf
#endif /* MBEDTLS_PLATFORM_C */
#endif /* MBEDTLS_SELF_TEST */

#if !defined(MBEDTLS_AEAD_CHACHA20_POLY1305_ALT)

#define AEAD_CHACHA20_POLY1305_STATE_INIT       ( 0 )
#define AEAD_CHACHA20_POLY1305_STATE_AAD        ( 1 )
#define AEAD_CHACHA20_POLY1305_STATE_CIPHERTEXT ( 2 ) /* Encrypting or decrypting */
#define AEAD_CHACHA20_POLY1305_STATE_FINISHED   ( 3 )

/* Implementation that should never be optimized out by the compiler */
static void mbedtls_zeroize( void *v, size_t n ) {
    volatile unsigned char *p = v; while( n-- ) *p++ = 0;
}

/**
 * \brief           Adds padding bytes (zeroes) to pad the AAD for Poly1305.
 *
 * \param ctx       The ChaCha20-Poly1305 context.
 */
static void mbedtls_aead_chacha20_poly1305_pad_aad( mbedtls_aead_chacha20_poly1305_context *ctx )
{
    uint32_t partial_block_len = (uint32_t)( ctx->aad_len % 16U );
    unsigned char zeroes[15];

    if ( partial_block_len > 0U )
    {
        memset( zeroes, 0, sizeof(zeroes) );
        (void)mbedtls_poly1305_update( &ctx->poly1305_ctx,
                                       16U - partial_block_len,
                                       zeroes );
    }
}

/**
 * \brief           Adds padding bytes (zeroes) to pad the ciphertext for Poly1305.
 *
 * \param ctx       The ChaCha20-Poly1305 context.
 */
static void mbedtls_aead_chacha20_poly1305_pad_ciphertext( mbedtls_aead_chacha20_poly1305_context *ctx )
{
    uint32_t partial_block_len = (uint32_t)( ctx->ciphertext_len % 16U );
    unsigned char zeroes[15];

    if ( partial_block_len > 0U )
    {
        memset( zeroes, 0, sizeof(zeroes) );
        (void)mbedtls_poly1305_update( &ctx->poly1305_ctx,
                                       16U - partial_block_len,
                                       zeroes );
    }
}

void mbedtls_aead_chacha20_poly1305_init( mbedtls_aead_chacha20_poly1305_context *ctx )
{
    if ( ctx != NULL )
    {
        mbedtls_chacha20_init( &ctx->chacha20_ctx );
        mbedtls_poly1305_init( &ctx->poly1305_ctx );
        ctx->aad_len        = 0U;
        ctx->ciphertext_len = 0U;
        ctx->state          = AEAD_CHACHA20_POLY1305_STATE_INIT;
        ctx->mode           = MBEDTLS_AEAD_CHACHA20_POLY1305_ENCRYPT;
    }
}

void mbedtls_aead_chacha20_poly1305_free( mbedtls_aead_chacha20_poly1305_context *ctx )
{
    if ( ctx != NULL )
    {
        mbedtls_chacha20_free( &ctx->chacha20_ctx );
        mbedtls_poly1305_free( &ctx->poly1305_ctx );
        ctx->aad_len        = 0U;
        ctx->ciphertext_len = 0U;
        ctx->state          = AEAD_CHACHA20_POLY1305_STATE_INIT;
        ctx->mode           = MBEDTLS_AEAD_CHACHA20_POLY1305_ENCRYPT;
    }
}

int mbedtls_aead_chacha20_poly1305_setkey( mbedtls_aead_chacha20_poly1305_context *ctx,
                                           const unsigned char key[32] )
{
    int result;

    if ( ( ctx == NULL ) || ( key == NULL ) )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_INPUT_DATA );
    }

    result = mbedtls_chacha20_setkey( &ctx->chacha20_ctx, key );

    return( result );
}

int mbedtls_aead_chacha20_poly1305_starts( mbedtls_aead_chacha20_poly1305_context *ctx,
                                           const unsigned char nonce[12],
                                           mbedtls_aead_chacha20_poly1305_mode_t mode  )
{
    int result;
    unsigned char poly1305_key[64];

    if ( ( ctx == NULL ) || ( nonce == NULL ) )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_INPUT_DATA );
    }

    result = mbedtls_chacha20_starts( &ctx->chacha20_ctx, nonce, 1U );
    if ( result != 0 )
        goto cleanup;

    /* Generate the Poly1305 key by getting the ChaCha20 keystream output with counter = 0.
     * Only the first 256-bits (32 bytes) of the key is used for Poly1305.
     * The other 256 bits are discarded.
     */
    result = mbedtls_chacha20_keystream_block( &ctx->chacha20_ctx, 0U, poly1305_key );
    if ( result != 0 )
        goto cleanup;

    result = mbedtls_poly1305_setkey( &ctx->poly1305_ctx, poly1305_key );

    if ( result == 0 )
    {
        ctx->aad_len        = 0U;
        ctx->ciphertext_len = 0U;
        ctx->state          = AEAD_CHACHA20_POLY1305_STATE_AAD;
        ctx->mode           = mode;
    }

cleanup:
    mbedtls_zeroize( poly1305_key, 64U );
    return( result );
}

int mbedtls_aead_chacha20_poly1305_update_aad( mbedtls_aead_chacha20_poly1305_context *ctx,
                                               size_t aad_len,
                                               const unsigned char *aad )
{
    if ( ( ctx == NULL ) || ( aad == NULL ) )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_INPUT_DATA );
    }
    else if ( ctx->state != AEAD_CHACHA20_POLY1305_STATE_AAD )
    {
        return (MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_STATE );
    }

    ctx->aad_len += aad_len;

    return ( mbedtls_poly1305_update( &ctx->poly1305_ctx, aad_len, aad ) );
}

int mbedtls_aead_chacha20_poly1305_update( mbedtls_aead_chacha20_poly1305_context *ctx,
                                           size_t len,
                                           const unsigned char *input,
                                           unsigned char *output )
{
    if ( ( ctx == NULL ) || ( input == NULL ) || ( output == NULL ) )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_INPUT_DATA );
    }
    else if ( ( ctx->state != AEAD_CHACHA20_POLY1305_STATE_AAD ) &&
              ( ctx->state != AEAD_CHACHA20_POLY1305_STATE_CIPHERTEXT ) )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_STATE );
    }

    if ( ctx->state == AEAD_CHACHA20_POLY1305_STATE_AAD )
    {
        ctx->state = AEAD_CHACHA20_POLY1305_STATE_CIPHERTEXT;

        mbedtls_aead_chacha20_poly1305_pad_aad( ctx );
    }

    ctx->ciphertext_len += len;

    if ( ctx->mode == MBEDTLS_AEAD_CHACHA20_POLY1305_ENCRYPT )
    {
        /* Note: the following functions return an error only if one or more of
         *       the input pointers are NULL. Since we have checked their validity
         *       above, we can safety ignore the return value.
         */
        (void)mbedtls_chacha20_update( &ctx->chacha20_ctx, len, input, output );
        (void)mbedtls_poly1305_update( &ctx->poly1305_ctx, len, output );
    }
    else /* DECRYPT */
    {
        (void)mbedtls_poly1305_update( &ctx->poly1305_ctx, len, input );
        (void)mbedtls_chacha20_update( &ctx->chacha20_ctx, len, input, output );
    }

    return( 0 );
}

int mbedtls_aead_chacha20_poly1305_finish( mbedtls_aead_chacha20_poly1305_context *ctx,
                                           unsigned char mac[16] )
{
    unsigned char len_block[16];

    if ( ( ctx == NULL ) || ( mac == NULL ) )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_INPUT_DATA );
    }
    else if ( ctx->state == AEAD_CHACHA20_POLY1305_STATE_INIT )
    {
        return( MBEDTLS_ERR_AEAD_CHACHA20_POLY1305_BAD_STATE );
    }

    if ( ctx->state == AEAD_CHACHA20_POLY1305_STATE_AAD )
    {
        mbedtls_aead_chacha20_poly1305_pad_aad( ctx );
    }
    else if ( ctx->state == AEAD_CHACHA20_POLY1305_STATE_CIPHERTEXT )
    {
        mbedtls_aead_chacha20_poly1305_pad_ciphertext( ctx );
    }

    ctx->state = AEAD_CHACHA20_POLY1305_STATE_FINISHED;

    /* The lengths of the AAD and ciphertext are processed by
     * Poly1305 as the final 128-bit block, encoded as little-endian integers.
     */
    len_block[0]  = (unsigned char)ctx->aad_len;
    len_block[1]  = (unsigned char)( ctx->aad_len >> 8 );
    len_block[2]  = (unsigned char)( ctx->aad_len >> 16 );
    len_block[3]  = (unsigned char)( ctx->aad_len >> 24 );
    len_block[4]  = (unsigned char)( ctx->aad_len >> 32 );
    len_block[5]  = (unsigned char)( ctx->aad_len >> 40 );
    len_block[6]  = (unsigned char)( ctx->aad_len >> 48 );
    len_block[7]  = (unsigned char)( ctx->aad_len >> 56 );
    len_block[8]  = (unsigned char)ctx->ciphertext_len;
    len_block[9]  = (unsigned char)( ctx->ciphertext_len >> 8 );
    len_block[10] = (unsigned char)( ctx->ciphertext_len >> 16 );
    len_block[11] = (unsigned char)( ctx->ciphertext_len >> 24 );
    len_block[12] = (unsigned char)( ctx->ciphertext_len >> 32 );
    len_block[13] = (unsigned char)( ctx->ciphertext_len >> 40 );
    len_block[14] = (unsigned char)( ctx->ciphertext_len >> 48 );
    len_block[15] = (unsigned char)( ctx->ciphertext_len >> 56 );

    (void)mbedtls_poly1305_update( &ctx->poly1305_ctx, 16U, len_block );
    (void)mbedtls_poly1305_finish( &ctx->poly1305_ctx, mac );

    return( 0 );
}

#endif /* MBEDTLS_AEAD_CHACHA20_POLY1305_ALT */

int mbedtls_aead_chacha20_poly1305_crypt_and_mac ( const unsigned char key[32],
                                                    const unsigned char nonce[12],
                                                    mbedtls_aead_chacha20_poly1305_mode_t mode,
                                                    size_t aad_len,
                                                    const unsigned char *aad,
                                                    size_t ilen,
                                                    const unsigned char *input,
                                                    unsigned char *output,
                                                    unsigned char mac[16] )
{
    mbedtls_aead_chacha20_poly1305_context ctx;
    int result;

    mbedtls_aead_chacha20_poly1305_init( &ctx );

    result = mbedtls_aead_chacha20_poly1305_setkey( &ctx, key );
    if ( result != 0 )
        goto cleanup;

    result = mbedtls_aead_chacha20_poly1305_starts( &ctx, nonce, mode );
    if ( result != 0 )
        goto cleanup;

    result = mbedtls_aead_chacha20_poly1305_update_aad( &ctx, aad_len, aad );
    if ( result != 0 )
            goto cleanup;

    result = mbedtls_aead_chacha20_poly1305_update( &ctx, ilen, input, output );
    if ( result != 0 )
            goto cleanup;

    result = mbedtls_aead_chacha20_poly1305_finish( &ctx, mac );

cleanup:
    mbedtls_aead_chacha20_poly1305_free( &ctx );
    return( result );
}

#if defined(MBEDTLS_SELF_TEST)

static const unsigned char test_key[1][32] =
{
    {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    }
};

static const unsigned char test_nonce[1][12] =
{
    {
        0x07, 0x00, 0x00, 0x00,                         /* 32-bit common part */
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47  /* 64-bit IV */
    }
};

static const unsigned char test_aad[1][12] =
{
    {
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7
    }
};

static const size_t test_aad_len[1] =
{
    12U
};

static const unsigned char test_input[1][114] =
{
    {
        0x4c, 0x61, 0x64, 0x69, 0x65, 0x73, 0x20, 0x61,
        0x6e, 0x64, 0x20, 0x47, 0x65, 0x6e, 0x74, 0x6c,
        0x65, 0x6d, 0x65, 0x6e, 0x20, 0x6f, 0x66, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x61, 0x73,
        0x73, 0x20, 0x6f, 0x66, 0x20, 0x27, 0x39, 0x39,
        0x3a, 0x20, 0x49, 0x66, 0x20, 0x49, 0x20, 0x63,
        0x6f, 0x75, 0x6c, 0x64, 0x20, 0x6f, 0x66, 0x66,
        0x65, 0x72, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x6f,
        0x6e, 0x6c, 0x79, 0x20, 0x6f, 0x6e, 0x65, 0x20,
        0x74, 0x69, 0x70, 0x20, 0x66, 0x6f, 0x72, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x66, 0x75, 0x74, 0x75,
        0x72, 0x65, 0x2c, 0x20, 0x73, 0x75, 0x6e, 0x73,
        0x63, 0x72, 0x65, 0x65, 0x6e, 0x20, 0x77, 0x6f,
        0x75, 0x6c, 0x64, 0x20, 0x62, 0x65, 0x20, 0x69,
        0x74, 0x2e
    }
};

static const unsigned char test_output[1][114] =
{
    {
        0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb,
        0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e, 0xc2,
        0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe,
        0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6,
        0x3d, 0xbe, 0xa4, 0x5e, 0x8c, 0xa9, 0x67, 0x12,
        0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
        0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29,
        0x05, 0xd6, 0xa5, 0xb6, 0x7e, 0xcd, 0x3b, 0x36,
        0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c,
        0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58,
        0xfa, 0xb3, 0x24, 0xe4, 0xfa, 0xd6, 0x75, 0x94,
        0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
        0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d,
        0xe5, 0x76, 0xd2, 0x65, 0x86, 0xce, 0xc6, 0x4b,
        0x61, 0x16
    }
};

static const size_t test_input_len[1] =
{
    114U
};

static const unsigned char test_mac[1][16] =
{
    {
        0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a,
        0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91
    }
};

int mbedtls_aead_chacha20_poly1305_self_test( int verbose )
{
    size_t i;
    int result;
    unsigned char output[200];
    unsigned char mac[16];

    for ( i = 0U; i < 1U; i++ )
    {
        result = mbedtls_aead_chacha20_poly1305_crypt_and_mac( test_key[i],
                                                               test_nonce[i],
                                                               MBEDTLS_AEAD_CHACHA20_POLY1305_ENCRYPT,
                                                               test_aad_len[i],
                                                               test_aad[i],
                                                               test_input_len[i],
                                                               test_input[i],
                                                               output,
                                                               mac );
        if ( result != 0 )
        {
            if ( verbose != 0 )
            {
                mbedtls_printf( "ChaCha20-Poly1305 test %zi error code: %i\n", i, result );
            }
            return( -1 );
        }

        if ( memcmp( output, test_output[i], test_input_len[i] ) != 0 )
        {
            if ( verbose != 0 )
            {
                mbedtls_printf( "ChaCha20-Poly1305 test %zi failure (wrong output)\n", i );
            }
            return( -1 );
        }

        if ( memcmp( mac, test_mac[i], 16U ) != 0 )
        {
            if ( verbose != 0 )
            {
                mbedtls_printf( "ChaCha20-Poly1305 test %zi failure (wrong MAC)\n", i );
            }
            return( -1 );
        }
    }

    return( 0 );
}

#endif /* MBEDTLS_SELF_TEST */

#endif /* MBEDTLS_AEAD_CHACHA20_POLY1305_C */
