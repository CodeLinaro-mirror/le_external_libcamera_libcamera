/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * Public key signature verification
 */

#include "libcamera/internal/pub_key.h"

#if HAVE_CRYPTO
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#elif HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/gnutls.h>
#endif

#include <libcamera/base/utils.h>

#include "libcamera/internal/pub_key.h"

/**
 * \file pub_key.h
 * \brief Public key signature verification
 */

namespace libcamera {

/**
 * \class PubKey
 * \brief Public key wrapper for signature verification
 *
 * The PubKey class wraps a public key and implements signature verification. It
 * supports RSA keys with the RSA-SHA256 signature algorithm, or ML-DSA-65 keys
 * as specified in NIST FIPS 204. The signature algorithm is determined at
 * compile time.
 */

/**
 * \brief Construct a PubKey from key data
 * \param[in] key Key data encoded in DER format
 *
 * Supported key types are RSA (verified with RSA-SHA256) and ML-DSA-65
 * (verified as ML-DSA-65 according to FIPS 204).
 */
PubKey::PubKey([[maybe_unused]] Span<const uint8_t> key)
	: valid_(false)
{
#if HAVE_CRYPTO
	const uint8_t *data = key.data();
	pubkey_ = d2i_PUBKEY(nullptr, &data, key.size());
	if (!pubkey_)
		return;

	valid_ = true;
#elif HAVE_GNUTLS
	int ret = gnutls_pubkey_init(&pubkey_);
	if (ret < 0)
		return;

	const gnutls_datum_t gnuTlsKey{
		const_cast<unsigned char *>(key.data()),
		static_cast<unsigned int>(key.size())
	};
	ret = gnutls_pubkey_import(pubkey_, &gnuTlsKey, GNUTLS_X509_FMT_DER);
	if (ret < 0)
		return;

	valid_ = true;
#endif
}

PubKey::~PubKey()
{
#if HAVE_CRYPTO
	EVP_PKEY_free(pubkey_);
#elif HAVE_GNUTLS
	gnutls_pubkey_deinit(pubkey_);
#endif
}

/**
 * \fn bool PubKey::isValid() const
 * \brief Check is the public key is valid
 * \return True if the public key is valid, false otherwise
 */

/**
 * \brief Verify signature on data
 * \param[in] data The signed data
 * \param[in] sig The signature
 *
 * Verify that the signature \a sig matches the signed \a data for the public
 * key.
 *
 * \return True if the signature is valid, false otherwise
 */
bool PubKey::verify([[maybe_unused]] Span<const uint8_t> data,
		    [[maybe_unused]] Span<const uint8_t> sig) const
{
	if (!valid_)
		return false;

#if HAVE_CRYPTO
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx)
		return false;

	utils::scope_exit ctxGuard([&] { EVP_MD_CTX_free(ctx); });

	if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr,
				 pubkey_) <= 0)
		return false;

	int ret = EVP_DigestVerify(ctx, sig.data(), sig.size(),
				   data.data(), data.size());

	return ret == 1;

#elif HAVE_GNUTLS
	const gnutls_datum_t gnuTlsData{
		const_cast<unsigned char *>(data.data()),
		static_cast<unsigned int>(data.size())
	};

	const gnutls_datum_t gnuTlsSig{
		const_cast<unsigned char *>(sig.data()),
		static_cast<unsigned int>(sig.size())
	};

	constexpr gnutls_sign_algorithm_t algo =
		std::string(IPA_MODULE_DIR_SIGNATURE_ALGO) == std::string("ml-dsa-65") ? GNUTLS_SIGN_MLDSA65 : GNUTLS_SIGN_RSA_SHA256;

	int ret = gnutls_pubkey_verify_data2(pubkey_, algo, 0,
					     &gnuTlsData, &gnuTlsSig);

	return ret >= 0;
#else
	return false;
#endif
}

} /* namespace libcamera */
