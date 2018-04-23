#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <stdio.h>
#include <sgx_key_exchange.h>
#include "eckey.h"

static enum _error_type {
	e_none,
	e_crypto,
	e_system
} error_type= e_none;

static const char *ep= NULL;

/* Load an EC key from a file in PEM format */

int key_load_file (EC_KEY **eckey, const char *filename)
{
	EVP_PKEY *key;
	FILE *fp;

	error_type= e_none;

	key= EVP_PKEY_new();

	if ( (fp= fopen(filename, "r")) == NULL ) {
		error_type= e_system;
		ep= filename;
		return 0;
	}
	PEM_read_PrivateKey(fp, &key, NULL, NULL);
	fclose(fp);

	*eckey= EVP_PKEY_get1_EC_KEY(key);
	if ( *eckey == NULL ) {
		error_type= e_crypto;
		return 0;
	}

	return 1;
}

EC_KEY *key_from_sgx_ec256 (sgx_ec256_public_t k)
{
	EC_KEY *key= NULL;

	error_type= e_none;

	BIGNUM *gx= NULL;
	BIGNUM *gy= NULL;

	/* Get gx and gy as BIGNUMs */

	if ( (gx= BN_lebin2bn((unsigned char *) k.gx, 32, NULL)) == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( (gy= BN_lebin2bn((unsigned char *) k.gy, 32, NULL)) == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	key= EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if ( key == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EC_KEY_set_public_key_affine_coordinates(key, gx, gy) ) {
		EC_KEY_free(key);
		key= NULL;
		error_type= e_crypto;
		goto cleanup;
	}


cleanup:
	if ( gy != NULL ) BN_free(gy);
	if ( gx != NULL ) BN_free(gx);

	return key;
}

/* Compute a shared secret using the peer's public key and a generated key */

unsigned char *key_shared_secret (EC_KEY *ec_g_a, size_t *slen)
{
	EVP_PKEY_CTX *pctx= NULL;
	EVP_PKEY_CTX *kctx= NULL;
	EVP_PKEY_CTX *sctx= NULL;
	EVP_PKEY *params= NULL;
	EVP_PKEY *key= NULL;
	EVP_PKEY *g_a= NULL;
	unsigned char *secret= NULL;

	*slen= 0;
	error_type= e_none;

	/* Generate a new EC key. */

	/* Set up the parameter context */
	pctx= EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
	if ( pctx == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	/* Generate parameters for the P-256 curve */

	if ( ! EVP_PKEY_paramgen_init(pctx) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_paramgen(pctx, &params) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	/* Generate the key */

	kctx= EVP_PKEY_CTX_new(params, NULL);
	if ( kctx == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_keygen_init(kctx) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_keygen(kctx, &key) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	/* Get the peer key as an EVP_PKEY object */

	g_a= EVP_PKEY_new();
	if ( g_a == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_set1_EC_KEY(g_a, ec_g_a) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	/* Set up the shared secret derivation */

	sctx= EVP_PKEY_CTX_new(key, NULL);
	if ( sctx == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_derive_init(sctx) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	if ( ! EVP_PKEY_derive_set_peer(sctx, g_a) ) {
		error_type= e_crypto;
		goto cleanup;
	}

	/* Get the secret length */
	if ( ! EVP_PKEY_derive(sctx, NULL, slen) ) {
		error_type= e_crypto;
		goto cleanup;
	}
	fprintf(stderr, "slen= %u\n", *slen);

	secret= OPENSSL_malloc(*slen);
	if ( secret == NULL ) {
		error_type= e_crypto;
		goto cleanup;
	}
	fprintf(stderr, "secret= 0x%08x\n", secret);

	if ( ! EVP_PKEY_derive(sctx, secret, slen) ) {
		error_type= e_crypto;
		OPENSSL_free(secret);
		secret= NULL;
	}

cleanup:
	if ( sctx != NULL ) EVP_PKEY_CTX_free(sctx);
	if ( g_a != NULL ) EVP_PKEY_free(g_a);
	if ( key != NULL ) EVP_PKEY_free(key);
	if ( kctx != NULL ) EVP_PKEY_CTX_free(kctx);
	if ( params != NULL ) EVP_PKEY_free(params);
	if ( pctx != NULL ) EVP_PKEY_CTX_free(pctx);

	return secret;
}

/* Print the error */

void key_perror (const char *prefix)
{
	fprintf(stderr, "%s: ", prefix);
	if ( error_type == e_none ) fprintf(stderr, "no error\n");
	else if ( error_type == e_system ) perror(ep);
	else if ( error_type == e_crypto ) ERR_print_errors_fp(stderr);
}

